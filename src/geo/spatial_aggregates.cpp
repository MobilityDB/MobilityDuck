#include "meos_wrapper_simple.hpp"

#include "geo/spatial_aggregates.hpp"
#include "geo/stbox.hpp"
#include "geo/tgeompoint.hpp"
#include "geo/tgeogpoint.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

#include <cfloat>
#include <mutex>

#include "duckdb/common/exception.hpp"
#include "duckdb/function/aggregate_function.hpp"
#include "duckdb/function/function_set.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

namespace {

// State for extent(tgeompoint) → stbox. STBox is a fixed-size POD struct
// (Span period + double xmin/xmax/ymin/ymax/zmin/zmax + int32 srid + int16
// flags) so it can live inline in the aggregate state.
struct StboxExtentState {
    STBox box;
    bool isset;
};

struct TspatialExtentFunction {
    template <class STATE>
    static void Initialize(STATE &state) {
        state.isset = false;
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

        if (!state.isset) {
            STBox *fresh = tspatial_extent_transfn(nullptr, temp);
            memcpy(&state.box, fresh, sizeof(STBox));
            free(fresh);
            state.isset = true;
        } else {
            (void) tspatial_extent_transfn(&state.box, temp);
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
            memcpy(&target.box, &source.box, sizeof(STBox));
            target.isset = true;
        } else {
            stbox_expand(&source.box, &target.box);
        }
    }

    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
        if (!state.isset) {
            finalize_data.ReturnNull();
            return;
        }
        target = finalize_data.ReturnString(
            string_t(reinterpret_cast<const char *>(&state.box), sizeof(STBox)));
    }
};

static AggregateFunction MakeExtentTspatialAggregate(const LogicalType &input_type) {
    return AggregateFunction::UnaryAggregate<StboxExtentState, string_t, string_t, TspatialExtentFunction>(
        input_type, StboxType::stbox());
}

// extent(stbox) → stbox — input is a raw STBox blob (not a Temporal wrapper).
struct StboxExtentBoxFunction {
    template <class STATE>
    static void Initialize(STATE &state) {
        state.isset = false;
    }

    static bool IgnoreNull() {
        return true;
    }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(input.GetData());
        if (!state.isset) {
            memcpy(&state.box, bytes, sizeof(STBox));
            state.isset = true;
        } else {
            stbox_expand(reinterpret_cast<const STBox *>(bytes), &state.box);
        }
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
            memcpy(&target.box, &source.box, sizeof(STBox));
            target.isset = true;
        } else {
            stbox_expand(&source.box, &target.box);
        }
    }

    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
        if (!state.isset) {
            finalize_data.ReturnNull();
            return;
        }
        target = finalize_data.ReturnString(
            string_t(reinterpret_cast<const char *>(&state.box), sizeof(STBox)));
    }
};

// ============================================================================
// tCentroid(tgeompoint) -> tgeompoint
//
// Skiplist-backed. Per-instant value is stored as a (sum_x, sum_y, count)
// triple for 2D points or (sum_x, sum_y, sum_z, count) quadruple for 3D
// points. Combine merges via local replicas of MEOS's datum_sum_double3
// or datum_sum_double4 — chosen by inspecting state->extra
// (GeoAggregateState).
// ============================================================================

struct TcentroidState {
    SkipList *skiplist;
};

// Local replica of the MEOS-internal struct {int32_t srid; bool hasz}.
struct mduck_geo_agg_state {
    int32_t srid;
    bool hasz;
};

struct mduck_double3 {
    double a;
    double b;
    double c;
};
struct mduck_double4 {
    double a;
    double b;
    double c;
    double d;
};

static Datum mduck_datum_sum_double3(Datum l, Datum r) {
    auto *lp = reinterpret_cast<mduck_double3 *>(l);
    auto *rp = reinterpret_cast<mduck_double3 *>(r);
    auto *out = reinterpret_cast<mduck_double3 *>(malloc(sizeof(mduck_double3)));
    out->a = lp->a + rp->a;
    out->b = lp->b + rp->b;
    out->c = lp->c + rp->c;
    return reinterpret_cast<Datum>(out);
}
static Datum mduck_datum_sum_double4(Datum l, Datum r) {
    auto *lp = reinterpret_cast<mduck_double4 *>(l);
    auto *rp = reinterpret_cast<mduck_double4 *>(r);
    auto *out = reinterpret_cast<mduck_double4 *>(malloc(sizeof(mduck_double4)));
    out->a = lp->a + rp->a;
    out->b = lp->b + rp->b;
    out->c = lp->c + rp->c;
    out->d = lp->d + rp->d;
    return reinterpret_cast<Datum>(out);
}

