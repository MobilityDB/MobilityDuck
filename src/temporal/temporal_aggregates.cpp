#include "meos_wrapper_simple.hpp"

#include "temporal/tbox.hpp"
#include "temporal/temporal.hpp"
#include "temporal/temporal_aggregates.hpp"
#include "time_util.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/function/aggregate_function.hpp"
#include "duckdb/function/function_set.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

namespace {

// State for extent(tnumber) → tbox. TBox is a fixed-size POD struct
// (Span period + Span span + int16 flags) so it can live inline in
// the aggregate state.
struct TboxExtentState {
    TBox box;
    bool isset;
};

struct TnumberExtentFunction {
    template <class STATE>
    static void Initialize(STATE &state) {
        state.isset = false;
    }

    static bool IgnoreNull() {
        return true;
    }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        // Temporal is varlena: copy the blob into a heap allocation
        // before passing to MEOS so the lifetime is well-defined.
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(input.GetData());
        size_t size = input.GetSize();
        Temporal *temp = reinterpret_cast<Temporal *>(malloc(size));
        memcpy(temp, bytes, size);

        if (!state.isset) {
            // tnumber_extent_transfn(NULL, temp) palloc's a fresh TBox.
            TBox *fresh = tnumber_extent_transfn(nullptr, temp);
            memcpy(&state.box, fresh, sizeof(TBox));
            free(fresh);
            state.isset = true;
        } else {
            (void) tnumber_extent_transfn(&state.box, temp);
        }
        free(temp);
    }

    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &unary_input,
                                  idx_t /*count*/) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, unary_input);
    }

    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        if (!source.isset) {
            return;
        }
        if (!target.isset) {
            memcpy(&target.box, &source.box, sizeof(TBox));
            target.isset = true;
        } else {
            // Combine two TBoxes: expand target with source via tbox_expand.
            // Replicates the inline path in tnumber_extent_transfn when both
            // operands are non-null TBox values.
            tbox_expand(&source.box, &target.box);
        }
    }

    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
        if (!state.isset) {
            finalize_data.ReturnNull();
            return;
        }
        target = finalize_data.ReturnString(
            string_t(reinterpret_cast<const char *>(&state.box), sizeof(TBox)));
    }
};

static AggregateFunction MakeExtentTnumberAggregate(const LogicalType &tnumber_type) {
    return AggregateFunction::UnaryAggregate<TboxExtentState, string_t, string_t, TnumberExtentFunction>(
        tnumber_type, TboxType::TBOX());
}

// ============================================================================
// tCount(temporal) → tint
//
// State holds a MEOS SkipList* (heap-allocated separately from the inline
// aggregate state). The state size is just `sizeof(SkipList *)`. Lifetime
// is managed via the aggregate destructor which calls skiplist_free.
// ============================================================================

struct TCountState {
    SkipList *skiplist;
};

// Local equivalent of MEOS's datum_sum_int32 (not exported from MEOS public
// or internal headers). Used as the merge function when splicing two
// tcount skiplists during Combine.
static Datum mduck_datum_sum_int32(Datum l, Datum r) {
    int32_t li = static_cast<int32_t>(l);
    int32_t ri = static_cast<int32_t>(r);
    return static_cast<Datum>(static_cast<int32_t>(li + ri));
}

struct TCountFunction {
    template <class STATE>
    static void Initialize(STATE &state) {
        state.skiplist = nullptr;
    }

    static bool IgnoreNull() {
        return true;
    }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(input.GetData());
        size_t size = input.GetSize();
        Temporal *temp = reinterpret_cast<Temporal *>(malloc(size));
        memcpy(temp, bytes, size);
        // MEOS allocates the SkipList on the first call (state == NULL) and
        // returns the same/extended state on subsequent calls.
        state.skiplist = temporal_tcount_transfn(state.skiplist, temp);
        free(temp);
    }

    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &unary_input,
                                  idx_t /*count*/) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, unary_input);
    }

    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        if (!source.skiplist || source.skiplist->length == 0) {
            return;
        }
        if (!target.skiplist) {
            const_cast<STATE &>(target).skiplist = temporal_skiplist_make();
        }
        // Splice source's already-counted instants into target with a
        // sum merge function. Equivalent to MEOS's internal
        // temporal_tagg_combinefn(state1, state2, datum_sum_int32, false)
        // without depending on the unexported symbol.
        void **values = skiplist_values(source.skiplist);
        int count = source.skiplist->length;
        temporal_skiplist_splice(target.skiplist, values, count,
                                 &mduck_datum_sum_int32, false);
        free(values);
    }

    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
        if (!state.skiplist || state.skiplist->length == 0) {
            finalize_data.ReturnNull();
            return;
        }
        Temporal *result = temporal_tagg_finalfn(state.skiplist);
        // temporal_tagg_finalfn calls skiplist_free internally; null out
        // the pointer so our destructor doesn't double-free.
        state.skiplist = nullptr;
        if (!result) {
            finalize_data.ReturnNull();
            return;
        }
        size_t out_size = temporal_mem_size(result);
        target = finalize_data.ReturnString(
            string_t(reinterpret_cast<const char *>(result), out_size));
        free(result);
    }

    template <class STATE>
    static void Destroy(STATE &state, AggregateInputData &) {
        if (state.skiplist) {
            skiplist_free(state.skiplist);
            state.skiplist = nullptr;
        }
    }
};

