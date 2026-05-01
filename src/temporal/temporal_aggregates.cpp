// SkipList-state temporal aggregates: tcount, tand, tor, tmin, tmax, tsum,
// tavg, tcentroid. These differ from the fixed-state `extent()` aggregates
// in two ways:
//
//   1. State holds a `SkipList *` pointer; the skiplist owns variable-size
//      heap-allocated Temporal* values, so the aggregate needs a destructor.
//
//   2. Combine merges two skiplists via MEOS's `temporal_tagg_combinefn`.
//      That function is exported from libmeos but not declared in the
//      installed public headers, so we forward-declare it here. Same goes
//      for the `datum_*` per-aggregate merge functors.
//
// The MEOS combine semantics share Temporal* pointers between the two input
// skiplists once both are non-empty: the returned skiplist holds the merged
// elements while the "loser" skiplist still references the same pointers.
// We can't call the standard `skiplist_free` on the loser without double-
// freeing, so after combine we walk the loser's elements and NULL out their
// keys/values before calling `skiplist_free` (which then only releases the
// SkipList struct + freed[] array + elem array).

#include "meos_wrapper_simple.hpp"

#include "common.hpp"
#include "temporal/temporal.hpp"
#include "temporal/temporal_aggregates.hpp"
#include "geo/tgeompoint.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/function/aggregate_function.hpp"
#include "duckdb/function/function_set.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

extern "C" {
// Per-aggregate merge functions (exported from libmeos.a but not in the
// public headers).
Datum datum_and(Datum, Datum);
Datum datum_or(Datum, Datum);
Datum datum_min_int32(Datum, Datum);
Datum datum_max_int32(Datum, Datum);
Datum datum_sum_int32(Datum, Datum);
Datum datum_min_float8(Datum, Datum);
Datum datum_max_float8(Datum, Datum);
Datum datum_sum_float8(Datum, Datum);
Datum datum_min_text(Datum, Datum);
Datum datum_max_text(Datum, Datum);
Datum datum_sum_double2(Datum, Datum);
Datum datum_sum_double3(Datum, Datum);
Datum datum_sum_double4(Datum, Datum);

// Forward-decl: same signature as in meos/include/temporal/temporal_aggfuncs.h.
SkipList *temporal_tagg_combinefn(SkipList *state1, SkipList *state2,
                                  datum_func2 func, bool crossings);
}

