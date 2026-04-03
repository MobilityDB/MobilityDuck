#include "meos_wrapper_simple.hpp"

#include "common.hpp"
#include "geo/stbox.hpp"
#include "geo/stbox_functions.hpp"

#include "duckdb/common/types/blob.hpp"
#include "duckdb/function/function.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "spatial/spatial_types.hpp"

namespace duckdb {

LogicalType StboxType::STBOX() {
    LogicalType type(LogicalTypeId::BLOB);
    type.SetAlias("STBOX");
    return type;
}

void StboxType::RegisterType(DatabaseInstance &instance) {
    ExtensionUtil::RegisterType(instance, "STBOX", STBOX());
}

void StboxType::RegisterCastFunctions(DatabaseInstance &instance) {
    ExtensionUtil::RegisterCastFunction(
        instance,
        LogicalType::VARCHAR,
        STBOX(),
        StboxFunctions::Stbox_in_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        STBOX(),
        LogicalType::VARCHAR,
        StboxFunctions::Stbox_out
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        GeoTypes::GEOMETRY(),
        STBOX(),
        StboxFunctions::Geo_to_stbox_cast
    );
}

void StboxType::RegisterScalarFunctions(DatabaseInstance &instance) {
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction("stbox", {LogicalType::VARCHAR}, STBOX(), StboxFunctions::Stbox_in, nullptr, nullptr, nullptr,
                     nullptr, LogicalType(LogicalTypeId::INVALID), FunctionStability::VOLATILE));

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stboxFromBinary",
            {LogicalType::BLOB},
            STBOX(),
            StboxFunctions::Stbox_from_wkb
        )
    );

    // ExtensionUtil::RegisterFunction(
    //     instance,
    //     ScalarFunction(
    //         "stboxFromHexWKB",
    //         {LogicalType::VARCHAR},
    //         STBOX(),
    //         StboxFunctions::Stbox_from_hexwkb
    //     )
    // );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "asText",
            {STBOX()},
            LogicalType::VARCHAR,
            StboxFunctions::Stbox_as_text
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "asBinary",
            {STBOX()},
            LogicalType::BLOB,
            StboxFunctions::Stbox_as_wkb
        )
    );

    // ExtensionUtil::RegisterFunction(
    //     instance,
    //     ScalarFunction(
    //         "asHexWKB",
    //         {STBOX()},
    //         LogicalType::VARCHAR,
    //         StboxFunctions::Stbox_as_hexwkb
    //     )
    // );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox",
            {GeoTypes::GEOMETRY(), LogicalType::TIMESTAMP_TZ},
            StboxType::STBOX(),
            StboxFunctions::Geo_timestamptz_to_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox",
            {GeoTypes::GEOMETRY(), SpanTypes::TSTZSPAN()},
            StboxType::STBOX(),
            StboxFunctions::Geo_tstzspan_to_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox",
            {GeoTypes::GEOMETRY()},
            StboxType::STBOX(),
            StboxFunctions::Geo_to_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "geometry",
            {STBOX()},
            GeoTypes::GEOMETRY(),
            StboxFunctions::Stbox_to_geo
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "area",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_area
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "expandSpace",
            {STBOX(), LogicalType::DOUBLE},
            STBOX(),
            StboxFunctions::Stbox_expand_space
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "&&",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overlaps_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "@>", // contains
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contains_stbox_stbox
        )
    );
}

} // namespace duckdb