#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

/* QUADBIN is a 64-bit unsigned cell id (CARTO QUADBIN square-quadtree DGGS);
 * surfaced as BIGINT (signed reinterpretation is safe — equality and ordering
 * care only about the bit pattern).  TQUADBIN is the temporal cell index,
 * stored as a Temporal* blob (BLOB).  Analogue of H3INDEX / TH3INDEX. */
struct QuadbinTypes {
    static LogicalType quadbin();
    static LogicalType tquadbin();

    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
    static void RegisterScalarFunctions(ExtensionLoader &loader);
};

struct QuadbinFunctions {
    /* In/out — static QUADBIN cell (BIGINT) */
    static bool Quadbin_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Quadbin_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* In/out — TQUADBIN temporal value */
    static bool Tquadbin_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tquadbin_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* Static cell helpers */
    static void Quadbin_tile_to_cell(DataChunk &args, ExpressionState &state, Vector &result);
    static void Quadbin_cell_to_tile_x(DataChunk &args, ExpressionState &state, Vector &result);
    static void Quadbin_cell_to_tile_y(DataChunk &args, ExpressionState &state, Vector &result);
    static void Quadbin_cell_to_tile_z(DataChunk &args, ExpressionState &state, Vector &result);
    static void Quadbin_get_resolution(DataChunk &args, ExpressionState &state, Vector &result);
    static void Quadbin_is_valid_cell(DataChunk &args, ExpressionState &state, Vector &result);

    /* Constructor */
    static void Tquadbin_make(DataChunk &args, ExpressionState &state, Vector &result);

    /* Accessors (startValue/endValue are generated — see src/generated) */
    static void Tquadbin_value_n(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tquadbin_values(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tquadbin_value_at_timestamptz(DataChunk &args, ExpressionState &state, Vector &result);

    /* Quadkey conversion */
    static void Tquadbin_cell_to_quadkey(DataChunk &args, ExpressionState &state, Vector &result);

    /* Casts to/from tbigint */
    static void Tbigint_to_tquadbin(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tquadbin_to_tbigint(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
