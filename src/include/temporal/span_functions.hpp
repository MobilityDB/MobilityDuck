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
    static void Span_as_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_as_binary(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_as_hexwkb(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_from_binary(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_from_hexwkb(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_constructor(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_binary_constructor(DataChunk &args, ExpressionState &state, Vector &result);
    static void Value_to_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Intspan_to_floatspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Floatspan_to_intspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Datespan_to_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tstzspan_to_datespan(DataChunk &args, ExpressionState &state, Vector &result);    
    static void Set_to_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_spans(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_split_n_spans(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_split_each_n_spans(DataChunk &args, ExpressionState &state, Vector &result);
    // TODO (Type Range): static void Range_to_span(DataChunk &args, ExpressionState &state, Vector &result);
    // TODO (Type Range): static void Span_to_range(DataChunk &args, ExpressionState &state, Vector &result);
        // accessors
    static void Span_lower(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_upper(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_hash(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_hash_extended(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_lower_inc(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_upper_inc(DataChunk &args, ExpressionState &state, Vector &result);
    static void Numspan_width(DataChunk &args, ExpressionState &state, Vector &result);
    static void Datespan_duration(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tstzspan_duration(DataChunk &args, ExpressionState &state, Vector &result);
        //transformations
    static void Numspan_expand(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tstzspan_expand(DataChunk &args, ExpressionState &state, Vector &result);
    static void Numspan_shift(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tstzspan_shift(DataChunk &args, ExpressionState &state, Vector &result);
    static void Numspan_scale(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tstzspan_scale(DataChunk &args, ExpressionState &state, Vector &result);
    static  void Numspan_shift_scale(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tstzspan_shift_scale(DataChunk &args, ExpressionState &state, Vector &result);
    // floor/ceil/round/degrees/radians on floatspan are generated (generated_temporal_udfs.cpp);
    // only round(DOUBLE), the scalar base helper, remains hand-written.
    static void Float_round(DataChunk &args, ExpressionState &state, Vector &result);
        // TODO: Selectivity functions
        // Comparison operators
    static void Span_eq(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_ne(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_lt(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_le(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_ge(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_gt(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_cmp(DataChunk &args, ExpressionState &state, Vector &result);
        // Topological operators
    static void Contains_span_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contains_span_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contained_value_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contained_span_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overlaps_span_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adjacent_value_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adjacent_span_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adjacent_span_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Left_value_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Left_span_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Left_span_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Right_value_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Right_span_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Right_span_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overleft_value_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overleft_span_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overleft_span_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overright_value_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overright_span_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overright_span_value(DataChunk &args, ExpressionState &state, Vector &result);
        // Set operators
    static void Union_span_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Union_value_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Union_span_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Intersection_span_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Intersection_value_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Intersection_span_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Minus_span_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Minus_value_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Minus_span_value(DataChunk &args, ExpressionState &state, Vector &result);
        // Distance operators
    static void Distance_span_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Distance_value_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Distance_span_value(DataChunk &args, ExpressionState &state, Vector &result);
    
};



} // namespace duckdb
