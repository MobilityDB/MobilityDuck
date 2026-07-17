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

#include "temporal/set_functions.hpp"

namespace duckdb {

struct SetTypes {
    static LogicalType intset();
    static LogicalType bigintset();
    static LogicalType floatset();
    static LogicalType textset();
    static LogicalType dateset();
    static LogicalType tstzset();

    static const std::vector<LogicalType> &AllTypes();

    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
    static void RegisterScalarFunctions(ExtensionLoader &loader);
    static void RegisterSetUnnest(ExtensionLoader &loader);
    static void RegisterSetUnionAgg(ExtensionLoader &loader);
};

struct SetTypeMapping {
    static MeosType GetMeosTypeFromAlias(const std::string &alias);
    static LogicalType GetChildType(const LogicalType &type);
};

} // namespace duckdb
