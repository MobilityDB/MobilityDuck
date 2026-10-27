#include "geo/tgeometry.hpp"
#include "duckdb_version_compat.hpp"
#include "geo/tgeompoint_functions.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include <regex>
#include <string>
#include <temporal/span.hpp>
#include "temporal/spanset.hpp"
#include "temporal/set.hpp"
#include "temporal/temporal_functions.hpp"
#include "geo/stbox.hpp"
#include "geo/geoset.hpp"
#include<time_util.hpp>
#include "geo_util.hpp"
#include "mobilityduck/meos_registration.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_geo.h>
    #include <meos_internal.h>
    #include <meos_internal_geo.h>
}


namespace duckdb {

LogicalType TGeometryTypes::tgeometry() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("tgeometry");
    return type;
}

/*
 * Constructors
*/








/*
 * Accessor Functions
*/















void TGeometryTypes::RegisterScalarFunctions(ExtensionLoader &loader) {
    // timeSpan(tgeometry), tgeometryInst(tgeometry) and shiftScaleTime(tgeometry, interval,
    // interval) are generated from the catalog (meos_temporal_conversion, meos_temporal_transf).
}

void TGeometryTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType( "tgeometry", TGeometryTypes::tgeometry());
}


}
