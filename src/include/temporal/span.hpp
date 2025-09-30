#pragma once

#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include <tydef.hpp>

extern "C" {
    #include <meos.h>
    #include <meos_internal.h>
}

namespace duckdb {

struct SpanTypes {
    static const std::vector<LogicalType> &AllTypes();

    static LogicalType INTSPAN();
    static LogicalType BIGINTSPAN();
    static LogicalType FLOATSPAN();
    static LogicalType TEXTSPAN();
    static LogicalType DATESPAN();
    static LogicalType TSTZSPAN();
    static void RegisterTypes(DatabaseInstance &instance);
    static void RegisterScalarFunctions(DatabaseInstance &instance);
    static void RegisterCastFunctions(DatabaseInstance &instance);
};

struct SpanFunctions {
    // for cast
    static bool Span_to_text(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Text_to_span(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Value_to_span_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Intspan_to_floatspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Floatspan_to_intspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Datespan_to_tstzspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tstzspan_to_datespan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    // scalar functions
    static void Span_as_text(DataChunk &args, ExpressionState &state, Vector &result);    
    static void Span_constructor(DataChunk &args, ExpressionState &state, Vector &result);   
    static void Span_binary_constructor(DataChunk &args, ExpressionState &state, Vector &result);
    static void Value_to_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Intspan_to_floatspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Floatspan_to_intspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Datespan_to_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tstzspan_to_datespan(DataChunk &args, ExpressionState &state, Vector &result);    
    static void Span_mem_size(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_num_values(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_start_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_end_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_value_n(DataChunk &args, ExpressionState &state, Vector &result_vec);
    static void Span_values(DataChunk &args, ExpressionState &state, Vector &result);
    static void Numspan_shift(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contains_tstzspan_timestamptz(DataChunk &args, ExpressionState &state, Vector &result);
};

struct SpanTypeMapping {
    static meosType GetMeosTypeFromAlias(const std::string &alias);
    static LogicalType GetChildType(const LogicalType &type);
};

} // namespace duckdb