// ============================================================================
// Generic skiplist-backed aggregate (tand / tor / tmin / tmax / tsum)
//
// Same shape as TCountFunction. Parameterised on:
//   - TRANSFN: the MEOS transition function (e.g. tbool_tand_transfn)
//   - MERGE_FN: the per-instant merge function used during Combine
//                (e.g. mduck_datum_and, mduck_datum_min_int32)
//   - CROSSINGS: whether MEOS should split sequences at crossings during
//                Combine. true for tfloat aggregates, false for the rest.
// ============================================================================

using TempTransfn = SkipList *(*)(SkipList *, const Temporal *);

// Local equivalents of the unexported MEOS datum_* merge functions. Datum is
// a uintptr_t; for primitive types these are simple bit-level reinterprets.
static Datum mduck_datum_and(Datum l, Datum r) {
    return static_cast<Datum>(static_cast<bool>(l) && static_cast<bool>(r));
}
static Datum mduck_datum_or(Datum l, Datum r) {
    return static_cast<Datum>(static_cast<bool>(l) || static_cast<bool>(r));
}
static Datum mduck_datum_min_int32(Datum l, Datum r) {
    int32_t li = static_cast<int32_t>(l);
    int32_t ri = static_cast<int32_t>(r);
    return static_cast<Datum>(static_cast<int32_t>(li < ri ? li : ri));
}
static Datum mduck_datum_max_int32(Datum l, Datum r) {
    int32_t li = static_cast<int32_t>(l);
    int32_t ri = static_cast<int32_t>(r);
    return static_cast<Datum>(static_cast<int32_t>(li > ri ? li : ri));
}
// Float8: PostgreSQL/MEOS pass-by-value convention encodes the IEEE 754 bits
// of the double in the Datum. Convert via an aliased union to avoid UB.
static double datum_to_float8(Datum d) {
    union { uint64_t u; double f; } u;
    u.u = static_cast<uint64_t>(d);
    return u.f;
}
static Datum float8_to_datum(double f) {
    union { uint64_t u; double f; } u;
    u.f = f;
    return static_cast<Datum>(u.u);
}
static Datum mduck_datum_min_float8(Datum l, Datum r) {
    double lf = datum_to_float8(l);
    double rf = datum_to_float8(r);
    return float8_to_datum(lf < rf ? lf : rf);
}
static Datum mduck_datum_max_float8(Datum l, Datum r) {
    double lf = datum_to_float8(l);
    double rf = datum_to_float8(r);
    return float8_to_datum(lf > rf ? lf : rf);
}
static Datum mduck_datum_sum_float8(Datum l, Datum r) {
    return float8_to_datum(datum_to_float8(l) + datum_to_float8(r));
}
// double2 — internal MEOS struct for {sum, count} pairs used by tavg.
// Layout per meos/include/temporal/doublen.h: { double a; double b; }
// MEOS internal (not in public install). We replicate the layout here
// and provide a sum function for use as the merge function in tavg
// combine. Allocation matches the palloc path used internally so the
// MEOS skiplist's free path interoperates with our state.
struct mduck_double2 {
    double a;
    double b;
};
static Datum mduck_datum_sum_double2(Datum l, Datum r) {
    auto *lp = reinterpret_cast<mduck_double2 *>(l);
    auto *rp = reinterpret_cast<mduck_double2 *>(r);
    auto *out = reinterpret_cast<mduck_double2 *>(malloc(sizeof(mduck_double2)));
    out->a = lp->a + rp->a;
    out->b = lp->b + rp->b;
    return reinterpret_cast<Datum>(out);
}

