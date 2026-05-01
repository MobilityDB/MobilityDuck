// SkipList-state temporal aggregates: TcountAgg, TandAgg, TorAgg, TminAgg,
// TmaxAgg, TsumAgg, TavgAgg, TcentroidAgg. Names follow MobilityDB RFC #827:
// every SkipList aggregate is exposed under a Pascal-cased *Agg identifier,
// disambiguating it from the same-stem scalar accessors (Tmin(tbox), etc.)
// in case-folding catalogs like DuckDB. These differ from the fixed-state
// `extent()` aggregates in two ways:
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
#include "temporal/span.hpp"
#include "temporal/spanset.hpp"
#include "temporal/set.hpp"
#include "time_util.hpp"
#include "geo/tgeompoint.hpp"
#include "geo/tgeometry.hpp"
#include "geo/tgeography.hpp"
#include "geo/tgeogpoint.hpp"
#include "geo/geoset.hpp"
#include "geo_util.hpp"
#include "spatial/spatial_types.hpp"

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
SkipList *temporal_tagg_transfn(SkipList *state, const Temporal *temp,
                                datum_func2 func, bool crossings);

// Window-aggregate helpers — same signatures as in
// meos/include/temporal/temporal_waggfuncs.h. (TSequence is already declared
// by the MEOS public headers; we just refer to it.)
TSequence **temporal_transform_wcount(const Temporal *temp,
                                      const ::Interval *interval, int *count);
SkipList *temporal_wagg_transform_transfn(SkipList *state, const Temporal *temp,
                                          const ::Interval *interval, datum_func2 func,
                                          TSequence ** (*transform)(const Temporal *, const ::Interval *, int *));
}

