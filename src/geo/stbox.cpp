#include "meos_wrapper_simple.hpp"

#include "common.hpp"
#include "geo/stbox.hpp"
#include "geo/stbox_functions.hpp"

#include "duckdb/common/types/blob.hpp"
#include "duckdb/function/function.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include <scoped_allocator>
#include "spatial/spatial_types.hpp"

namespace duckdb {

LogicalType StboxType::STBOX() {
    LogicalType type(LogicalTypeId::BLOB);
    type.SetAlias("STBOX");
    return type;
}

void StboxType::RegisterType(ExtensionLoader &loader) {
    loader.RegisterType( "STBOX", STBOX());
}

void StboxType::RegisterCastFunctions(ExtensionLoader &loader) {
    loader.RegisterCastFunction(
        LogicalType::VARCHAR,
        STBOX(),
        StboxFunctions::Stbox_in_cast
    );

    loader.RegisterCastFunction(
        STBOX(),
        LogicalType::VARCHAR,
        StboxFunctions::Stbox_out
    );

    loader.RegisterCastFunction(
        GeoTypes::GEOMETRY(),
        STBOX(),
        StboxFunctions::Geo_to_stbox_cast
    );

    loader.RegisterCastFunction(
        LogicalType::TIMESTAMP_TZ,
        STBOX(),
        StboxFunctions::Timestamptz_to_stbox_cast
    );

    loader.RegisterCastFunction(
        SetTypes::tstzset(),
        STBOX(),
        StboxFunctions::Tstzset_to_stbox_cast
    );

    loader.RegisterCastFunction(
        SpanTypes::TSTZSPAN(),
        STBOX(),
        StboxFunctions::Tstzspan_to_stbox_cast
    );

    loader.RegisterCastFunction(
        SpansetTypes::tstzspanset(),
        STBOX(),
        StboxFunctions::Tstzspanset_to_stbox_cast
    );
}

void StboxType::RegisterScalarFunctions(ExtensionLoader &loader) {
    loader.RegisterFunction(
        ScalarFunction("stbox", {LogicalType::VARCHAR}, STBOX(), StboxFunctions::Stbox_in, nullptr, nullptr, nullptr,
                     nullptr, LogicalType(LogicalTypeId::INVALID), FunctionStability::VOLATILE));

    loader.RegisterFunction(
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

    loader.RegisterFunction(
        ScalarFunction(
            "asText",
            {STBOX()},
            LogicalType::VARCHAR,
            StboxFunctions::Stbox_as_text
        )
    );

    loader.RegisterFunction(
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

    loader.RegisterFunction(
        ScalarFunction(
            "stbox",
            {GeoTypes::GEOMETRY(), LogicalType::TIMESTAMP_TZ},
            StboxType::STBOX(),
            StboxFunctions::Geo_timestamptz_to_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox",
            {GeoTypes::GEOMETRY(), SpanTypes::TSTZSPAN()},
            StboxType::STBOX(),
            StboxFunctions::Geo_tstzspan_to_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox",
            {LogicalType::TIMESTAMP_TZ},
            StboxType::STBOX(),
            StboxFunctions::Timestamptz_to_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox",
            {SetTypes::tstzset()},
            StboxType::STBOX(),
            StboxFunctions::Tstzset_to_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox",
            {SpanTypes::TSTZSPAN()},
            StboxType::STBOX(),
            StboxFunctions::Tstzspan_to_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox",
            {SpansetTypes::tstzspanset()},
            StboxType::STBOX(),
            StboxFunctions::Tstzspanset_to_stbox
        )
    );
    

    loader.RegisterFunction(
        ScalarFunction(
            "stbox",
            {GeoTypes::GEOMETRY()},
            StboxType::STBOX(),
            StboxFunctions::Geo_to_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "geometry",
            {STBOX()},
            GeoTypes::GEOMETRY(),
            StboxFunctions::Stbox_to_geo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "hasX",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_hasx
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "hasZ",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_hasz
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "hasT",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_hast
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "isGeodetic",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_isgeodetic
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "Xmin",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_xmin
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "Ymin",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_ymin
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "Zmin",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_zmin
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "Tmin",
            {STBOX()},
            LogicalType::TIMESTAMP_TZ,
            StboxFunctions::Stbox_tmin
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "TminInc",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_tmin_inc
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "Xmax",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_xmax
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "Ymax",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_ymax
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "Zmax",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_zmax
        )
    );
 
    loader.RegisterFunction(
        ScalarFunction(
            "Tmax",
            {STBOX()},
            LogicalType::TIMESTAMP_TZ,
            StboxFunctions::Stbox_tmax
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "TmaxInc",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_tmax_inc
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "area",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_area
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "volume",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_volume
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "shiftTime",
            {STBOX(), LogicalType::INTERVAL},
            STBOX(),
            StboxFunctions::Stbox_shift_time
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "scaleTime",
            {STBOX(), LogicalType::INTERVAL},
            STBOX(),
            StboxFunctions::Stbox_scale_time
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "shiftScaleTime",
            {STBOX(), LogicalType::INTERVAL, LogicalType::INTERVAL},
            STBOX(),
            StboxFunctions::Stbox_shift_scale_time
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "getSpace",
            {STBOX()},
            STBOX(),
            StboxFunctions::Stbox_get_space
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "expandTime",
            {STBOX(), LogicalType::INTERVAL},
            STBOX(),
            StboxFunctions::Stbox_expand_time
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "expandSpace",
            {STBOX(), LogicalType::DOUBLE},
            STBOX(),
            StboxFunctions::Stbox_expand_space
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_contains",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contains_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_contained",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contained_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_overlaps",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overlaps_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_same",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Same_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_adjacent",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Adjacent_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "@>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contains_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "<@",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contained_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "&&",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overlaps_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "~=",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Same_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "-|-",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Adjacent_stbox_stbox
        )
    );

        loader.RegisterFunction(
        ScalarFunction(
            "stbox_left",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Left_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_overleft",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overleft_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_right",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Right_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_overright",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overright_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_below",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Below_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_overbelow",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overbelow_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_above",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Above_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_overabove",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overabove_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_before",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Before_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_overbefore",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overbefore_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_after",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::After_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_overafter",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overafter_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_front",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Front_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_overfront",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overfront_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_back",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Back_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_overback",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overback_stbox_stbox
        )
    );

        loader.RegisterFunction(
        ScalarFunction(
            "<<",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Left_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "&<",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overleft_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            ">>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Right_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "&>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overright_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "<<|",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Below_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "&<|",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overbelow_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "|>>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Above_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "|&>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overabove_stbox_stbox
        )
    );
// # is not a operator in Duckdb, fix later
    loader.RegisterFunction(
        ScalarFunction(
            "<<#",  
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Before_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "&<#",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overbefore_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "#>>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::After_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "#&>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overafter_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "<</",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Front_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "&</",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overfront_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "/>>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Back_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "/&>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overback_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_union",
            {STBOX(), STBOX()},
            STBOX(),
            StboxFunctions::Union_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_intersection",
            {STBOX(), STBOX()},
            STBOX(),
            StboxFunctions::Intersection_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "+",
            {STBOX(), STBOX()},
            STBOX(),
            StboxFunctions::Union_stbox_stbox
        )
    );
    
    loader.RegisterFunction(
        ScalarFunction(
            "*",
            {STBOX(), STBOX()},
            STBOX(),
            StboxFunctions::Intersection_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_cmp",
            {STBOX(), STBOX()},
            LogicalType::INTEGER,
            StboxFunctions::Stbox_cmp
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_eq",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_eq
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_ne",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_ne
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_lt",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_lt
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_le",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_le
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_ge",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_ge
        )
    );  
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_gt",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_gt
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "=",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_eq
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "<>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_ne
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "<",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_lt
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "<=",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_le
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            ">=",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_ge
        )
    );  
    loader.RegisterFunction(
        ScalarFunction(
            ">",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_gt
        )
    );
}

} // namespace duckdb