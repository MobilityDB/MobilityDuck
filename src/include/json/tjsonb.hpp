#pragma once

#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

struct TJsonbTypes {
    static LogicalType jsonb();
    // The JSON path a temporal JSONB query is written in. PostgreSQL declares `jsonpath`
    // as a built-in, MobilityDB names it in the SQL of the path predicates, and MEOS
    // publishes its text I/O, so the binding carries it as its own value type.
    static LogicalType jsonpath();
    static LogicalType tjsonb();
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
    // Base jsonb value (BLOB-alias) -> VARCHAR render cast, mirroring the cbuffer
    // sibling (CbufferFunctions::Cbuffer_out_cast): the generated startValue/endValue
    // return the jsonb base value, which DuckDB renders through this cast (jsonb_out)
    // as canonical JSON text.
    static bool Jsonb_out_cast(Vector &source, Vector &result, idx_t count,
                                CastParameters &parameters);
    // VARCHAR -> base jsonb value, the inverse of Jsonb_out_cast. Without it the
    // jsonb alias inherits DuckDB's VARCHAR -> BLOB conversion, which stores the
    // JSON TEXT where the binary jsonb container belongs.
    static bool Jsonb_in_cast(Vector &source, Vector &result, idx_t count,
                                CastParameters &parameters);
    static bool Jsonpath_in_cast(Vector &source, Vector &result, idx_t count,
                                CastParameters &parameters);
    static bool Jsonpath_out_cast(Vector &source, Vector &result, idx_t count,
                                CastParameters &parameters);
};

} // namespace duckdb
