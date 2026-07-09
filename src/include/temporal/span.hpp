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

struct SpanTypes {
    static const std::vector<LogicalType> &AllTypes();

    static LogicalType intspan();
    static LogicalType bigintspan();
    static LogicalType floatspan();
    static LogicalType textspan();
    static LogicalType datespan();
    static LogicalType tstzspan();
    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterScalarFunctions(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
};


struct SpanTypeMapping {
    static MeosType GetMeosTypeFromAlias(const std::string &alias);
    static LogicalType GetChildType(const LogicalType &type);
};

} // namespace duckdb
