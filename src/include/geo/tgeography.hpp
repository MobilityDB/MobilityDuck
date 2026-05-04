#pragma once

#include <tydef.hpp>
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

// Temporal geographies — the geographic-coordinate-system counterpart to
// `tgeometry`. Mirrors the `tgeometry` registration pattern (see
// `src/geo/tgeometry.{hpp,cpp}` and `src/geo/tgeometry_ops.cpp`); the
// MEOS C functions reached here are subtype-agnostic and shared between
// the two types.
struct TGeographyTypes {
    static LogicalType TGEOGRAPHY();
    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterScalarFunctions(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
    static void RegisterScalarInOutFunctions(ExtensionLoader &loader);
};

struct TgeographyFunctions {
    static bool StringToTgeography(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool TgeographyToString(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
};

} // namespace duckdb
