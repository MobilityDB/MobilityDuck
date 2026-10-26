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
};

struct RaquetFunctions {
    /* In/out */
    static bool Raquet_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Raquet_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
};

} // namespace duckdb
