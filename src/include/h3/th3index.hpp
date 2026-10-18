#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

/* H3INDEX is a 64-bit H3 cell id, surfaced as BIGINT (the uint64 bit pattern
 * reinterprets losslessly; equality/ordering care only about the bits).
 * H3 cells are geography-like — inherently WGS84 / EPSG:4326.
 * TH3INDEX is the temporal H3 cell index, stored as a Temporal* blob (BLOB).
 *
 * These type registrations are the justified hand layer (like QUADBIN /
 * TQUADBIN); the H3 function surface is generated from the MEOS-API catalog. */
struct H3indexTypes {
    static LogicalType h3index();
    static LogicalType th3index();

    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
};

struct H3indexFunctions {
    /* In/out — static h3index cell (BIGINT), H3 hex-string form */
    static bool H3index_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool H3index_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* In/out — th3index temporal value */
    static bool Th3index_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Th3index_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
};

} // namespace duckdb