namespace {

// Mirrors the elem-walk in MEOS skiplist_free, nulling out keys/values so
// the subsequent skiplist_free only releases the structural memory.
void NullSkiplistElements(SkipList *list) {
    if (!list || !list->elems) return;
    int cur = 0;
    while (cur != -1) {
        SkipListElem *elem = &list->elems[cur];
        elem->key = nullptr;
        elem->value = nullptr;
        cur = elem->next[0];
    }
}

void SafeFreeLoserSkiplist(SkipList *result, SkipList *a, SkipList *b) {
    SkipList *loser = (result == a) ? b : a;
    if (loser) {
        NullSkiplistElements(loser);
        skiplist_free(loser);
    }
}

// Aggregate state — one pointer.
struct SkipListState {
    SkipList *skiplist;
};

// Generic SkipList aggregate over Temporal inputs.
//
//   TRANSFN     — MEOS transition function (state, blob_decoded_temporal).
//   FINALFN     — MEOS finalize function (state -> Temporal *).
//   MERGE_FN    — datum_func2 used by combinefn.
//   CROSSINGS   — pass to combinefn (true for tfloat min/max).
template <SkipList *(*TRANSFN)(SkipList *, const Temporal *),
          Temporal *(*FINALFN)(SkipList *),
          Datum (*MERGE_FN)(Datum, Datum),
          bool CROSSINGS = false>
struct TemporalSkipListAggFn {
    template <class STATE>
    static void Initialize(STATE &state) {
        state.skiplist = nullptr;
    }
    static bool IgnoreNull() { return true; }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        // Decode input blob to Temporal *. The MEOS transfn copies the
        // value into the skiplist, so the transient blob memory is fine.
        const Temporal *t = reinterpret_cast<const Temporal *>(input.GetData());
        state.skiplist = TRANSFN(state.skiplist, t);
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &u, idx_t) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, u);
    }

    template <class STATE, class OP>
    static void Combine(const STATE &source_const, STATE &target, AggregateInputData &) {
        STATE &source = const_cast<STATE &>(source_const);
        if (!source.skiplist) return;
        if (!target.skiplist) {
            target.skiplist = source.skiplist;
            source.skiplist = nullptr;
            return;
        }
        SkipList *result = temporal_tagg_combinefn(target.skiplist, source.skiplist,
                                                   MERGE_FN, CROSSINGS);
        SafeFreeLoserSkiplist(result, target.skiplist, source.skiplist);
        target.skiplist = result;
        source.skiplist = nullptr;
    }

    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &fd) {
        if (!state.skiplist) {
            fd.ReturnNull();
            return;
        }
        Temporal *result = FINALFN(state.skiplist);
        // MEOS finalfns release the skiplist themselves on the success
        // path; on the empty-skiplist path they leave it intact and
        // return NULL. Either way, we can no longer safely call
        // skiplist_free on this pointer.
        SkipList *raw = state.skiplist;
        state.skiplist = nullptr;
        if (!result) {
            // Empty input → skiplist still allocated; release it now.
            skiplist_free(raw);
            fd.ReturnNull();
            return;
        }
        size_t size = temporal_mem_size(result);
        target = fd.ReturnString(string_t(reinterpret_cast<const char *>(result), size));
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

// Macro to declare a concrete aggregate type with a fixed return-type alias.
#define DECLARE_TAGG(NAME, TRANSFN, FINALFN, MERGE_FN, CROSSINGS) \
    using NAME = TemporalSkipListAggFn<TRANSFN, FINALFN, MERGE_FN, CROSSINGS>;

DECLARE_TAGG(TandFn,         tbool_tand_transfn,        temporal_tagg_finalfn,  datum_and,           false)
DECLARE_TAGG(TorFn,          tbool_tor_transfn,         temporal_tagg_finalfn,  datum_or,            false)
DECLARE_TAGG(TcountTempFn,   temporal_tcount_transfn,   temporal_tagg_finalfn,  datum_sum_int32,     false)
DECLARE_TAGG(TminTintFn,     tint_tmin_transfn,         temporal_tagg_finalfn,  datum_min_int32,     false)
DECLARE_TAGG(TmaxTintFn,     tint_tmax_transfn,         temporal_tagg_finalfn,  datum_max_int32,     false)
DECLARE_TAGG(TsumTintFn,     tint_tsum_transfn,         temporal_tagg_finalfn,  datum_sum_int32,     false)
DECLARE_TAGG(TminTfloatFn,   tfloat_tmin_transfn,       temporal_tagg_finalfn,  datum_min_float8,    true)
DECLARE_TAGG(TmaxTfloatFn,   tfloat_tmax_transfn,       temporal_tagg_finalfn,  datum_max_float8,    true)
DECLARE_TAGG(TsumTfloatFn,   tfloat_tsum_transfn,       temporal_tagg_finalfn,  datum_sum_float8,    false)
DECLARE_TAGG(TminTtextFn,    ttext_tmin_transfn,        temporal_tagg_finalfn,  datum_min_text,      false)
DECLARE_TAGG(TmaxTtextFn,    ttext_tmax_transfn,        temporal_tagg_finalfn,  datum_max_text,      false)
DECLARE_TAGG(TavgFn,         tnumber_tavg_transfn,      tnumber_tavg_finalfn,   datum_sum_double2,   false)

// tcentroid: chooses sum_double3 (2D) vs sum_double4 (3D) based on input
// dimension. We forward to the same templated functor via a runtime-chosen
// merge func by branching at Combine time. To keep the templates simple we
// use a small wrapper that overrides Combine.
struct TcentroidFn {
    template <class STATE>
    static void Initialize(STATE &state) { state.skiplist = nullptr; }
    static bool IgnoreNull() { return true; }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        // tpoint_tcentroid_transfn takes Temporal * (non-const), and copies.
        Temporal *t = const_cast<Temporal *>(reinterpret_cast<const Temporal *>(input.GetData()));
        state.skiplist = tpoint_tcentroid_transfn(state.skiplist, t);
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &u, idx_t) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, u);
    }
    template <class STATE, class OP>
    static void Combine(const STATE &source_const, STATE &target, AggregateInputData &) {
        STATE &source = const_cast<STATE &>(source_const);
        if (!source.skiplist) return;
        if (!target.skiplist) {
            target.skiplist = source.skiplist;
            source.skiplist = nullptr;
            return;
        }
        // Choose merge functor based on dimensionality stored in the
        // skiplist's `extra` (a GeoAggregateState). Bit 0 of the first
        // byte after the SkipList header is `hasz` per MEOS layout.
        // Falling back to double3 (2D) if extra is null.
        datum_func2 func = &datum_sum_double3;
        const auto *extra = (target.skiplist && target.skiplist->extra)
                              ? target.skiplist->extra
                              : (source.skiplist ? source.skiplist->extra : nullptr);
        if (extra) {
            // GeoAggregateState begins with `bool hasz;` — first byte.
            if (*static_cast<const bool *>(extra)) {
                func = &datum_sum_double4;
            }
        }
        SkipList *result = temporal_tagg_combinefn(target.skiplist, source.skiplist, func, false);
        SafeFreeLoserSkiplist(result, target.skiplist, source.skiplist);
        target.skiplist = result;
        source.skiplist = nullptr;
    }
    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &fd) {
        if (!state.skiplist) { fd.ReturnNull(); return; }
        Temporal *result = tpoint_tcentroid_finalfn(state.skiplist);
        SkipList *raw = state.skiplist;
        state.skiplist = nullptr;
        if (!result) {
            skiplist_free(raw);
            fd.ReturnNull();
            return;
        }
        size_t size = temporal_mem_size(result);
        target = fd.ReturnString(string_t(reinterpret_cast<const char *>(result), size));
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

template <class OP>
static AggregateFunction MakeTaggAggregate(const LogicalType &input_type, const LogicalType &return_type) {
    return AggregateFunction::UnaryAggregateDestructor<
        SkipListState, string_t, string_t, OP, AggregateDestructorType::LEGACY>(input_type, return_type);
}

} // namespace

