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

    static void RegisterTypes(DatabaseInstance &db);
    static void RegisterCastFunctions(DatabaseInstance &db);
    static void RegisterScalarFunctions(DatabaseInstance &db);    
    static void RegisterSetUnnest(DatabaseInstance &db);    
};

struct SetTypeMapping {
    static meosType GetMeosTypeFromAlias(const std::string &alias);
    static LogicalType GetChildType(const LogicalType &type);
};

} // namespace duckdb
