#include "geo/tgeography.hpp"
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

LogicalType TGeographyTypes::tgeography() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("tgeography");
    return type;
}

/*
 * Constructors
*/








/*
 * Accessor Functions
*/















void TGeographyTypes::RegisterScalarFunctions(ExtensionLoader &loader) {
    // timeSpan(tgeography), tgeographyInst(tgeography) and shiftScaleTime(tgeography, interval,
    // interval) are generated from the catalog (meos_temporal_conversion, meos_temporal_transf).
}

void TGeographyTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType( "tgeography", TGeographyTypes::tgeography());
}


}