struct TcentroidFunction {
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
        state.skiplist = tpoint_tcentroid_transfn(state.skiplist, temp);
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
            // Take ownership of source's skiplist so its geo-specific
            // `extra` field (GeoAggregateState) is preserved for the
            // finalfn. Source's destructor will see a null pointer and
            // skip its skiplist_free.
            const_cast<STATE &>(target).skiplist = source.skiplist;
            const_cast<STATE &>(source).skiplist = nullptr;
            return;
        }
        // Both non-empty: pick 2D vs 3D merge fn based on source's extra.
        auto *extra = reinterpret_cast<mduck_geo_agg_state *>(source.skiplist->extra);
        datum_func2 merge_fn = (extra && extra->hasz) ? &mduck_datum_sum_double4
                                                       : &mduck_datum_sum_double3;
        void **values = skiplist_values(source.skiplist);
        int count = source.skiplist->length;
        temporal_skiplist_splice(target.skiplist, values, count, merge_fn, false);
        free(values);
    }

    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
        if (!state.skiplist || state.skiplist->length == 0) {
            finalize_data.ReturnNull();
            return;
        }
        Temporal *result = tpoint_tcentroid_finalfn(state.skiplist);
        // tpoint_tcentroid_finalfn frees the skiplist; null state.skiplist
        // so the destructor doesn't double-free.
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

static AggregateFunction MakeTcentroidAggregate(const LogicalType &input_type) {
    return AggregateFunction::UnaryAggregateDestructor<TcentroidState, string_t, string_t, TcentroidFunction>(
        input_type, input_type);
}

struct MinDistanceState {
    double min_dist;
};

struct MinDistanceFunction {
    template <class STATE>
    static void Initialize(STATE &state) {
        state.min_dist = DBL_MAX;
    }

    static bool IgnoreNull() {
        return true;
    }

    template <class A_TYPE, class B_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const A_TYPE &a, const B_TYPE &b, AggregateBinaryInput &) {
        // Not a RegisterSerializedScalarFunction, so it isn't wrapped by
        // WrapScalarFunctionWithMeosExecMutex — mindistance_tgeo_tgeo uses GEOS's
        // legacy global API internally, so per-thread init + the serialization
        // mutex have to be taken here explicitly (see meos_exec_serial.hpp).
        EnsureMeosThreadInitialized();
        std::lock_guard<std::mutex> guard(MeosSerializedExecMutex());

        const uint8_t *abytes = reinterpret_cast<const uint8_t *>(a.GetData());
        Temporal *t1 = reinterpret_cast<Temporal *>(malloc(a.GetSize()));
        memcpy(t1, abytes, a.GetSize());

        const uint8_t *bbytes = reinterpret_cast<const uint8_t *>(b.GetData());
        Temporal *t2 = reinterpret_cast<Temporal *>(malloc(b.GetSize()));
        memcpy(t2, bbytes, b.GetSize());

        state.min_dist = mindistance_tgeo_tgeo(t1, t2, state.min_dist);
        free(t1);
        free(t2);
    }

    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        if (source.min_dist < target.min_dist) {
            target.min_dist = source.min_dist;
        }
    }

    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
        if (state.min_dist == DBL_MAX) {
            finalize_data.ReturnNull();
            return;
        }
        target = state.min_dist;
    }
};

static AggregateFunction MakeMinDistanceAggregate(const LogicalType &input_type) {
    return AggregateFunction::BinaryAggregate<MinDistanceState, string_t, string_t, double, MinDistanceFunction>(
        input_type, input_type, LogicalType::DOUBLE);
}

} // namespace

void SpatialAggregates::AddExtentOverloads(AggregateFunctionSet &extent_set) {
    extent_set.AddFunction(MakeExtentTspatialAggregate(TgeompointType::tgeompoint()));
    extent_set.AddFunction(AggregateFunction::UnaryAggregate<StboxExtentState, string_t, string_t, StboxExtentBoxFunction>(
        StboxType::stbox(), StboxType::stbox()));
}

void SpatialAggregates::RegisterTcentroid(ExtensionLoader &loader) {
    AggregateFunctionSet tcentroid_set("TcentroidAgg");
    tcentroid_set.AddFunction(MakeTcentroidAggregate(TgeompointType::tgeompoint()));
    loader.RegisterFunction(std::move(tcentroid_set));
}

void SpatialAggregates::RegisterMinDistance(ExtensionLoader &loader) {
    // Named "MinDistanceAgg", not "minDistance": DuckDB routes SCALAR_FUNCTION_ENTRY and
    // AGGREGATE_FUNCTION_ENTRY into the same catalog set (DuckSchemaEntry::GetCatalogSet),
    // so registering an aggregate under the same name as the existing scalar minDistance
    // (LIST(tgeo), LIST(tgeo)) -> DOUBLE overloads from codegen hits ALTER_ON_CONFLICT ->
    // AlterEntry, which requires a real ClientContext the extension-load transaction
    // (CatalogTransaction::GetSystemTransaction) doesn't have -> crashes at load. Same
    // reason RegisterTcentroid above uses "TcentroidAgg" instead of "tCentroid".
    AggregateFunctionSet min_distance_set("MinDistanceAgg");
    min_distance_set.AddFunction(MakeMinDistanceAggregate(TgeompointType::tgeompoint()));
    min_distance_set.AddFunction(MakeMinDistanceAggregate(TgeogpointType::tgeogpoint()));
    loader.RegisterFunction(std::move(min_distance_set));
}

} // namespace duckdb
