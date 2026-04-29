#include "meos_wrapper_simple.hpp"

#include "temporal/set.hpp"
#include "temporal/span.hpp"
#include "temporal/span_aggregates.hpp"
#include "temporal/spanset.hpp"
#include "time_util.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/function/aggregate_function.hpp"
#include "duckdb/function/function_set.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

namespace {

// Shared state for all extent(*) aggregates: the result is always a Span.
// MEOS Span is a fixed-size struct so the state is POD.
struct SpanExtentState {
    Span span;
    bool isset;
};

// Generic Initialize / Combine / Finalize. Operation differs per input
// type because it dispatches to a different MEOS transfn — those are
// in the typed functors below.
struct SpanExtentBase {
    template <class STATE>
    static void Initialize(STATE &state) {
        state.isset = false;
    }

    static bool IgnoreNull() {
        return true;
    }

    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        if (!source.isset) {
            return;
        }
        if (!target.isset) {
            memcpy(&target.span, &source.span, sizeof(Span));
            target.isset = true;
        } else {
            (void) span_extent_transfn(&target.span, &source.span);
        }
    }

    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
        if (!state.isset) {
            finalize_data.ReturnNull();
            return;
        }
        target = finalize_data.ReturnString(
            string_t(reinterpret_cast<const char *>(&state.span), sizeof(Span)));
    }
};

// extent(span)
struct SpanExtentFunction : SpanExtentBase {
    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        const Span *input_span = reinterpret_cast<const Span *>(input.GetData());
        if (!state.isset) {
            memcpy(&state.span, input_span, sizeof(Span));
            state.isset = true;
        } else {
            (void) span_extent_transfn(&state.span, input_span);
        }
    }

    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &unary_input,
                                  idx_t /*count*/) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, unary_input);
    }
};

// extent(spanset)
struct SpansetExtentFunction : SpanExtentBase {
    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        const SpanSet *input_ss = reinterpret_cast<const SpanSet *>(input.GetData());
        if (!state.isset) {
            // spanset_extent_transfn(NULL, ss) returns palloc'd Span; copy into state.
            Span *fresh = spanset_extent_transfn(nullptr, input_ss);
            memcpy(&state.span, fresh, sizeof(Span));
            free(fresh);
            state.isset = true;
        } else {
            (void) spanset_extent_transfn(&state.span, input_ss);
        }
    }

    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &unary_input,
                                  idx_t /*count*/) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, unary_input);
    }
};

// extent(<scalar>) — input is a primitive numeric / temporal value rather
// than a varlena blob. Each subtype dispatches to its own MEOS transfn.
struct IntExtentFunction : SpanExtentBase {
    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        if (!state.isset) {
            Span *fresh = int_extent_transfn(nullptr, static_cast<int>(input));
            memcpy(&state.span, fresh, sizeof(Span));
            free(fresh);
            state.isset = true;
        } else {
            (void) int_extent_transfn(&state.span, static_cast<int>(input));
        }
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &unary_input,
                                  idx_t /*count*/) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, unary_input);
    }
};

struct BigintExtentFunction : SpanExtentBase {
    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        if (!state.isset) {
            Span *fresh = bigint_extent_transfn(nullptr, static_cast<int64>(input));
            memcpy(&state.span, fresh, sizeof(Span));
            free(fresh);
            state.isset = true;
        } else {
            (void) bigint_extent_transfn(&state.span, static_cast<int64>(input));
        }
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &unary_input,
                                  idx_t /*count*/) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, unary_input);
    }
};

struct FloatExtentFunction : SpanExtentBase {
    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        if (!state.isset) {
            Span *fresh = float_extent_transfn(nullptr, static_cast<double>(input));
            memcpy(&state.span, fresh, sizeof(Span));
            free(fresh);
            state.isset = true;
        } else {
            (void) float_extent_transfn(&state.span, static_cast<double>(input));
        }
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &unary_input,
                                  idx_t /*count*/) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, unary_input);
    }
};

struct DateExtentFunction : SpanExtentBase {
    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        // INPUT_TYPE is duckdb::date_t (offset from 1970); MEOS DateADT
        // is offset from 2000. Convert before passing.
        int32_t meos_d = ToMeosDate(input);
        if (!state.isset) {
            Span *fresh = date_extent_transfn(nullptr, meos_d);
            memcpy(&state.span, fresh, sizeof(Span));
            free(fresh);
            state.isset = true;
        } else {
            (void) date_extent_transfn(&state.span, meos_d);
        }
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &unary_input,
                                  idx_t /*count*/) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, unary_input);
    }
};

