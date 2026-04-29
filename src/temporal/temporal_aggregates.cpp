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

} // namespace duckdb
