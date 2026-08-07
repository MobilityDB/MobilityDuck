#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

/* RAQUET is a Web-Mercator raster tile addressed by a CARTO QUADBIN cell: the
 * tile carries its pixel band plus the metadata (cell, width, height, pixel
 * type, nodata) needed to georeference it, and its geotransform is derived
 * from the cell.  It is surfaced as a BLOB holding the MEOS WKB serialisation
 * of the tile, the same wire form its text representation hex-encodes, so the
 * value round-trips through Parquet and through the VARCHAR casts. */
struct RaquetTypes {
    static LogicalType raquet();

    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
    static void RegisterScalarFunctions(ExtensionLoader &loader);
};

struct RaquetFunctions {
    /* In/out */
    static bool Raquet_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Raquet_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* Constructors */
    static void Raquet_constructor(DataChunk &args, ExpressionState &state, Vector &result);
    static void Raquet_read(DataChunk &args, ExpressionState &state, Vector &result);

    /* Accessors */
    static void Raquet_quadbin(DataChunk &args, ExpressionState &state, Vector &result);
    static void Raquet_width(DataChunk &args, ExpressionState &state, Vector &result);
    static void Raquet_height(DataChunk &args, ExpressionState &state, Vector &result);
    static void Raquet_nodata(DataChunk &args, ExpressionState &state, Vector &result);
    static void Raquet_pixtype(DataChunk &args, ExpressionState &state, Vector &result);
    static void Raquet_to_stbox(DataChunk &args, ExpressionState &state, Vector &result);

    /* Comparisons */
    static void Raquet_eq(DataChunk &args, ExpressionState &state, Vector &result);
    static void Raquet_ne(DataChunk &args, ExpressionState &state, Vector &result);
    static void Raquet_lt(DataChunk &args, ExpressionState &state, Vector &result);
    static void Raquet_le(DataChunk &args, ExpressionState &state, Vector &result);
    static void Raquet_ge(DataChunk &args, ExpressionState &state, Vector &result);
    static void Raquet_gt(DataChunk &args, ExpressionState &state, Vector &result);
    static void Raquet_cmp(DataChunk &args, ExpressionState &state, Vector &result);
    static void Raquet_hash(DataChunk &args, ExpressionState &state, Vector &result);
    static void Raquet_hash_extended(DataChunk &args, ExpressionState &state, Vector &result);

    /* Sampling a raster file along a trajectory, through GDAL */
    static void Raster_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Raster_at_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Raster_minus_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Eraster_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Araster_value(DataChunk &args, ExpressionState &state, Vector &result);

    /* Sampling along a trajectory */
    static void Raster_tile_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Raster_tile_value_array(DataChunk &args, ExpressionState &state, Vector &result);
    static void Raster_tile_value_quadbin(DataChunk &args, ExpressionState &state, Vector &result);
    static void Trajectory_quadbins(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