struct TimestamptzExtentFunction : SpanExtentBase {
    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        // INPUT_TYPE is duckdb::timestamp_t (1970 epoch); MEOS TimestampTz
        // uses 2000 epoch.
        timestamp_tz_t in_ts;
        in_ts.value = input.value;
        timestamp_tz_t meos_ts = DuckDBToMeosTimestamp(in_ts);
        if (!state.isset) {
            Span *fresh = timestamptz_extent_transfn(nullptr, meos_ts.value);
            memcpy(&state.span, fresh, sizeof(Span));
            free(fresh);
            state.isset = true;
        } else {
            (void) timestamptz_extent_transfn(&state.span, meos_ts.value);
        }
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &unary_input,
                                  idx_t /*count*/) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, unary_input);
    }
};

// extent(set)
struct SetExtentFunction : SpanExtentBase {
    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        const Set *input_set = reinterpret_cast<const Set *>(input.GetData());
        if (!state.isset) {
            Span *fresh = set_extent_transfn(nullptr, input_set);
            memcpy(&state.span, fresh, sizeof(Span));
            free(fresh);
            state.isset = true;
        } else {
            (void) set_extent_transfn(&state.span, input_set);
        }
    }

    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &unary_input,
                                  idx_t /*count*/) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, unary_input);
    }
};

template <class FUNC>
static AggregateFunction MakeExtentAggregate(const LogicalType &input_type, const LogicalType &output_span_type) {
    return AggregateFunction::UnaryAggregate<SpanExtentState, string_t, string_t, FUNC>(
        input_type, output_span_type);
}

// ============================================================================
// spanUnion(span | spanset) -> spanset
//
// State holds a heap-allocated SpanSet *. MEOS span_union_transfn allocates
// a fresh spanset on first call and either appends in place or returns a
// new (possibly larger) spanset on subsequent calls — we always reassign
// state.spanset to whatever the transfn returns. spanset_union_finalfn
// compacts the state and frees it; we null state.spanset after the call
// to keep our destructor from double-freeing.
// ============================================================================

struct SpansetUnionState {
    SpanSet *spanset;
};

template <bool INPUT_IS_SPANSET>
struct SpanUnionFunction {
    template <class STATE>
    static void Initialize(STATE &state) {
        state.spanset = nullptr;
    }

    static bool IgnoreNull() {
        return true;
    }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(input.GetData());
        size_t size = input.GetSize();
        if constexpr (INPUT_IS_SPANSET) {
            SpanSet *ss = reinterpret_cast<SpanSet *>(malloc(size));
            memcpy(ss, bytes, size);
            state.spanset = spanset_union_transfn(state.spanset, ss);
            free(ss);
        } else {
            Span *s = reinterpret_cast<Span *>(malloc(size));
            memcpy(s, bytes, size);
            state.spanset = span_union_transfn(state.spanset, s);
            free(s);
        }
    }

    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &unary_input,
                                  idx_t /*count*/) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, unary_input);
    }

    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        if (!source.spanset || source.spanset->count == 0) {
            return;
        }
        // spanset_union_transfn(target, source) merges source into target.
        // It may return target unchanged (if it had room) or a new larger
        // spanset; in the latter case it frees the old target. Always
        // reassign whatever is returned.
        const_cast<STATE &>(target).spanset =
            spanset_union_transfn(target.spanset, source.spanset);
    }

    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
        if (!state.spanset) {
            finalize_data.ReturnNull();
            return;
        }
        SpanSet *result = spanset_union_finalfn(state.spanset);
        // spanset_union_finalfn calls pfree(state) internally; null out
        // the pointer so our destructor doesn't double-free.
        state.spanset = nullptr;
        if (!result) {
            finalize_data.ReturnNull();
            return;
        }
        size_t out_size = spanset_mem_size(result);
        target = finalize_data.ReturnString(
            string_t(reinterpret_cast<const char *>(result), out_size));
        free(result);
    }

    template <class STATE>
    static void Destroy(STATE &state, AggregateInputData &) {
        if (state.spanset) {
            free(state.spanset);
            state.spanset = nullptr;
        }
    }
};

template <bool INPUT_IS_SPANSET>
static AggregateFunction MakeSpanUnionAggregate(const LogicalType &input_type, const LogicalType &output_spanset_type) {
    using OPS = SpanUnionFunction<INPUT_IS_SPANSET>;
    return AggregateFunction::UnaryAggregateDestructor<SpansetUnionState, string_t, string_t, OPS>(
        input_type, output_spanset_type);
}

} // namespace