namespace {

// Mirror of `struct GeoAggregateState` from MEOS's tgeo_aggfuncs.h. Used
// only to peek at the `hasz` flag carried in `SkipList::extra` so we can
// pick the correct datum_sum functor at combine time.
struct GeoAggregateStateLayout {
    int32_t srid;
    bool hasz;
};

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

// TcountAgg variants over time-only inputs use distinct transfn signatures.
// The blob-shaped ones (set / span / spanset) all funnel through a common
// blob-casting wrapper; the scalar timestamptz one needs its own functor
// since the input isn't a string_t blob.

inline SkipList *TcountTstzsetTransfn(SkipList *state, const Temporal *blob) {
    return tstzset_tcount_transfn(state, reinterpret_cast<const Set *>(blob));
}
inline SkipList *TcountTstzspanTransfn(SkipList *state, const Temporal *blob) {
    return tstzspan_tcount_transfn(state, reinterpret_cast<const Span *>(blob));
}
inline SkipList *TcountTstzspansetTransfn(SkipList *state, const Temporal *blob) {
    return tstzspanset_tcount_transfn(state, reinterpret_cast<const SpanSet *>(blob));
}

DECLARE_TAGG(TandFn,             tbool_tand_transfn,         temporal_tagg_finalfn,  datum_and,        false)
DECLARE_TAGG(TorFn,              tbool_tor_transfn,          temporal_tagg_finalfn,  datum_or,         false)
DECLARE_TAGG(TcountTempFn,       temporal_tcount_transfn,    temporal_tagg_finalfn,  datum_sum_int32,  false)
DECLARE_TAGG(TcountTstzsetFn,    TcountTstzsetTransfn,       temporal_tagg_finalfn,  datum_sum_int32,  false)
DECLARE_TAGG(TcountTstzspanFn,   TcountTstzspanTransfn,      temporal_tagg_finalfn,  datum_sum_int32,  false)
DECLARE_TAGG(TcountTstzspansetFn, TcountTstzspansetTransfn,  temporal_tagg_finalfn,  datum_sum_int32,  false)

// TcountAgg(timestamptz) — input is a scalar TimestampTz, not a blob.
struct TcountTstzFn {
    template <class STATE>
    static void Initialize(STATE &state) { state.skiplist = nullptr; }
    static bool IgnoreNull() { return true; }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        timestamp_tz_t meos_t = DuckDBToMeosTimestamp(input);
        state.skiplist = timestamptz_tcount_transfn(state.skiplist, (TimestampTz) meos_t.value);
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
                                                   &datum_sum_int32, /*crossings=*/false);
        SafeFreeLoserSkiplist(result, target.skiplist, source.skiplist);
        target.skiplist = result;
        source.skiplist = nullptr;
    }
    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &fd) {
        if (!state.skiplist) { fd.ReturnNull(); return; }
        Temporal *result = temporal_tagg_finalfn(state.skiplist);
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
        // skiplist's `extra` (a GeoAggregateState laid out as
        // {int32 srid; bool hasz;}). Falling back to double3 (2D) if
        // extra is null.
        datum_func2 func = &datum_sum_double3;
        const auto *extra = (target.skiplist && target.skiplist->extra)
                              ? target.skiplist->extra
                              : (source.skiplist ? source.skiplist->extra : nullptr);
        if (extra) {
            const auto *gs = static_cast<const GeoAggregateStateLayout *>(extra);
            if (gs->hasz) {
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

// =====================================================================
// Window aggregates (W*Agg): binary aggregate over (Temporal, Interval).
// State = SkipList *, transfn takes the extra Interval, combinefn is the
// same as for the non-windowed variant.
// =====================================================================

template <
    SkipList *(*TRANSFN)(SkipList *, const Temporal *, const ::Interval *),
    Temporal *(*FINALFN)(SkipList *),
    Datum (*MERGE_FN)(Datum, Datum),
    bool CROSSINGS = false>
struct WindowAggFn {
    template <class STATE>
    static void Initialize(STATE &state) { state.skiplist = nullptr; }
    static bool IgnoreNull() { return true; }

    template <class A_TYPE, class B_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const A_TYPE &input, const B_TYPE &interv,
                          AggregateBinaryInput &) {
        const Temporal *t = reinterpret_cast<const Temporal *>(input.GetData());
        MeosInterval iv = IntervaltToInterval(interv);
        state.skiplist = TRANSFN(state.skiplist, t, &iv);
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
        if (!state.skiplist) { fd.ReturnNull(); return; }
        Temporal *result = FINALFN(state.skiplist);
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

// WcountAgg: input is transformed through `temporal_transform_wcount`
// before being aggregated with `datum_sum_int32`. The result is always
// a tint regardless of input subtype.
struct WcountAggFn {
    template <class STATE>
    static void Initialize(STATE &state) { state.skiplist = nullptr; }
    static bool IgnoreNull() { return true; }

    template <class A_TYPE, class B_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const A_TYPE &input, const B_TYPE &interv,
                          AggregateBinaryInput &) {
        const Temporal *t = reinterpret_cast<const Temporal *>(input.GetData());
        MeosInterval iv = IntervaltToInterval(interv);
        state.skiplist = temporal_wagg_transform_transfn(
            state.skiplist, t, &iv, &datum_sum_int32, &temporal_transform_wcount);
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
                                                   &datum_sum_int32, /*crossings=*/false);
        SafeFreeLoserSkiplist(result, target.skiplist, source.skiplist);
        target.skiplist = result;
        source.skiplist = nullptr;
    }
    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &fd) {
        if (!state.skiplist) { fd.ReturnNull(); return; }
        Temporal *result = temporal_tagg_finalfn(state.skiplist);
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

// Concrete window aggregates.
using WminTintFn   = WindowAggFn<tint_wmin_transfn,   temporal_tagg_finalfn, datum_min_int32,  false>;
using WmaxTintFn   = WindowAggFn<tint_wmax_transfn,   temporal_tagg_finalfn, datum_max_int32,  false>;
using WsumTintFn   = WindowAggFn<tint_wsum_transfn,   temporal_tagg_finalfn, datum_sum_int32,  false>;
using WminTfloatFn = WindowAggFn<tfloat_wmin_transfn, temporal_tagg_finalfn, datum_min_float8, true>;
using WmaxTfloatFn = WindowAggFn<tfloat_wmax_transfn, temporal_tagg_finalfn, datum_max_float8, true>;
using WsumTfloatFn = WindowAggFn<tfloat_wsum_transfn, temporal_tagg_finalfn, datum_sum_float8, false>;
using WavgFn       = WindowAggFn<tnumber_wavg_transfn, tnumber_tavg_finalfn,  datum_sum_double2, false>;

template <class OP>
static AggregateFunction MakeWindowAggregate(const LogicalType &input_type, const LogicalType &return_type) {
    auto fn = AggregateFunction::BinaryAggregate<
        SkipListState, string_t, interval_t, string_t, OP, AggregateDestructorType::LEGACY>(
        input_type, LogicalType::INTERVAL, return_type);
    fn.destructor = AggregateFunction::StateDestroy<SkipListState, OP>;
    return fn;
}

// =====================================================================
// MergeAgg: same SkipList state, but with no per-instant merge functor.
// MEOS treats `func == NULL` as "concatenate values without conflict
// resolution"; the resulting Temporal is the merged sequence/sequenceset.
// =====================================================================

inline SkipList *MergeTransfn(SkipList *state, const Temporal *temp) {
    return temporal_tagg_transfn(state, temp, /*func=*/nullptr, /*crossings=*/false);
}

struct MergeAggFn {
    template <class STATE>
    static void Initialize(STATE &state) { state.skiplist = nullptr; }
    static bool IgnoreNull() { return true; }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        const Temporal *t = reinterpret_cast<const Temporal *>(input.GetData());
        state.skiplist = MergeTransfn(state.skiplist, t);
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
                                                   /*func=*/nullptr, /*crossings=*/false);
        SafeFreeLoserSkiplist(result, target.skiplist, source.skiplist);
        target.skiplist = result;
        source.skiplist = nullptr;
    }
    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &fd) {
        if (!state.skiplist) { fd.ReturnNull(); return; }
        Temporal *result = temporal_tagg_finalfn(state.skiplist);
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

// =====================================================================
// AppendInstantAgg / AppendSequenceAgg: state shape is `Temporal *`
// (not a SkipList). The MEOS transfn mutates the state Temporal in
// place and returns either the same pointer or a freshly-allocated
// successor. Combine merges two states via the public `temporal_merge`.
// =====================================================================

struct TemporalState {
    Temporal *value;
};

template <Temporal *(*COMBINE_MERGE)(const Temporal *, const Temporal *) = temporal_merge>
struct TemporalStateCombineBase {
    template <class STATE, class OP>
    static void Combine(const STATE &source_const, STATE &target, AggregateInputData &) {
        STATE &source = const_cast<STATE &>(source_const);
        if (!source.value) return;
        if (!target.value) {
            target.value = source.value;
            source.value = nullptr;
            return;
        }
        Temporal *merged = COMBINE_MERGE(target.value, source.value);
        free(target.value);
        free(source.value);
        target.value = merged;
        source.value = nullptr;
    }
    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &fd) {
        if (!state.value) { fd.ReturnNull(); return; }
        Temporal *result = temporal_compact(state.value);
        free(state.value);
        state.value = nullptr;
        if (!result) { fd.ReturnNull(); return; }
        size_t size = temporal_mem_size(result);
        target = fd.ReturnString(string_t(reinterpret_cast<const char *>(result), size));
        free(result);
    }
    template <class STATE>
    static void Destroy(STATE &state, AggregateInputData &) {
        if (state.value) {
            free(state.value);
            state.value = nullptr;
        }
    }
};

// AppendInstantAgg(temporal) — uses default LINEAR-or-discrete interpretation
// inferred from the temporal subtype, no maxdist / maxt thresholds.
struct AppendInstantAggFn : TemporalStateCombineBase<> {
    template <class STATE>
    static void Initialize(STATE &state) { state.value = nullptr; }
    static bool IgnoreNull() { return true; }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        const Temporal *t = reinterpret_cast<const Temporal *>(input.GetData());
        // The MEOS transfn expects a TInstant *; an aggregate input that
        // is already a sequence falls through to AppendSequenceAgg.
        const TInstant *inst = reinterpret_cast<const TInstant *>(t);
        // INTERP_NONE lets MEOS pick the natural interpretation for the
        // base type (DISCRETE for tbool/ttext, LINEAR for tnumber/tgeo).
        state.value = temporal_app_tinst_transfn(
            state.value, inst, INTERP_NONE, /*maxdist=*/0.0, /*maxt=*/nullptr);
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &u, idx_t) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, u);
    }
};

