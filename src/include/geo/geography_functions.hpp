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

    // GEOGRAPHY -> BLOB: MEOS `geo_as_ewkb(gs, endian, &size)`.  Output is
    // standard EWKB (SRID-prefixed but without MEOS's geodetic flag) — a
    // round-trip via `ST_GeogFromBinary` re-asserts geodetic-ness from
    // the SRID.
    static void ST_AsBinary(DataChunk &args, ExpressionState &state, Vector &result);

    // BLOB -> GEOGRAPHY: MEOS `geo_from_ewkb(wkb, size, srid)`.  Re-sets
    // the geodetic flag explicitly so the round-trip through standard
    // EWKB does not lose it.
    static void ST_GeogFromBinary(DataChunk &args, ExpressionState &state, Vector &result);

    static void RegisterScalarFunctions(ExtensionLoader &loader);
};

} // namespace duckdb