void SpanAggregates::AddExtentOverloads(AggregateFunctionSet &extent_set) {
    // extent(<span>) -> <span>
    extent_set.AddFunction(MakeExtentAggregate<SpanExtentFunction>(SpanTypes::INTSPAN(), SpanTypes::INTSPAN()));
    extent_set.AddFunction(MakeExtentAggregate<SpanExtentFunction>(SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()));
    extent_set.AddFunction(MakeExtentAggregate<SpanExtentFunction>(SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()));
    extent_set.AddFunction(MakeExtentAggregate<SpanExtentFunction>(SpanTypes::DATESPAN(), SpanTypes::DATESPAN()));
    extent_set.AddFunction(MakeExtentAggregate<SpanExtentFunction>(SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()));

    // extent(<spanset>) -> <span>
    extent_set.AddFunction(MakeExtentAggregate<SpansetExtentFunction>(SpansetTypes::intspanset(), SpanTypes::INTSPAN()));
    extent_set.AddFunction(MakeExtentAggregate<SpansetExtentFunction>(SpansetTypes::bigintspanset(), SpanTypes::BIGINTSPAN()));
    extent_set.AddFunction(MakeExtentAggregate<SpansetExtentFunction>(SpansetTypes::floatspanset(), SpanTypes::FLOATSPAN()));
    extent_set.AddFunction(MakeExtentAggregate<SpansetExtentFunction>(SpansetTypes::datespanset(), SpanTypes::DATESPAN()));
    extent_set.AddFunction(MakeExtentAggregate<SpansetExtentFunction>(SpansetTypes::tstzspanset(), SpanTypes::TSTZSPAN()));

    // extent(<set>) -> <span>. textset has no span equivalent.
    extent_set.AddFunction(MakeExtentAggregate<SetExtentFunction>(SetTypes::intset(), SpanTypes::INTSPAN()));
    extent_set.AddFunction(MakeExtentAggregate<SetExtentFunction>(SetTypes::bigintset(), SpanTypes::BIGINTSPAN()));
    extent_set.AddFunction(MakeExtentAggregate<SetExtentFunction>(SetTypes::floatset(), SpanTypes::FLOATSPAN()));
    extent_set.AddFunction(MakeExtentAggregate<SetExtentFunction>(SetTypes::dateset(), SpanTypes::DATESPAN()));
    extent_set.AddFunction(MakeExtentAggregate<SetExtentFunction>(SetTypes::tstzset(), SpanTypes::TSTZSPAN()));

    // extent(<scalar>) -> <span>. Primitive inputs (not blob-encoded);
    // matches MobilityDB's extent(integer) / (bigint) / (float) / (date)
    // / (timestamptz) overloads.
    extent_set.AddFunction(
        AggregateFunction::UnaryAggregate<SpanExtentState, int32_t, string_t, IntExtentFunction>(
            LogicalType::INTEGER, SpanTypes::INTSPAN()));
    extent_set.AddFunction(
        AggregateFunction::UnaryAggregate<SpanExtentState, int64_t, string_t, BigintExtentFunction>(
            LogicalType::BIGINT, SpanTypes::BIGINTSPAN()));
    extent_set.AddFunction(
        AggregateFunction::UnaryAggregate<SpanExtentState, double, string_t, FloatExtentFunction>(
            LogicalType::DOUBLE, SpanTypes::FLOATSPAN()));
    extent_set.AddFunction(
        AggregateFunction::UnaryAggregate<SpanExtentState, date_t, string_t, DateExtentFunction>(
            LogicalType::DATE, SpanTypes::DATESPAN()));
    extent_set.AddFunction(
        AggregateFunction::UnaryAggregate<SpanExtentState, timestamp_t, string_t, TimestamptzExtentFunction>(
            LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()));
}

void SpanAggregates::RegisterSpanUnion(ExtensionLoader &loader) {
    AggregateFunctionSet spanunion_set("spanUnion");

    // spanUnion(<span>) -> <spanset>
    spanunion_set.AddFunction(MakeSpanUnionAggregate<false>(SpanTypes::INTSPAN(),    SpansetTypes::intspanset()));
    spanunion_set.AddFunction(MakeSpanUnionAggregate<false>(SpanTypes::BIGINTSPAN(), SpansetTypes::bigintspanset()));
    spanunion_set.AddFunction(MakeSpanUnionAggregate<false>(SpanTypes::FLOATSPAN(),  SpansetTypes::floatspanset()));
    spanunion_set.AddFunction(MakeSpanUnionAggregate<false>(SpanTypes::DATESPAN(),   SpansetTypes::datespanset()));
    spanunion_set.AddFunction(MakeSpanUnionAggregate<false>(SpanTypes::TSTZSPAN(),   SpansetTypes::tstzspanset()));

    // spanUnion(<spanset>) -> <spanset>
    spanunion_set.AddFunction(MakeSpanUnionAggregate<true>(SpansetTypes::intspanset(),    SpansetTypes::intspanset()));
    spanunion_set.AddFunction(MakeSpanUnionAggregate<true>(SpansetTypes::bigintspanset(), SpansetTypes::bigintspanset()));
    spanunion_set.AddFunction(MakeSpanUnionAggregate<true>(SpansetTypes::floatspanset(),  SpansetTypes::floatspanset()));
    spanunion_set.AddFunction(MakeSpanUnionAggregate<true>(SpansetTypes::datespanset(),   SpansetTypes::datespanset()));
    spanunion_set.AddFunction(MakeSpanUnionAggregate<true>(SpansetTypes::tstzspanset(),   SpansetTypes::tstzspanset()));

    loader.RegisterFunction(std::move(spanunion_set));
}

} // namespace duckdb