// AppendSequenceAgg(temporal) — input is already a TSequence.
struct AppendSequenceAggFn : TemporalStateCombineBase<> {
    template <class STATE>
    static void Initialize(STATE &state) { state.value = nullptr; }
    static bool IgnoreNull() { return true; }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        const Temporal *t = reinterpret_cast<const Temporal *>(input.GetData());
        const TSequence *seq = reinterpret_cast<const TSequence *>(t);
        state.value = temporal_app_tseq_transfn(state.value, seq);
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &u, idx_t) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, u);
    }
};

template <class OP>
static AggregateFunction MakeTemporalStateAggregate(const LogicalType &type) {
    return AggregateFunction::UnaryAggregateDestructor<
        TemporalState, string_t, string_t, OP, AggregateDestructorType::LEGACY>(type, type);
}

// =====================================================================
// SpanUnionAgg: state shape is `SpanSet *`. Each input span/spanset is
// folded into the state via MEOS's union transfns. Final state is the
// state itself (no separate finalfn — `spanset_union_finalfn` is a
// passthrough at this point).
// =====================================================================

struct SpanSetState {
    SpanSet *value;
};

template <class STATE>
inline void SpanSetCombine(STATE &source, STATE &target) {
    if (!source.value) return;
    if (!target.value) {
        target.value = source.value;
        source.value = nullptr;
        return;
    }
    // spanset_union_transfn either mutates target.value in place (returns
    // the same pointer) or frees it and returns a fresh allocation; in
    // both cases the old target should not be freed by us. The source is
    // a const input — we own it independently and must free it.
    target.value = spanset_union_transfn(target.value, source.value);
    free(source.value);
    source.value = nullptr;
}

template <class T, class STATE>
inline void SpanSetFinalize(STATE &state, T &target, AggregateFinalizeData &fd) {
    if (!state.value) { fd.ReturnNull(); return; }
    // spanset_union_finalfn frees the state internally on the success
    // path, so clear the pointer first to prevent Destroy from
    // double-freeing if anything below throws.
    SpanSet *result = spanset_union_finalfn(state.value);
    state.value = nullptr;
    if (!result) { fd.ReturnNull(); return; }
    size_t size = (size_t) spanset_mem_size(result);
    target = fd.ReturnString(string_t(reinterpret_cast<const char *>(result), size));
    free(result);
}

template <class STATE>
inline void SpanSetDestroy(STATE &state) {
    if (state.value) {
        free(state.value);
        state.value = nullptr;
    }
}