// Text comparison: Datum carries text* (PG varlena). text_cmp is exported
// from MEOS so we just invoke it on the two pointers and pick whichever
// Datum we want.
static Datum mduck_datum_min_text(Datum l, Datum r) {
    return text_cmp(reinterpret_cast<text *>(l), reinterpret_cast<text *>(r)) < 0 ? l : r;
}
static Datum mduck_datum_max_text(Datum l, Datum r) {
    return text_cmp(reinterpret_cast<text *>(l), reinterpret_cast<text *>(r)) > 0 ? l : r;
}

template <TempTransfn TRANSFN, Datum (*MERGE_FN)(Datum, Datum), bool CROSSINGS>
struct SkiplistAggFunction {
    template <class STATE>
    static void Initialize(STATE &state) {
        state.skiplist = nullptr;
    }

    static bool IgnoreNull() {
        return true;
    }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(input.GetData());
        size_t size = input.GetSize();
        Temporal *temp = reinterpret_cast<Temporal *>(malloc(size));
        memcpy(temp, bytes, size);
        state.skiplist = TRANSFN(state.skiplist, temp);
        free(temp);
    }

    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &unary_input,
                                  idx_t /*count*/) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, unary_input);
    }

    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        if (!source.skiplist || source.skiplist->length == 0) {
            return;
        }
        if (!target.skiplist) {
            const_cast<STATE &>(target).skiplist = temporal_skiplist_make();
        }
        void **values = skiplist_values(source.skiplist);
        int count = source.skiplist->length;
        temporal_skiplist_splice(target.skiplist, values, count, MERGE_FN, CROSSINGS);
        free(values);
    }

    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
        if (!state.skiplist || state.skiplist->length == 0) {
            finalize_data.ReturnNull();
            return;
        }
        Temporal *result = temporal_tagg_finalfn(state.skiplist);
        state.skiplist = nullptr;
        if (!result) {
            finalize_data.ReturnNull();
            return;
        }
        size_t out_size = temporal_mem_size(result);
        target = finalize_data.ReturnString(
            string_t(reinterpret_cast<const char *>(result), out_size));
        free(result);
    }

    template <class STATE>
    static void Destroy(STATE &state, AggregateInputData &) {
        if (state.skiplist) {
            skiplist_free(state.skiplist);
            state.skiplist = nullptr;
        }
    }
};

template <TempTransfn TRANSFN, Datum (*MERGE_FN)(Datum, Datum), bool CROSSINGS>
static AggregateFunction MakeSkiplistAggregate(const LogicalType &input_type, const LogicalType &output_type) {
    using OPS = SkiplistAggFunction<TRANSFN, MERGE_FN, CROSSINGS>;
    return AggregateFunction::UnaryAggregateDestructor<TCountState, string_t, string_t, OPS>(
        input_type, output_type);
}

// ============================================================================
// tAvg(tnumber) -> tfloat — Skiplist of {sum, count} pairs.
//
// Differs from the generic skiplist pattern in two places:
//   1. Combine uses mduck_datum_sum_double2 (sum-of-pair merge).
//   2. Finalize uses tnumber_tavg_finalfn (computes sum/count per
//      instant) instead of the generic temporal_tagg_finalfn.
// ============================================================================

struct TAvgFunction {
    template <class STATE>
    static void Initialize(STATE &state) {
        state.skiplist = nullptr;
    }

    static bool IgnoreNull() {
        return true;
    }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(input.GetData());
        size_t size = input.GetSize();
        Temporal *temp = reinterpret_cast<Temporal *>(malloc(size));
        memcpy(temp, bytes, size);
        state.skiplist = tnumber_tavg_transfn(state.skiplist, temp);
        free(temp);
    }

    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &unary_input,
                                  idx_t /*count*/) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, unary_input);
    }

    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        if (!source.skiplist || source.skiplist->length == 0) {
            return;
        }
        if (!target.skiplist) {
            const_cast<STATE &>(target).skiplist = temporal_skiplist_make();
        }
        void **values = skiplist_values(source.skiplist);
        int count = source.skiplist->length;
        temporal_skiplist_splice(target.skiplist, values, count, &mduck_datum_sum_double2, false);
        free(values);
    }

    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
        if (!state.skiplist || state.skiplist->length == 0) {
            finalize_data.ReturnNull();
            return;
        }
        Temporal *result = tnumber_tavg_finalfn(state.skiplist);
        // tnumber_tavg_finalfn calls skiplist_free internally — same
        // contract as temporal_tagg_finalfn.
        state.skiplist = nullptr;
        if (!result) {
            finalize_data.ReturnNull();
            return;
        }
        size_t out_size = temporal_mem_size(result);
        target = finalize_data.ReturnString(
            string_t(reinterpret_cast<const char *>(result), out_size));
        free(result);
    }

    template <class STATE>
    static void Destroy(STATE &state, AggregateInputData &) {
        if (state.skiplist) {
            skiplist_free(state.skiplist);
            state.skiplist = nullptr;
        }
    }
};

