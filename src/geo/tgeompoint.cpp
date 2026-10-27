#include "meos_wrapper_simple.hpp"
#include "duckdb_version_compat.hpp"

#include "common.hpp"
#include "geo/tgeompoint.hpp"
#include "geo/tgeompoint_functions.hpp"
#include "geo/geoset.hpp"
#include "temporal/temporal_functions.hpp"
#include "geo/stbox.hpp"
#include "temporal/spanset.hpp"
#include "temporal/temporal.hpp"
#include "temporal/set.hpp"
#include "temporal/span.hpp"

#include "duckdb/common/types/blob.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "mobilityduck/meos_registration.hpp"
#include "geo_util.hpp"
#include "time_util.hpp"

namespace duckdb {

LogicalType TgeompointType::tgeompoint() {
    LogicalType type(LogicalTypeId::BLOB);
    type.SetAlias("tgeompoint");
    return type;
}

void TgeompointType::RegisterType(ExtensionLoader &loader) {
    loader.RegisterType( "tgeompoint", tgeompoint());
}

void TgeompointType::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, 
        LogicalType::VARCHAR,
        tgeompoint(),
        TgeompointFunctions::Tpoint_in
    );

    RegisterMeosCastFunction(loader, 
        tgeompoint(),
        LogicalType::VARCHAR,
        TemporalFunctions::Temporal_out
    );

    RegisterMeosCastFunction(loader, 
        tgeompoint(),
        StboxType::stbox(),
        TgeompointFunctions::Tspatial_to_stbox_cast
    );

    RegisterMeosCastFunction(loader, 
        tgeompoint(),
        SpanTypes::tstzspan(),
        TgeompointFunctions::Temporal_to_tstzspan_cast
    );
}

void TgeompointType::RegisterScalarFunctions(ExtensionLoader &loader) {

    /* ***************************************************
     * In/out functions
     ****************************************************/

    const auto varchar_list = LogicalType::LIST(LogicalType::VARCHAR);

    duckdb::RegisterMeosFunction(loader, 
        ScalarFunction(
            "asText",
            {LogicalType::LIST(tgeompoint())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    duckdb::RegisterMeosFunction(loader, 
        ScalarFunction(
            "asText",
            {LogicalType::LIST(tgeompoint()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    duckdb::RegisterMeosFunction(loader, 
        ScalarFunction(
            "asText",
            {LogicalType::LIST(MobilityDuckGeometryType())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    duckdb::RegisterMeosFunction(loader, 
        ScalarFunction(
            "asText",
            {LogicalType::LIST(MobilityDuckGeometryType()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );

    duckdb::RegisterMeosFunction(loader, 
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(tgeompoint())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    duckdb::RegisterMeosFunction(loader, 
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(tgeompoint()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    duckdb::RegisterMeosFunction(loader, 
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(MobilityDuckGeometryType())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    duckdb::RegisterMeosFunction(loader, 
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(MobilityDuckGeometryType()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );

    // stbox(tgeompoint), shiftScaleTime(tgeompoint, interval, interval), rotate and scale over
    // tgeompoint, and collect(geometry[]) and distance(geometry, geometry), the native geometry
    // operations, are generated from the catalog (meos_geo_conversion, meos_temporal_transf,
    // meos_geo_transf, meos_geo_base_spatial, meos_geo_base_dist).
}

} // namespace duckdb