// SpanUnionAgg(<typed-span>) — input is a Span blob.
struct SpanUnionFromSpanFn {
    template <class STATE>
    static void Initialize(STATE &state) { state.value = nullptr; }
    static bool IgnoreNull() { return true; }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        const Span *s = reinterpret_cast<const Span *>(input.GetData());
        state.value = span_union_transfn(state.value, s);
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &u, idx_t) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, u);
    }
    template <class STATE, class OP>
    static void Combine(const STATE &source_const, STATE &target, AggregateInputData &) {
        SpanSetCombine(const_cast<STATE &>(source_const), target);
    }
    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &fd) {
        SpanSetFinalize(state, target, fd);
    }
    template <class STATE>
    static void Destroy(STATE &state, AggregateInputData &) { SpanSetDestroy(state); }
};

// SpanUnionAgg(<typed-spanset>) — input is a SpanSet blob.
struct SpanUnionFromSpanSetFn {
    template <class STATE>
    static void Initialize(STATE &state) { state.value = nullptr; }
    static bool IgnoreNull() { return true; }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        const SpanSet *ss = reinterpret_cast<const SpanSet *>(input.GetData());
        state.value = spanset_union_transfn(state.value, ss);
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &u, idx_t) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, u);
    }
    template <class STATE, class OP>
    static void Combine(const STATE &source_const, STATE &target, AggregateInputData &) {
        SpanSetCombine(const_cast<STATE &>(source_const), target);
    }
    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &fd) {
        SpanSetFinalize(state, target, fd);
    }
    template <class STATE>
    static void Destroy(STATE &state, AggregateInputData &) { SpanSetDestroy(state); }
};

template <class OP>
static AggregateFunction MakeSpanSetAggregate(const LogicalType &input_type, const LogicalType &return_type) {
    return AggregateFunction::UnaryAggregateDestructor<
        SpanSetState, string_t, string_t, OP, AggregateDestructorType::LEGACY>(input_type, return_type);
}

// =====================================================================
// SetUnionAgg: state shape is `Set *`. Inputs come in two flavours —
// scalars (folded via `value_union_transfn`) and sets (folded via
// `set_union_transfn`).
// =====================================================================

struct SetAggState {
    Set *value;
};

template <class STATE>
inline void SetCombine(STATE &source, STATE &target) {
    if (!source.value) return;
    if (!target.value) {
        target.value = source.value;
        source.value = nullptr;
        return;
    }
    // Same ownership story as SpanSetCombine: transfn handles target;
    // source is a separate allocation we own.
    target.value = set_union_transfn(target.value, source.value);
    free(source.value);
    source.value = nullptr;
}

template <class T, class STATE>
inline void SetFinalize(STATE &state, T &target, AggregateFinalizeData &fd) {
    if (!state.value) { fd.ReturnNull(); return; }
    // set_union_finalfn frees the state internally on the success path.
    Set *result = set_union_finalfn(state.value);
    state.value = nullptr;
    if (!result) { fd.ReturnNull(); return; }
    size_t size = (size_t) set_mem_size(result);
    target = fd.ReturnString(string_t(reinterpret_cast<const char *>(result), size));
    free(result);
}

template <class STATE>
inline void SetDestroy(STATE &state) {
    if (state.value) {
        free(state.value);
        state.value = nullptr;
    }
}

// SetUnionAgg(<typed-set>) — input is a Set blob.
struct SetUnionFromSetFn {
    template <class STATE>
    static void Initialize(STATE &state) { state.value = nullptr; }
    static bool IgnoreNull() { return true; }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        Set *s = const_cast<Set *>(reinterpret_cast<const Set *>(input.GetData()));
        state.value = set_union_transfn(state.value, s);
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &u, idx_t) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, u);
    }
    template <class STATE, class OP>
    static void Combine(const STATE &source_const, STATE &target, AggregateInputData &) {
        SetCombine(const_cast<STATE &>(source_const), target);
    }
    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &fd) {
        SetFinalize(state, target, fd);
    }
    template <class STATE>
    static void Destroy(STATE &state, AggregateInputData &) { SetDestroy(state); }
};

// SetUnionAgg(<scalar>) — input is a primitive value lifted to a Datum.
template <class IN, Datum (*TO_DATUM)(IN), meosType BASETYPE>
struct SetUnionFromScalarFn {
    template <class STATE>
    static void Initialize(STATE &state) { state.value = nullptr; }
    static bool IgnoreNull() { return true; }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        state.value = value_union_transfn(state.value, TO_DATUM(input), BASETYPE);
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &u, idx_t) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, u);
    }
    template <class STATE, class OP>
    static void Combine(const STATE &source_const, STATE &target, AggregateInputData &) {
        SetCombine(const_cast<STATE &>(source_const), target);
    }
    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &fd) {
        SetFinalize(state, target, fd);
    }
    template <class STATE>
    static void Destroy(STATE &state, AggregateInputData &) { SetDestroy(state); }
};