static AggregateFunction MakeTavgAggregate(const LogicalType &input_type) {
    return AggregateFunction::UnaryAggregateDestructor<TCountState, string_t, string_t, TAvgFunction>(
        input_type, TemporalTypes::TFLOAT());
}

// ============================================================================
// Window aggregates (wmin / wmax / wsum) — Binary input: (Temporal, Interval)
//
// Same skiplist state shape as TCountFunction. Difference: Operation takes
// the window-width interval as second arg and forwards it to the MEOS
// w*_transfn function. Combine reuses the same merge function as the
// non-windowed counterpart since the per-instant value layout is identical.
// ============================================================================

using TempWindowTransfn = SkipList *(*)(SkipList *, const Temporal *, const ::Interval *);

template <TempWindowTransfn TRANSFN, Datum (*MERGE_FN)(Datum, Datum), bool CROSSINGS>
struct WindowAggFunction {
    template <class STATE>
    static void Initialize(STATE &state) {
        state.skiplist = nullptr;
    }

    static bool IgnoreNull() {
        return true;
    }

    template <class A_TYPE, class B_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const A_TYPE &a_input, const B_TYPE &b_input,
                          AggregateBinaryInput &) {
        // a_input is string_t holding a Temporal blob.
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(a_input.GetData());
        size_t size = a_input.GetSize();
        Temporal *temp = reinterpret_cast<Temporal *>(malloc(size));
        memcpy(temp, bytes, size);

        // b_input is duckdb::interval_t; convert to MEOS Interval.
        MeosInterval interv = IntervaltToInterval(b_input);

        state.skiplist = TRANSFN(state.skiplist, temp, &interv);
        free(temp);
    }

    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        if (!source.skiplist || source.skiplist->length == 0) {
            return;
        }
        if (!target.skiplist) {
            const_cast<STATE &>(target).skiplist = temporal_skiplist_make();
        }
        void **values = skiplist_values(source.skiplist);
        int count = source.skiplist->length;
        temporal_skiplist_splice(target.skiplist, values, count, MERGE_FN, CROSSINGS);
        free(values);
    }

    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
        if (!state.skiplist || state.skiplist->length == 0) {
            finalize_data.ReturnNull();
            return;
        }
        Temporal *result = temporal_tagg_finalfn(state.skiplist);
        state.skiplist = nullptr;
        if (!result) {
            finalize_data.ReturnNull();
            return;
        }
        size_t out_size = temporal_mem_size(result);
        target = finalize_data.ReturnString(
            string_t(reinterpret_cast<const char *>(result), out_size));
        free(result);
    }

    template <class STATE>
    static void Destroy(STATE &state, AggregateInputData &) {
        if (state.skiplist) {
            skiplist_free(state.skiplist);
            state.skiplist = nullptr;
        }
    }
};

template <TempWindowTransfn TRANSFN, Datum (*MERGE_FN)(Datum, Datum), bool CROSSINGS>
static AggregateFunction MakeWindowAggregate(const LogicalType &input_type, const LogicalType &output_type) {
    using OPS = WindowAggFunction<TRANSFN, MERGE_FN, CROSSINGS>;
    AggregateFunction fn = AggregateFunction::BinaryAggregate<TCountState, string_t, interval_t, string_t, OPS>(
        input_type, LogicalType::INTERVAL, output_type);
    fn.destructor = AggregateFunction::StateDestroy<TCountState, OPS>;
    return fn;
}

// wavg uses tnumber_tavg_finalfn instead of temporal_tagg_finalfn so it
// has its own functor parametrised on the window transfn only.
template <TempWindowTransfn TRANSFN>
struct WavgFunction {
    template <class STATE>
    static void Initialize(STATE &state) {
        state.skiplist = nullptr;
    }

