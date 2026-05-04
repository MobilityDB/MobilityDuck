#include "meos_wrapper_simple.hpp"

#include "geo/spatial_aggregates.hpp"
#include "geo/stbox.hpp"
#include "geo/tgeompoint.hpp"

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
        input_type, StboxType::STBOX());
}

} // namespace

void SpatialAggregates::AddExtentOverloads(AggregateFunctionSet &extent_set) {
    extent_set.AddFunction(MakeExtentTspatialAggregate(TgeompointType::TGEOMPOINT()));
}

} // namespace duckdb
