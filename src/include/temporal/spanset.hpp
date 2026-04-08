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

struct SpansetTypeMapping {
    static meosType GetMeosTypeFromAlias(const std::string &alias);
    static LogicalType GetChildType(const LogicalType &type);
    static LogicalType GetBaseType(const LogicalType &type);
    static LogicalType GetSetType(const LogicalType &type);
};

} // namespace duckdb
