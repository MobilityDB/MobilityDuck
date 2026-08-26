#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

/* trgeometry is a temporal rigid geometry: a reference geometry carried
 * unchanged plus a temporal pose placing it, so its base value is `pose`
 * (meos_catalog.c: [T_TRGEOMETRY].temptype_basetype = T_POSE) and the pose
 * type the PoseTypes family registers is the one its accessors answer in.
 * Canonical name per meos_catalog.c ([T_TRGEOMETRY]="trgeometry"); `trgeo`
 * is the file/helper abbreviation only, never the user-visible name.
 *
 * The reference geometry is appended at the END of the varlena, and
 * `VARSIZE` — which `temporal_mem_size` returns — spans it, so the temporal
 * blob marshaller carries the geometry with the value.  Its text I/O is
 * `trgeometry_out`, NOT the generic `temporal_out`: an op that walks only
 * the pose skeleton drops the appended geometry.
 *
 * This binding registers only the type + text I/O boundary; the operation
 * surface is generated from the MEOS-API catalog into src/generated/. */
struct TrgeometryTypes {
    static LogicalType trgeometry();

    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
};

struct TrgeometryFunctions {
    static bool Trgeometry_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Trgeometry_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
};

} // namespace duckdb
