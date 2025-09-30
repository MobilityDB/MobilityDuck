#pragma once

#include <tydef.hpp>
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {


struct TGeometryTypes {
    static LogicalType TGEOMETRY();
    static LogicalType MEOS_WKB_BLOB();
    static LogicalType GEOMETRY();
    static void RegisterTypes(DatabaseInstance &instance);
    static void RegisterScalarFunctions(DatabaseInstance &instance);
    static void RegisterCastFunctions(DatabaseInstance &instance);
    static void RegisterScalarInOutFunctions(DatabaseInstance &instance);
};

struct TgeometryFunctions {
    static bool StringToTgeometry(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool TgeometryToString(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool WkbBlobToGeometry(Vector &source, Vector &result, idx_t count, CastParameters &parameters); 
};


} // namespace duckdb