#include "meos_wrapper_simple.hpp"

#include "temporal/set.hpp"
#include "temporal/span.hpp"
#include "temporal/span_aggregates.hpp"
#include "temporal/spanset.hpp"

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
}

} // namespace duckdb
