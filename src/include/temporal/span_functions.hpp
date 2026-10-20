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
    static void Span_constructor(DataChunk &args, ExpressionState &state, Vector &result);
    // TODO (Type Range): static void Range_to_span(DataChunk &args, ExpressionState &state, Vector &result);
    // TODO (Type Range): static void Span_to_range(DataChunk &args, ExpressionState &state, Vector &result);
        // accessors
    static void Span_lower_inc(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_upper_inc(DataChunk &args, ExpressionState &state, Vector &result);
        //transformations
    static void Numspan_expand(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tstzspan_shift(DataChunk &args, ExpressionState &state, Vector &result);
    // floor/ceil/round/degrees/radians on floatspan are generated (generated_temporal_udfs.cpp);
    // only round(DOUBLE), the scalar base helper, remains hand-written.
    static void Float_round(DataChunk &args, ExpressionState &state, Vector &result);
        // Set operators
    static void Union_span_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Intersection_span_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Intersection_value_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Minus_span_span(DataChunk &args, ExpressionState &state, Vector &result);
    
};



} // namespace duckdb
