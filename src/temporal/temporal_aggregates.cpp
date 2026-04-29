#include "meos_wrapper_simple.hpp"

#include "temporal/tbox.hpp"
#include "temporal/temporal.hpp"
#include "temporal/temporal_aggregates.hpp"

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

} // namespace

void TemporalAggregates::AddExtentOverloads(AggregateFunctionSet &extent_set) {
    extent_set.AddFunction(MakeExtentTnumberAggregate(TemporalTypes::TINT()));
    extent_set.AddFunction(MakeExtentTnumberAggregate(TemporalTypes::TFLOAT()));
}

} // namespace duckdb
