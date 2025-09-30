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

struct SpansetTypes {
    static LogicalType intspanset();
    static LogicalType bigintspanset();
    static LogicalType floatspanset();
    static LogicalType textspanset();
    static LogicalType datespanset();
    static LogicalType tstzspanset();

    static const std::vector<LogicalType> &AllTypes();

    static void RegisterTypes(DatabaseInstance &db);
    static void RegisterCastFunctions(DatabaseInstance &db);
    static void RegisterScalarFunctions(DatabaseInstance &db);    
    static void RegisterSetUnnest(DatabaseInstance &db);    
};

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

    // other    
    static void Spanset_as_text(DataChunk &args, ExpressionState &state, Vector &result);
    
    static void Spanset_constructor(DataChunk &args, ExpressionState &state, Vector &result); 
    static void Value_to_spanset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_to_spanset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Span_to_spanset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Spanset_to_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Intspanset_to_floatspanset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Floatspanset_to_intspanset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Datespanset_to_tstzspanset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tstzspanset_to_datespanset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Spanset_mem_size(DataChunk &args, ExpressionState &state, Vector &result);
    static void Spanset_lower(DataChunk &args, ExpressionState &state, Vector &result);
    static void Spanset_upper(DataChunk &args, ExpressionState &state, Vector &result);
    static void Spanset_lower_inc(DataChunk &args, ExpressionState &state, Vector &result);
    static void Spanset_upper_inc(DataChunk &args, ExpressionState &state, Vector &result);
    static void Numspanset_width(DataChunk &args, ExpressionState &state, Vector &result);
    static void Datespanset_duration(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tstzspanset_duration(DataChunk &args, ExpressionState &state, Vector &result);
    static void Spanset_num_spans(DataChunk &args, ExpressionState &state, Vector &result);
    static void Spanset_start_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Spanset_end_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Spanset_span_n(DataChunk &args, ExpressionState &state, Vector &result);
};

struct SpansetTypeMapping {
    static meosType GetMeosTypeFromAlias(const std::string &alias);
    static LogicalType GetChildType(const LogicalType &type);
    static LogicalType GetBaseType(const LogicalType &type);
    static LogicalType GetSetType(const LogicalType &type);
};

} // namespace duckdb