void TemporalAggregates::RegisterAggregateFunctions(ExtensionLoader &loader) {
    // ---- tand / tor on tbool ----
    {
        AggregateFunctionSet set("tand");
        set.AddFunction(MakeTaggAggregate<TandFn>(TemporalTypes::TBOOL(), TemporalTypes::TBOOL()));
        loader.RegisterFunction(std::move(set));
    }
    {
        AggregateFunctionSet set("tor");
        set.AddFunction(MakeTaggAggregate<TorFn>(TemporalTypes::TBOOL(), TemporalTypes::TBOOL()));
        loader.RegisterFunction(std::move(set));
    }

    // ---- tcount over each temporal type → tint ----
    {
        AggregateFunctionSet set("tcount");
        for (const auto &t : {TemporalTypes::TBOOL(), TemporalTypes::TINT(),
                              TemporalTypes::TFLOAT(), TemporalTypes::TTEXT()}) {
            set.AddFunction(MakeTaggAggregate<TcountTempFn>(t, TemporalTypes::TINT()));
        }
        set.AddFunction(MakeTaggAggregate<TcountTempFn>(TgeompointType::TGEOMPOINT(), TemporalTypes::TINT()));
        loader.RegisterFunction(std::move(set));
    }

    // ---- tmin / tmax — DEFERRED ----
    //
    // The MobilityDB SQL surface has two semantically-different functions
    // sharing a case-insensitive name: scalar `Tmin(tbox|stbox) -> timestamptz`
    // (already registered in tbox.cpp / stbox.cpp) and aggregate
    // `tMin(tint|tfloat|ttext) -> Temporal`. PostgreSQL dispatches by the
    // argument-types tuple, but DuckDB's catalog folds both onto a single
    // canonical lowercase name `tmin` and rejects mixing scalar + aggregate
    // overloads on the same name (catalog raises "GetAlterInfo not
    // implemented for this type"). Resolving this requires either renaming
    // the existing box scalars or registering the aggregate under a
    // disambiguated name. Out of scope for this PR.

    // ---- tsum on tint, tfloat ----
    {
        AggregateFunctionSet set("tsum");
        set.AddFunction(MakeTaggAggregate<TsumTintFn>(TemporalTypes::TINT(),    TemporalTypes::TINT()));
        set.AddFunction(MakeTaggAggregate<TsumTfloatFn>(TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()));
        loader.RegisterFunction(std::move(set));
    }

    // ---- tavg on tint, tfloat → tfloat ----
    {
        AggregateFunctionSet set("tavg");
        set.AddFunction(MakeTaggAggregate<TavgFn>(TemporalTypes::TINT(),  TemporalTypes::TFLOAT()));
        set.AddFunction(MakeTaggAggregate<TavgFn>(TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()));
        loader.RegisterFunction(std::move(set));
    }

    // ---- tcentroid on tgeompoint → tgeompoint ----
    {
        AggregateFunctionSet set("tcentroid");
        set.AddFunction(MakeTaggAggregate<TcentroidFn>(
            TgeompointType::TGEOMPOINT(), TgeompointType::TGEOMPOINT()));
        loader.RegisterFunction(std::move(set));
    }
}

} // namespace duckdb
