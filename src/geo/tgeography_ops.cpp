// Cross-type predicate registrations for tgeography. The MEOS C functions
// dispatched here all take Temporal * (subtype-agnostic), so this file is
// purely registration-and-glue; the heavy lifting stays in libmeos.

#include "meos_wrapper_simple.hpp"
#include "duckdb_version_compat.hpp"

#include "common.hpp"
#include "geo/tgeography.hpp"
#include "geo/tgeography_ops.hpp"
#include "geo/tgeometry.hpp"
#include "geo/tgeompoint_functions.hpp"
#include "geo/stbox.hpp"
#include "temporal/span.hpp"
#include "temporal/temporal.hpp"
#include "geo_util.hpp"
#include "time_util.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "spatial/spatial_types.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_geo.h>
    #include <meos_internal.h>
    #include <meos_internal_geo.h>
}

namespace duckdb {

namespace {

// ====================================================================
// Argument-decoding helpers
// ====================================================================

inline Temporal *DecodeTemporalCopy(string_t blob) {
    size_t sz = blob.GetSize();
    Temporal *t = (Temporal *) malloc(sz);
    memcpy(t, blob.GetData(), sz);
    return t;
}

// ====================================================================
// Spatial functions — SRID accessor, setSRID, transform, expand*,
// round, traversedArea, centroid, convexHull, and the
// tgeography <-> tgeogpoint coercions.
// ====================================================================

inline string_t StboxToBlob(Vector &result, STBox *box) {
    string_t out = StringVector::AddStringOrBlob(
        result, reinterpret_cast<const char *>(box), sizeof(STBox));
    free(box);
    return out;
}

void TspatialToStboxExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t blob) {
            Temporal *t = DecodeTemporalCopy(blob);
            STBox *box = tspatial_to_stbox(t);
            free(t);
            return StboxToBlob(result, box);
        });
}

} // namespace

void TGeographyOps::RegisterScalarFunctions(ExtensionLoader &loader) {
    const LogicalType TGEOM = TGeographyTypes::tgeography();
    const LogicalType GEOM  = MobilityDuckGeometryType();
    const LogicalType stbox = StboxType::stbox();
    const LogicalType tstzspan = SpanTypes::tstzspan();
    const LogicalType BOOL = LogicalType::BOOLEAN;
    const LogicalType DBL = LogicalType::DOUBLE;
    const LogicalType tfloat = TemporalTypes::tfloat();

    // Time-axis position predicates on tgeography × tstzspan (both directions;
    // before/after/overbefore/overafter, backing fns before_temporal_tstzspan etc.
    // @ingroup meos_temporal_bbox_pos) are generated from the catalog sqlSignatures
    // in src/generated/generated_temporal_udfs.cpp. The stale-snake temporal_*
    // aliases are dropped (bare names supersede them).


    // tgeography → stbox is a cast in the SQL surface; expose it as a
    // function for now to keep the implementation a single template.
    RegisterMeosFunction(loader, ScalarFunction(
        "stbox", {TGEOM}, stbox, TspatialToStboxExec));

    // Centroid / convexHull / traversedArea — produce a non-temporal
    // geometry summary of the trajectory.
}

} // namespace duckdb
