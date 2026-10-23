#pragma once

#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include <tydef.hpp>

extern "C" {
    #include <meos.h>
    #include <meos_internal.h>
}

namespace duckdb {


struct SpanFunctions {
    // for cast
    static bool Span_to_text(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Text_to_span(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Value_to_span_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Intspan_to_floatspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Floatspan_to_intspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Datespan_to_tstzspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tstzspan_to_datespan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Set_to_span_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    // TODO (Type Range): static bool Range_to_span_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    // TODO (Type Range): static bool Span_to_range_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    // scalar functions
    // TODO (Type Range): static void Range_to_span(DataChunk &args, ExpressionState &state, Vector &result);
    // TODO (Type Range): static void Span_to_range(DataChunk &args, ExpressionState &state, Vector &result);
        // Set operators
    
};



} // namespace duckdb
