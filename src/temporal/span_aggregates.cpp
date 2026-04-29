#include "meos_wrapper_simple.hpp"

#include "temporal/span.hpp"
#include "temporal/span_aggregates.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/function/aggregate_function.hpp"
#include "duckdb/function/function_set.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

namespace {

// State for the extent(span) aggregate. The MEOS Span is a fixed-size
// struct, so we can hold it inline in the aggregate state.
struct SpanExtentState {
    Span span;
    bool isset;
};

struct SpanExtentFunction {
    template <class STATE>
    static void Initialize(STATE &state) {
        state.isset = false;
    }

    static bool IgnoreNull() {
        return true;
    }

    // Operation receives the input blob; we decode to Span and call MEOS
    // span_extent_transfn, which expands state's bounds in place when
    // state is non-null and otherwise returns a fresh palloc'd copy.
    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        // INPUT_TYPE is string_t for blob-encoded spans.
        const Span *input_span = reinterpret_cast<const Span *>(input.GetData());
        if (!state.isset) {
            // First call: initialize state with a copy of the input.
            memcpy(&state.span, input_span, sizeof(Span));
            state.isset = true;
        } else {
            // Subsequent calls: expand state in place.
            // span_extent_transfn(state, s) calls span_expand(s, state).
            (void) span_extent_transfn(&state.span, input_span);
        }
    }

    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &unary_input,
                                  idx_t /*count*/) {
        // extent is idempotent in the value dimension, so a single
        // application is equivalent to applying the same value `count`
        // times.
        Operation<INPUT_TYPE, STATE, OP>(state, input, unary_input);
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
        target = finalize_data.ReturnString(string_t(reinterpret_cast<const char *>(&state.span), sizeof(Span)));
    }
};

static AggregateFunction MakeExtentSpanAggregate(const LogicalType &span_type) {
    return AggregateFunction::UnaryAggregate<SpanExtentState, string_t, string_t, SpanExtentFunction>(
        span_type, span_type);
}

} // namespace

void SpanAggregates::RegisterAggregateFunctions(ExtensionLoader &loader) {
    AggregateFunctionSet extent_set("extent");
    for (const auto &span_type : {SpanTypes::INTSPAN(), SpanTypes::BIGINTSPAN(),
                                  SpanTypes::FLOATSPAN(), SpanTypes::DATESPAN(),
                                  SpanTypes::TSTZSPAN()}) {
        extent_set.AddFunction(MakeExtentSpanAggregate(span_type));
    }
    loader.RegisterFunction(std::move(extent_set));
}

} // namespace duckdb
