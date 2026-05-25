#pragma once

// MobilityDuck I/O UDFs for the `GEOGRAPHY` LogicalType.  See
// `doc/geography-boundary.md` for the boundary design.  Each UDF is a thin
// shim over a MEOS export — the binding owns only the type conversion to
// and from the DuckDB columnar layout, never the geodetic semantics.

#include "common.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/function/scalar_function.hpp"

#include "meos_wrapper_simple.hpp"

namespace duckdb {

class ExtensionLoader;

struct GeographyFunctions {
    // VARCHAR -> GEOGRAPHY: MEOS `geog_in(text, typmod)`.  Stores the
    // resulting GSERIALIZED bytes in the GEOGRAPHY BLOB so the geodetic
    // flag in the type tag survives the boundary.
    static void ST_GeogFromText(DataChunk &args, ExpressionState &state, Vector &result);

    // GEOGRAPHY -> VARCHAR: MEOS `geo_as_ewkt(gs, precision)`.  Output
    // carries the SRID prefix so the round-trip through `ST_GeogFromText`
    // re-sets the geodetic flag.
    static void ST_AsText(DataChunk &args, ExpressionState &state, Vector &result);

    // GEOGRAPHY -> BLOB: MEOS `geo_as_wkb(gs, endian, &size)`.  Output is
    // standard EWKB (SRID-prefixed but without MEOS's geodetic flag) — a
    // round-trip via `ST_GeogFromBinary` re-asserts geodetic-ness from
    // the SRID.
    static void ST_AsBinary(DataChunk &args, ExpressionState &state, Vector &result);

    // BLOB -> GEOGRAPHY: MEOS `geo_from_ewkb(wkb, size, srid)`.  Re-sets
    // the geodetic flag explicitly so the round-trip through standard
    // EWKB does not lose it.
    static void ST_GeogFromBinary(DataChunk &args, ExpressionState &state, Vector &result);

    // GEOMETRY -> GEOGRAPHY cast: read sgl GEOMETRY, lift to GSERIALIZED,
    // re-flag geodetic, store as GEOGRAPHY BLOB.
    static bool Geometry_to_geography_cast(Vector &source, Vector &result,
                                           idx_t count, CastParameters &parameters);

    // GEOGRAPHY -> GEOMETRY cast: read GSERIALIZED from BLOB, clear the
    // geodetic flag, emit sgl GEOMETRY via the existing helper.
    static bool Geography_to_geometry_cast(Vector &source, Vector &result,
                                           idx_t count, CastParameters &parameters);

    // Scalar geodetic operations — all thin shims over MEOS exports.

    // GEOGRAPHY -> DOUBLE: MEOS `geog_length(gs, use_spheroid=true)`.
    static void ST_Length(DataChunk &args, ExpressionState &state, Vector &result);

    // GEOGRAPHY -> DOUBLE: MEOS `geog_area(gs, use_spheroid=true)`.
    static void ST_Area(DataChunk &args, ExpressionState &state, Vector &result);

    // (TGEOGPOINT, GEOGRAPHY) -> BOOLEAN: MEOS `eintersects_tgeo_geo`.
    static void EIntersects_tgeo_geog(DataChunk &args, ExpressionState &state, Vector &result);

    // (TGEOGPOINT, GEOGRAPHY) -> DOUBLE: MEOS `nad_tgeo_geo`.
    static void NAD_tgeo_geog(DataChunk &args, ExpressionState &state, Vector &result);

    static void RegisterScalarFunctions(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
};

} // namespace duckdb
