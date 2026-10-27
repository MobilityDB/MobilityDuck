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
#include "mobilityduck/meos_registration.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_geo.h>
    #include <meos_internal.h>
    #include <meos_internal_geo.h>
}

namespace duckdb {

void TGeographyOps::RegisterScalarFunctions(ExtensionLoader &loader) {
    // The time-axis position predicates on tgeography x tstzspan are generated from the catalog
    // sqlSignatures (@ingroup meos_temporal_bbox_pos), and so is stbox(tgeography)
    // (tspatial_to_stbox, meos_geo_conversion).
}

} // namespace duckdb
