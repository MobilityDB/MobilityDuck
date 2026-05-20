#pragma once

// MobilityDuck `GEOGRAPHY` LogicalType — the static, geodetic counterpart to
// DuckDB Spatial's `GEOMETRY`. See `doc/geography-boundary.md` for the full
// boundary design.
//
// This header declares only the LogicalType registration entry point. Casts
// (GEOMETRY ⇄ GEOGRAPHY, GEOGRAPHY ⇄ TGEOGPOINT) and I/O UDFs
// (ST_GeogFromText, ST_AsText, ST_AsBinary, ST_GeogFromBinary) land in
// follow-up PRs as scaffolded in the boundary doc.

#include "common.hpp"
#include "duckdb/common/types.hpp"

#include "meos_wrapper_simple.hpp"

namespace duckdb {

class ExtensionLoader;

struct GeographyType {
    // LogicalType alias for the static geodetic geography. The payload is a
    // BLOB whose bytes are MEOS-WKB with the geodetic flag preserved in the
    // type tag. The alias name `GEOGRAPHY` makes
    //
    //     SELECT geography 'POINT(4.35 50.85)'
    //
    // parse, and lets adopters declare columns as `column_name GEOGRAPHY`.
    static LogicalType GEOGRAPHY();

    static void RegisterType(ExtensionLoader &loader);
};

} // namespace duckdb