    static bool IgnoreNull() {
        return true;
    }

    template <class A_TYPE, class B_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const A_TYPE &a_input, const B_TYPE &b_input,
                          AggregateBinaryInput &) {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(a_input.GetData());
        size_t size = a_input.GetSize();
        Temporal *temp = reinterpret_cast<Temporal *>(malloc(size));
        memcpy(temp, bytes, size);
        MeosInterval interv = IntervaltToInterval(b_input);
        state.skiplist = TRANSFN(state.skiplist, temp, &interv);
        free(temp);
    }

    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        if (!source.skiplist || source.skiplist->length == 0) {
            return;
        }
        if (!target.skiplist) {
            const_cast<STATE &>(target).skiplist = temporal_skiplist_make();
        }
        void **values = skiplist_values(source.skiplist);
        int count = source.skiplist->length;
        temporal_skiplist_splice(target.skiplist, values, count, &mduck_datum_sum_double2, false);
        free(values);
    }

    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
        if (!state.skiplist || state.skiplist->length == 0) {
            finalize_data.ReturnNull();
            return;
        }
        Temporal *result = tnumber_tavg_finalfn(state.skiplist);
        state.skiplist = nullptr;
        if (!result) {
            finalize_data.ReturnNull();
            return;
        }
        size_t out_size = temporal_mem_size(result);
        target = finalize_data.ReturnString(
            string_t(reinterpret_cast<const char *>(result), out_size));
        free(result);
    }

    template <class STATE>
    static void Destroy(STATE &state, AggregateInputData &) {
        if (state.skiplist) {
            skiplist_free(state.skiplist);
            state.skiplist = nullptr;
        }
    }
};

template <TempWindowTransfn TRANSFN>
static AggregateFunction MakeWavgAggregate(const LogicalType &input_type) {
    using OPS = WavgFunction<TRANSFN>;
    AggregateFunction fn = AggregateFunction::BinaryAggregate<TCountState, string_t, interval_t, string_t, OPS>(
        input_type, LogicalType::INTERVAL, TemporalTypes::TFLOAT());
    fn.destructor = AggregateFunction::StateDestroy<TCountState, OPS>;
    return fn;
}

} // namespace

void TemporalAggregates::AddExtentOverloads(AggregateFunctionSet &extent_set) {
    extent_set.AddFunction(MakeExtentTnumberAggregate(TemporalTypes::TINT()));
    extent_set.AddFunction(MakeExtentTnumberAggregate(TemporalTypes::TFLOAT()));
}

void TemporalAggregates::RegisterTCount(ExtensionLoader &loader) {
    AggregateFunctionSet tcount_set("tCount");
    for (const auto &t : TemporalTypes::AllTypes()) {
        AggregateFunction fn =
            AggregateFunction::UnaryAggregateDestructor<TCountState, string_t, string_t, TCountFunction>(
                t, TemporalTypes::TINT());
        tcount_set.AddFunction(fn);
    }
    loader.RegisterFunction(std::move(tcount_set));
}

