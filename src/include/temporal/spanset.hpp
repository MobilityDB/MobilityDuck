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

struct SpansetTypes {
    static LogicalType intspanset();
    static LogicalType bigintspanset();
    static LogicalType floatspanset();
    static LogicalType datespanset();
    static LogicalType tstzspanset();

    static const std::vector<LogicalType> &AllTypes();

    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
    static void RegisterScalarFunctions(ExtensionLoader &loader);    
    static void RegisterSetUnnest(ExtensionLoader &loader);    
};

struct SpansetTypeMapping {
    static MeosType GetMeosTypeFromAlias(const std::string &alias);
    static LogicalType GetChildType(const LogicalType &type);
    static LogicalType GetBaseType(const LogicalType &type);
    static LogicalType GetSetType(const LogicalType &type);
};

} // namespace duckdb
