#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

/* npoint is a network point (a route identifier + a fractional position); a
 * varlena value surfaced as a BLOB.  nsegment is a network segment (route +
 * two positions).  tnpoint is the temporal network point, stored as a
 * Temporal* blob.  Canonical names per meos_catalog.c ([T_NPOINT]="npoint" :138,
 * [T_NSEGMENT]="nsegment" :140, [T_TNPOINT]="tnpoint" :141).  npoint is the
 * 2D-planar twin of cbuffer: both are Spatial<T> base values, non-geodetic.
 * npoint carries NO SRID of its own — npoint_srid() returns get_srid_ways(), so
 * the `ways` network CSV must be loaded (meos_set_ways_csv) for construction and
 * geometry ops to resolve.  This binding registers only the type + text I/O
 * boundary; the operation surface (constructors, accessors, predicates,
 * distance, …) is generated from the MEOS-API catalog into src/generated/. */
struct NpointTypes {
    static LogicalType npoint();
    static LogicalType nsegment();
    static LogicalType tnpoint();

    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
};

struct NpointFunctions {
    /* In/out — static npoint value */
    static bool Npoint_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Npoint_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* In/out — static nsegment value */
    static bool Nsegment_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Nsegment_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* In/out — tnpoint temporal value */
    static bool Tnpoint_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tnpoint_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
};

} // namespace duckdb
