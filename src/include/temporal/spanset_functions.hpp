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

struct SpansetFunctions{
    // for cast
    static bool Spanset_to_text(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Text_to_spanset(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Value_to_spanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Set_to_spanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Span_to_spanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Spanset_to_span_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Intspanset_to_floatspanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Floatspanset_to_intspanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Datespanset_to_tstzspanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tstzspanset_to_datespanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    // TODO: Multirange functions

    // Accessor functions
    static void Spanset_mem_size(DataChunk &args, ExpressionState &state, Vector &result);
    static void Spanset_lower_inc(DataChunk &args, ExpressionState &state, Vector &result);
    static void Spanset_upper_inc(DataChunk &args, ExpressionState &state, Vector &result);
    static void Datespanset_duration(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tstzspanset_duration(DataChunk &args, ExpressionState &state, Vector &result);

    // Transformations functions
    // floor/ceil/round/degrees/radians on floatspanset are generated (generated_temporal_udfs.cpp).
    
};


} // namespace duckdb