void TemporalAggregates::RegisterTemporalAggregates(ExtensionLoader &loader) {
    // tAnd / tOr: tbool only.
    {
        AggregateFunctionSet tand_set("tAnd");
        tand_set.AddFunction(
            MakeSkiplistAggregate<&tbool_tand_transfn, &mduck_datum_and, false>(
                TemporalTypes::TBOOL(), TemporalTypes::TBOOL()));
        loader.RegisterFunction(std::move(tand_set));

        AggregateFunctionSet tor_set("tOr");
        tor_set.AddFunction(
            MakeSkiplistAggregate<&tbool_tor_transfn, &mduck_datum_or, false>(
                TemporalTypes::TBOOL(), TemporalTypes::TBOOL()));
        loader.RegisterFunction(std::move(tor_set));
    }

    // tMin / tMax / tSum: registered as tagg_min / tagg_max / tagg_sum
    // because the names Tmin / Tmax are already taken by scalar
    // time-min / time-max accessors on tbox / stbox (registered in
    // src/temporal/tbox.cpp and src/geo/stbox.cpp). DuckDB's catalog
    // is case-insensitive at the name level, so registering an
    // aggregate with the same lowercased name as an existing scalar
    // triggers an ALTER path that CreateAggregateFunctionInfo doesn't
    // support and the extension fails to load.
    {
        AggregateFunctionSet tmin_set("tagg_min");
        tmin_set.AddFunction(
            MakeSkiplistAggregate<&tint_tmin_transfn, &mduck_datum_min_int32, false>(
                TemporalTypes::TINT(), TemporalTypes::TINT()));
        tmin_set.AddFunction(
            MakeSkiplistAggregate<&tfloat_tmin_transfn, &mduck_datum_min_float8, true>(
                TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()));
        tmin_set.AddFunction(
            MakeSkiplistAggregate<&ttext_tmin_transfn, &mduck_datum_min_text, false>(
                TemporalTypes::TTEXT(), TemporalTypes::TTEXT()));
        loader.RegisterFunction(std::move(tmin_set));

        AggregateFunctionSet tmax_set("tagg_max");
        tmax_set.AddFunction(
            MakeSkiplistAggregate<&tint_tmax_transfn, &mduck_datum_max_int32, false>(
                TemporalTypes::TINT(), TemporalTypes::TINT()));
        tmax_set.AddFunction(
            MakeSkiplistAggregate<&tfloat_tmax_transfn, &mduck_datum_max_float8, true>(
                TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()));
        tmax_set.AddFunction(
            MakeSkiplistAggregate<&ttext_tmax_transfn, &mduck_datum_max_text, false>(
                TemporalTypes::TTEXT(), TemporalTypes::TTEXT()));
        loader.RegisterFunction(std::move(tmax_set));

        AggregateFunctionSet tsum_set("tagg_sum");
        tsum_set.AddFunction(
            MakeSkiplistAggregate<&tint_tsum_transfn, &mduck_datum_sum_int32, false>(
                TemporalTypes::TINT(), TemporalTypes::TINT()));
        tsum_set.AddFunction(
            MakeSkiplistAggregate<&tfloat_tsum_transfn, &mduck_datum_sum_float8, true>(
                TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()));
        loader.RegisterFunction(std::move(tsum_set));
    }
}

void TemporalAggregates::RegisterWindowAggregates(ExtensionLoader &loader) {
    // wmin: tint, tfloat
    {
        AggregateFunctionSet wmin_set("wmin");
        wmin_set.AddFunction(
            MakeWindowAggregate<&tint_wmin_transfn, &mduck_datum_min_int32, false>(
                TemporalTypes::TINT(), TemporalTypes::TINT()));
        wmin_set.AddFunction(
            MakeWindowAggregate<&tfloat_wmin_transfn, &mduck_datum_min_float8, true>(
                TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()));
        loader.RegisterFunction(std::move(wmin_set));
    }
    // wmax: tint, tfloat
    {
        AggregateFunctionSet wmax_set("wmax");
        wmax_set.AddFunction(
            MakeWindowAggregate<&tint_wmax_transfn, &mduck_datum_max_int32, false>(
                TemporalTypes::TINT(), TemporalTypes::TINT()));
        wmax_set.AddFunction(
            MakeWindowAggregate<&tfloat_wmax_transfn, &mduck_datum_max_float8, true>(
                TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()));
        loader.RegisterFunction(std::move(wmax_set));
    }
    // wsum: tint, tfloat
    {
        AggregateFunctionSet wsum_set("wsum");
        wsum_set.AddFunction(
            MakeWindowAggregate<&tint_wsum_transfn, &mduck_datum_sum_int32, false>(
                TemporalTypes::TINT(), TemporalTypes::TINT()));
        wsum_set.AddFunction(
            MakeWindowAggregate<&tfloat_wsum_transfn, &mduck_datum_sum_float8, true>(
                TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()));
        loader.RegisterFunction(std::move(wsum_set));
    }
    // wavg: tnumber (output is tfloat).
    {
        AggregateFunctionSet wavg_set("wavg");
        wavg_set.AddFunction(MakeWavgAggregate<&tnumber_wavg_transfn>(TemporalTypes::TINT()));
        wavg_set.AddFunction(MakeWavgAggregate<&tnumber_wavg_transfn>(TemporalTypes::TFLOAT()));
        loader.RegisterFunction(std::move(wavg_set));
    }
}

void TemporalAggregates::RegisterTAvg(ExtensionLoader &loader) {
    AggregateFunctionSet tavg_set("tAvg");
    tavg_set.AddFunction(MakeTavgAggregate(TemporalTypes::TINT()));
    tavg_set.AddFunction(MakeTavgAggregate(TemporalTypes::TFLOAT()));
    loader.RegisterFunction(std::move(tavg_set));
}

} // namespace duckdb
