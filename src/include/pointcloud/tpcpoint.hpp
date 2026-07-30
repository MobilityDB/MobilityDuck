#pragma once

#include <tydef.hpp>
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {


struct TPcpointTypes {
    static LogicalType pcpoint();
    static LogicalType tpcpoint();
    static LogicalType GEOMETRY();
    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterScalarFunctions(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
    static void RegisterScalarInOutFunctions(ExtensionLoader &loader);
};

struct TpcpointFunctions {
    static bool StringToTpcpoint(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool TpcpointToString(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    // Base pcpoint value (BLOB-alias) -> VARCHAR render cast (pcpoint_hex_out), so the
    // generated startValue/endValue base value renders as hex-WKB text (cbuffer sibling).
    static bool Pcpoint_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool WkbBlobToGeometry(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
};


} // namespace duckdb