// Datum casting helpers — MobilityDuck doesn't pull in PG's `postgres.h`,
// so we cast explicitly. For floats this is a bit-pattern reinterpret; for
// integer / date / tstz a sign-extending widening to int64 covers both 64-
// and 32-bit Datum builds.
inline Datum Int32ToDatum(int32_t v) { return (Datum) (int64_t) v; }
inline Datum Int64ToDatum(int64_t v) { return (Datum) v; }
inline Datum Float8ToDatum(double v) {
    // Datum is `uintptr_t`. On native 64-bit builds the double fits by-value
    // (bit-pattern reinterpret); on 32-bit builds (e.g. wasm32-emscripten)
    // the Datum is 4 bytes, so we have to pass a pointer the way PG's
    // Float8GetDatum does under !USE_FLOAT8_BYVAL. The pointed-at storage
    // becomes part of the MEOS Set on insert and is freed by set_free.
    if (sizeof(Datum) >= sizeof(double)) {
        Datum d = 0;
        memcpy(&d, &v, sizeof(double));
        return d;
    }
    double *p = (double *) malloc(sizeof(double));
    *p = v;
    return (Datum) (uintptr_t) p;
}
inline Datum DateToDatum(date_t v) { return (Datum) (int64_t) ToMeosDate(v); }
inline Datum TstzToDatum(timestamp_tz_t v) {
    return (Datum) (int64_t) DuckDBToMeosTimestamp(v).value;
}
// Text Datum: MEOS' set machinery copies the pointed-at text struct
// into the Set on insert, so we can free the cstring2text-allocated
// buffer right after the call (handled inside the Operation wrapper).
inline Datum TextToDatum(string_t v) {
    text *t = cstring2text(v.GetString().c_str());
    return (Datum) (uintptr_t) t;
}

// SetUnionAgg(geometry|geography) — uses geo_union_transfn directly,
// which takes a GSERIALIZED rather than a Datum. Mirrors
// SetUnionFromScalarFn's lifecycle methods.
struct SetUnionFromGeoFn {
    template <class STATE>
    static void Initialize(STATE &state) { state.value = nullptr; }
    static bool IgnoreNull() { return true; }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        // SRID is irrelevant for set membership — we accept whatever
        // the input carries.
        GSERIALIZED *gs = GeometryToGSerialized(input, 0);
        if (!gs) return;
        state.value = geo_union_transfn(state.value, gs);
        free(gs);
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &u, idx_t) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, u);
    }
    template <class STATE, class OP>
    static void Combine(const STATE &source_const, STATE &target, AggregateInputData &) {
        SetCombine(const_cast<STATE &>(source_const), target);
    }
    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &fd) {
        SetFinalize(state, target, fd);
    }
    template <class STATE>
    static void Destroy(STATE &state, AggregateInputData &) { SetDestroy(state); }
};

// SetUnionAgg(text) — TextToDatum allocates a `text *` per input
// element. MEOS' set_make copies the pointed-at value when adding to
// the Set, so freeing right after the transfn returns is safe.
struct SetUnionFromTextFn {
    template <class STATE>
    static void Initialize(STATE &state) { state.value = nullptr; }
    static bool IgnoreNull() { return true; }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        text *t = cstring2text(input.GetString().c_str());
        state.value = value_union_transfn(state.value, (Datum)(uintptr_t)t, T_TEXT);
        free(t);
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &u, idx_t) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, u);
    }
    template <class STATE, class OP>
    static void Combine(const STATE &source_const, STATE &target, AggregateInputData &) {
        SetCombine(const_cast<STATE &>(source_const), target);
    }
    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &fd) {
        SetFinalize(state, target, fd);
    }
    template <class STATE>
    static void Destroy(STATE &state, AggregateInputData &) { SetDestroy(state); }
};

template <class OP>
static AggregateFunction MakeSetAggregate(const LogicalType &input_type, const LogicalType &return_type) {
    return AggregateFunction::UnaryAggregateDestructor<
        SetAggState, string_t, string_t, OP, AggregateDestructorType::LEGACY>(input_type, return_type);
}

template <class IN, Datum (*TO_DATUM)(IN), meosType BASETYPE>
static AggregateFunction MakeSetUnionScalarAggregate(const LogicalType &input_type,
                                                     const LogicalType &return_type) {
    return AggregateFunction::UnaryAggregateDestructor<
        SetAggState, IN, string_t,
        SetUnionFromScalarFn<IN, TO_DATUM, BASETYPE>,
        AggregateDestructorType::LEGACY>(input_type, return_type);
}

} // namespace

