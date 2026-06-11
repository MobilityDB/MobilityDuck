#pragma once

#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

struct TJsonbTypes {
    static LogicalType TJSONB();
    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterScalarFunctions(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
    static void RegisterScalarInOutFunctions(ExtensionLoader &loader);
};

struct TjsonbFunctions {
    static bool StringToTjsonb(Vector &source, Vector &result, idx_t count,
                                CastParameters &parameters);
    static bool TjsonbToString(Vector &source, Vector &result, idx_t count,
                                CastParameters &parameters);
};

} // namespace duckdb
