#include "meos_wrapper_simple.hpp"

#include "common.hpp"
#include "geo/stbox.hpp"
#include "geo/stbox_functions.hpp"

#include "duckdb/common/types/blob.hpp"
#include "duckdb/function/function.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include <scoped_allocator>
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

    ExtensionUtil::RegisterCastFunction(
        instance,
        LogicalType::TIMESTAMP_TZ,
        STBOX(),
        StboxFunctions::Timestamptz_to_stbox_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        SetTypes::tstzset(),
        STBOX(),
        StboxFunctions::Tstzset_to_stbox_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        SpanTypes::TSTZSPAN(),
        STBOX(),
        StboxFunctions::Tstzspan_to_stbox_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        SpansetTypes::tstzspanset(),
        STBOX(),
        StboxFunctions::Tstzspanset_to_stbox_cast
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
            {LogicalType::TIMESTAMP_TZ},
            StboxType::STBOX(),
            StboxFunctions::Timestamptz_to_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox",
            {SetTypes::tstzset()},
            StboxType::STBOX(),
            StboxFunctions::Tstzset_to_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox",
            {SpanTypes::TSTZSPAN()},
            StboxType::STBOX(),
            StboxFunctions::Tstzspan_to_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox",
            {SpansetTypes::tstzspanset()},
            StboxType::STBOX(),
            StboxFunctions::Tstzspanset_to_stbox
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
            "hasX",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_hasx
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "hasZ",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_hasz
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "hasT",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_hast
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "isGeodetic",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_isgeodetic
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "Xmin",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_xmin
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "Ymin",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_ymin
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "Zmin",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_zmin
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "Tmin",
            {STBOX()},
            LogicalType::TIMESTAMP_TZ,
            StboxFunctions::Stbox_tmin
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "TminInc",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_tmin_inc
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "Xmax",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_xmax
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "Ymax",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_ymax
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "Zmax",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_zmax
        )
    );
 
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "Tmax",
            {STBOX()},
            LogicalType::TIMESTAMP_TZ,
            StboxFunctions::Stbox_tmax
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "TmaxInc",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_tmax_inc
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
            "volume",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_volume
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "shiftTime",
            {STBOX(), LogicalType::INTERVAL},
            STBOX(),
            StboxFunctions::Stbox_shift_time
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "scaleTime",
            {STBOX(), LogicalType::INTERVAL},
            STBOX(),
            StboxFunctions::Stbox_scale_time
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "shiftScaleTime",
            {STBOX(), LogicalType::INTERVAL, LogicalType::INTERVAL},
            STBOX(),
            StboxFunctions::Stbox_shift_scale_time
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "getSpace",
            {STBOX()},
            STBOX(),
            StboxFunctions::Stbox_get_space
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "expandTime",
            {STBOX(), LogicalType::INTERVAL},
            STBOX(),
            StboxFunctions::Stbox_expand_time
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
            "stbox_contains",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contains_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox_contained",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contained_stbox_stbox
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox_overlaps",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overlaps_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox_same",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Same_stbox_stbox
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox_adjacent",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Adjacent_stbox_stbox
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "@>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contains_stbox_stbox
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "<@",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contained_stbox_stbox
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
            "~=",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Same_stbox_stbox
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "-|-",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Adjacent_stbox_stbox
        )
    );

        ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox_left",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Left_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox_overleft",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overleft_stbox_stbox
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox_right",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Right_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox_overright",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overright_stbox_stbox
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox_below",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Below_stbox_stbox
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox_overbelow",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overbelow_stbox_stbox
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox_above",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Above_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox_overabove",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overabove_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox_before",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Before_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox_overbefore",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overbefore_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox_after",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::After_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox_overafter",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overafter_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox_front",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Front_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox_overfront",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overfront_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox_back",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Back_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "stbox_overback",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overback_stbox_stbox
        )
    );

        ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "<<",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Left_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "&<",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overleft_stbox_stbox
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            ">>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Right_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "&>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overright_stbox_stbox
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "<<|",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Below_stbox_stbox
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "&<|",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overbelow_stbox_stbox
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "|>>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Above_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "|&>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overabove_stbox_stbox
        )
    );
// # is not a operator in Duckdb, fix later
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "<<#",  
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Before_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "&<#",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overbefore_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "#>>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::After_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "#&>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overafter_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "<</",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Front_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "&</",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overfront_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "/>>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Back_stbox_stbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "/&>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overback_stbox_stbox
        )
    );
    
}

} // namespace duckdb