void TemporalAggregates::RegisterAggregateFunctions(ExtensionLoader &loader) {
    // SkipList aggregates are registered under the Pascal-cased *Agg names
    // proposed in MobilityDB RFC #827. These avoid the case-folding
    // collision DuckDB's catalog hits when an aggregate and a scalar share
    // a canonical lowercase name (e.g. previously `tmin` aggregate vs the
    // existing `Tmin(tbox)` scalar). The MobilityDB upstream PR #828 adds
    // the matching aliases on the PG side; the original aggregate names
    // (`tand`, `tcount`, etc.) remain valid in PG for backward compat but
    // collide in DuckDB and are intentionally not registered here.

    // ---- TandAgg / TorAgg on tbool ----
    {
        AggregateFunctionSet set("TandAgg");
        set.AddFunction(MakeTaggAggregate<TandFn>(TemporalTypes::TBOOL(), TemporalTypes::TBOOL()));
        loader.RegisterFunction(std::move(set));
    }
    {
        AggregateFunctionSet set("TorAgg");
        set.AddFunction(MakeTaggAggregate<TorFn>(TemporalTypes::TBOOL(), TemporalTypes::TBOOL()));
        loader.RegisterFunction(std::move(set));
    }

    // ---- TcountAgg over each temporal type and over time-only inputs → tint ----
    {
        AggregateFunctionSet set("TcountAgg");
        for (const auto &t : {TemporalTypes::TBOOL(), TemporalTypes::TINT(),
                              TemporalTypes::TFLOAT(), TemporalTypes::TTEXT()}) {
            set.AddFunction(MakeTaggAggregate<TcountTempFn>(t, TemporalTypes::TINT()));
        }
        set.AddFunction(MakeTaggAggregate<TcountTempFn>(TgeompointType::TGEOMPOINT(), TemporalTypes::TINT()));
        set.AddFunction(MakeTaggAggregate<TcountTempFn>(TGeometryTypes::TGEOMETRY(), TemporalTypes::TINT()));
        set.AddFunction(MakeTaggAggregate<TcountTempFn>(TGeographyTypes::TGEOGRAPHY(), TemporalTypes::TINT()));
        set.AddFunction(MakeTaggAggregate<TcountTempFn>(TGeogpointType::TGEOGPOINT(), TemporalTypes::TINT()));

        // Time-only inputs.
        set.AddFunction(AggregateFunction::UnaryAggregateDestructor<
            SkipListState, timestamp_tz_t, string_t, TcountTstzFn,
            AggregateDestructorType::LEGACY>(
            LogicalType::TIMESTAMP_TZ, TemporalTypes::TINT()));
        set.AddFunction(MakeTaggAggregate<TcountTstzsetFn>(    SetTypes::tstzset(),         TemporalTypes::TINT()));
        set.AddFunction(MakeTaggAggregate<TcountTstzspanFn>(   SpanTypes::TSTZSPAN(),       TemporalTypes::TINT()));
        set.AddFunction(MakeTaggAggregate<TcountTstzspansetFn>(SpansetTypes::tstzspanset(), TemporalTypes::TINT()));

        loader.RegisterFunction(std::move(set));
    }

    // ---- TminAgg / TmaxAgg on tint, tfloat, ttext ----
    {
        AggregateFunctionSet set("TminAgg");
        set.AddFunction(MakeTaggAggregate<TminTintFn>(TemporalTypes::TINT(),    TemporalTypes::TINT()));
        set.AddFunction(MakeTaggAggregate<TminTfloatFn>(TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()));
        set.AddFunction(MakeTaggAggregate<TminTtextFn>(TemporalTypes::TTEXT(),   TemporalTypes::TTEXT()));
        loader.RegisterFunction(std::move(set));
    }
    {
        AggregateFunctionSet set("TmaxAgg");
        set.AddFunction(MakeTaggAggregate<TmaxTintFn>(TemporalTypes::TINT(),    TemporalTypes::TINT()));
        set.AddFunction(MakeTaggAggregate<TmaxTfloatFn>(TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()));
        set.AddFunction(MakeTaggAggregate<TmaxTtextFn>(TemporalTypes::TTEXT(),   TemporalTypes::TTEXT()));
        loader.RegisterFunction(std::move(set));
    }

    // ---- TsumAgg on tint, tfloat ----
    {
        AggregateFunctionSet set("TsumAgg");
        set.AddFunction(MakeTaggAggregate<TsumTintFn>(TemporalTypes::TINT(),    TemporalTypes::TINT()));
        set.AddFunction(MakeTaggAggregate<TsumTfloatFn>(TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()));
        loader.RegisterFunction(std::move(set));
    }

    // ---- TavgAgg on tint, tfloat → tfloat ----
    {
        AggregateFunctionSet set("TavgAgg");
        set.AddFunction(MakeTaggAggregate<TavgFn>(TemporalTypes::TINT(),  TemporalTypes::TFLOAT()));
        set.AddFunction(MakeTaggAggregate<TavgFn>(TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()));
        loader.RegisterFunction(std::move(set));
    }

    // ---- TcentroidAgg on tgeompoint / tgeogpoint → same type ----
    {
        AggregateFunctionSet set("TcentroidAgg");
        set.AddFunction(MakeTaggAggregate<TcentroidFn>(
            TgeompointType::TGEOMPOINT(), TgeompointType::TGEOMPOINT()));
        set.AddFunction(MakeTaggAggregate<TcentroidFn>(
            TGeogpointType::TGEOGPOINT(), TGeogpointType::TGEOGPOINT()));
        loader.RegisterFunction(std::move(set));
    }

    // ---- MergeAgg over each temporal type → same type ----
    {
        AggregateFunctionSet set("MergeAgg");
        for (const auto &t : {TemporalTypes::TBOOL(), TemporalTypes::TINT(),
                              TemporalTypes::TFLOAT(), TemporalTypes::TTEXT()}) {
            set.AddFunction(MakeTaggAggregate<MergeAggFn>(t, t));
        }
        set.AddFunction(MakeTaggAggregate<MergeAggFn>(
            TgeompointType::TGEOMPOINT(), TgeompointType::TGEOMPOINT()));
        set.AddFunction(MakeTaggAggregate<MergeAggFn>(
            TGeometryTypes::TGEOMETRY(), TGeometryTypes::TGEOMETRY()));
        set.AddFunction(MakeTaggAggregate<MergeAggFn>(
            TGeographyTypes::TGEOGRAPHY(), TGeographyTypes::TGEOGRAPHY()));
        set.AddFunction(MakeTaggAggregate<MergeAggFn>(
            TGeogpointType::TGEOGPOINT(), TGeogpointType::TGEOGPOINT()));
        loader.RegisterFunction(std::move(set));
    }

    // ---- AppendInstantAgg over each temporal type → same type ----
    {
        AggregateFunctionSet set("AppendInstantAgg");
        for (const auto &t : {TemporalTypes::TBOOL(), TemporalTypes::TINT(),
                              TemporalTypes::TFLOAT(), TemporalTypes::TTEXT()}) {
            set.AddFunction(MakeTemporalStateAggregate<AppendInstantAggFn>(t));
        }
        set.AddFunction(MakeTemporalStateAggregate<AppendInstantAggFn>(TgeompointType::TGEOMPOINT()));
        set.AddFunction(MakeTemporalStateAggregate<AppendInstantAggFn>(TGeometryTypes::TGEOMETRY()));
        set.AddFunction(MakeTemporalStateAggregate<AppendInstantAggFn>(TGeographyTypes::TGEOGRAPHY()));
        set.AddFunction(MakeTemporalStateAggregate<AppendInstantAggFn>(TGeogpointType::TGEOGPOINT()));
        loader.RegisterFunction(std::move(set));
    }

    // ---- AppendSequenceAgg over each temporal type → same type ----
    {
        AggregateFunctionSet set("AppendSequenceAgg");
        for (const auto &t : {TemporalTypes::TBOOL(), TemporalTypes::TINT(),
                              TemporalTypes::TFLOAT(), TemporalTypes::TTEXT()}) {
            set.AddFunction(MakeTemporalStateAggregate<AppendSequenceAggFn>(t));
        }
        set.AddFunction(MakeTemporalStateAggregate<AppendSequenceAggFn>(TgeompointType::TGEOMPOINT()));
        set.AddFunction(MakeTemporalStateAggregate<AppendSequenceAggFn>(TGeometryTypes::TGEOMETRY()));
        set.AddFunction(MakeTemporalStateAggregate<AppendSequenceAggFn>(TGeographyTypes::TGEOGRAPHY()));
        set.AddFunction(MakeTemporalStateAggregate<AppendSequenceAggFn>(TGeogpointType::TGEOGPOINT()));
        loader.RegisterFunction(std::move(set));
    }

    // ---- SpanUnionAgg(span | spanset) -> typed spanset ----
    struct SpanInputPair {
        LogicalType in;
        LogicalType out;
    };
    {
        AggregateFunctionSet set("SpanUnionAgg");
        const std::vector<SpanInputPair> span_pairs = {
            {SpanTypes::INTSPAN(),     SpansetTypes::intspanset()},
            {SpanTypes::BIGINTSPAN(),  SpansetTypes::bigintspanset()},
            {SpanTypes::FLOATSPAN(),   SpansetTypes::floatspanset()},
            {SpanTypes::DATESPAN(),    SpansetTypes::datespanset()},
            {SpanTypes::TSTZSPAN(),    SpansetTypes::tstzspanset()},
        };
        for (const auto &p : span_pairs) {
            set.AddFunction(MakeSpanSetAggregate<SpanUnionFromSpanFn>(p.in, p.out));
        }
        const std::vector<SpanInputPair> spanset_pairs = {
            {SpansetTypes::intspanset(),    SpansetTypes::intspanset()},
            {SpansetTypes::bigintspanset(), SpansetTypes::bigintspanset()},
            {SpansetTypes::floatspanset(),  SpansetTypes::floatspanset()},
            {SpansetTypes::datespanset(),   SpansetTypes::datespanset()},
            {SpansetTypes::tstzspanset(),   SpansetTypes::tstzspanset()},
        };
        for (const auto &p : spanset_pairs) {
            set.AddFunction(MakeSpanSetAggregate<SpanUnionFromSpanSetFn>(p.in, p.out));
        }
        loader.RegisterFunction(std::move(set));
    }

    // ---- Window aggregates: WminAgg / WmaxAgg / WsumAgg / WcountAgg / WavgAgg ----
    {
        AggregateFunctionSet set("WminAgg");
        set.AddFunction(MakeWindowAggregate<WminTintFn>(TemporalTypes::TINT(),     TemporalTypes::TINT()));
        set.AddFunction(MakeWindowAggregate<WminTfloatFn>(TemporalTypes::TFLOAT(),  TemporalTypes::TFLOAT()));
        loader.RegisterFunction(std::move(set));
    }
    {
        AggregateFunctionSet set("WmaxAgg");
        set.AddFunction(MakeWindowAggregate<WmaxTintFn>(TemporalTypes::TINT(),     TemporalTypes::TINT()));
        set.AddFunction(MakeWindowAggregate<WmaxTfloatFn>(TemporalTypes::TFLOAT(),  TemporalTypes::TFLOAT()));
        loader.RegisterFunction(std::move(set));
    }
    {
        AggregateFunctionSet set("WsumAgg");
        set.AddFunction(MakeWindowAggregate<WsumTintFn>(TemporalTypes::TINT(),     TemporalTypes::TINT()));
        set.AddFunction(MakeWindowAggregate<WsumTfloatFn>(TemporalTypes::TFLOAT(),  TemporalTypes::TFLOAT()));
        loader.RegisterFunction(std::move(set));
    }
    {
        AggregateFunctionSet set("WcountAgg");
        set.AddFunction(MakeWindowAggregate<WcountAggFn>(TemporalTypes::TINT(),    TemporalTypes::TINT()));
        set.AddFunction(MakeWindowAggregate<WcountAggFn>(TemporalTypes::TFLOAT(),  TemporalTypes::TINT()));
        // Spatial-temporal types — MobilityDB exposes wcount over the
        // tspatial family with the same `temporal_wagg_transform_transfn`
        // + `temporal_transform_wcount` shape (the count value is
        // subtype-agnostic). The result type is always tint.
        set.AddFunction(MakeWindowAggregate<WcountAggFn>(TgeompointType::TGEOMPOINT(),   TemporalTypes::TINT()));
        set.AddFunction(MakeWindowAggregate<WcountAggFn>(TGeogpointType::TGEOGPOINT(),   TemporalTypes::TINT()));
        set.AddFunction(MakeWindowAggregate<WcountAggFn>(TGeometryTypes::TGEOMETRY(),    TemporalTypes::TINT()));
        set.AddFunction(MakeWindowAggregate<WcountAggFn>(TGeographyTypes::TGEOGRAPHY(),  TemporalTypes::TINT()));
        loader.RegisterFunction(std::move(set));
    }
    {
        AggregateFunctionSet set("WavgAgg");
        set.AddFunction(MakeWindowAggregate<WavgFn>(TemporalTypes::TINT(),    TemporalTypes::TFLOAT()));
        set.AddFunction(MakeWindowAggregate<WavgFn>(TemporalTypes::TFLOAT(),  TemporalTypes::TFLOAT()));
        loader.RegisterFunction(std::move(set));
    }

    // ---- SetUnionAgg(<scalar | typed-set>) -> typed set ----
    {
        AggregateFunctionSet set("SetUnionAgg");
        // Scalar inputs.
        set.AddFunction(MakeSetUnionScalarAggregate<int32_t, Int32ToDatum, T_INT4>(
            LogicalType::INTEGER, SetTypes::intset()));
        set.AddFunction(MakeSetUnionScalarAggregate<int64_t, Int64ToDatum, T_INT8>(
            LogicalType::BIGINT, SetTypes::bigintset()));
        set.AddFunction(MakeSetUnionScalarAggregate<double, Float8ToDatum, T_FLOAT8>(
            LogicalType::DOUBLE, SetTypes::floatset()));
        set.AddFunction(MakeSetUnionScalarAggregate<date_t, DateToDatum, T_DATE>(
            LogicalType::DATE, SetTypes::dateset()));
        set.AddFunction(MakeSetUnionScalarAggregate<timestamp_tz_t, TstzToDatum, T_TIMESTAMPTZ>(
            LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()));
        // Text — uses the dedicated SetUnionFromTextFn that owns the
        // cstring2text lifecycle.
        set.AddFunction(AggregateFunction::UnaryAggregateDestructor<
            SetAggState, string_t, string_t, SetUnionFromTextFn,
            AggregateDestructorType::LEGACY>(
            LogicalType::VARCHAR, SetTypes::textset()));
        // Geometry scalar — uses geo_union_transfn directly.
        // The MobilityDB `setUnion(geography) → geogset` overload would
        // need a `geography` SQL type that MobilityDuck doesn't expose;
        // register only the geomset path here. (The geogset → geogset
        // set-input variant below still works for users who already
        // have geogset values.)
        set.AddFunction(AggregateFunction::UnaryAggregateDestructor<
            SetAggState, string_t, string_t, SetUnionFromGeoFn,
            AggregateDestructorType::LEGACY>(
            GeoTypes::GEOMETRY(), SpatialSetType::geomset()));

        // Set inputs.
        const std::vector<SpanInputPair> set_pairs = {
            {SetTypes::intset(),         SetTypes::intset()},
            {SetTypes::bigintset(),      SetTypes::bigintset()},
            {SetTypes::floatset(),       SetTypes::floatset()},
            {SetTypes::textset(),        SetTypes::textset()},
            {SetTypes::dateset(),        SetTypes::dateset()},
            {SetTypes::tstzset(),        SetTypes::tstzset()},
            {SpatialSetType::geomset(),  SpatialSetType::geomset()},
            {SpatialSetType::geogset(),  SpatialSetType::geogset()},
        };
        for (const auto &p : set_pairs) {
            set.AddFunction(MakeSetAggregate<SetUnionFromSetFn>(p.in, p.out));
        }
        loader.RegisterFunction(std::move(set));
    }
}

} // namespace duckdb
