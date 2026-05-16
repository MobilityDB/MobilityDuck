#include "meos_wrapper_simple.hpp"

#include "common.hpp"
#include "geo/stbox.hpp"
#include "geo/stbox_functions.hpp"
#include "geo/tgeompoint.hpp"

#include "duckdb/common/types/blob.hpp"
#include "duckdb/function/function.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include <scoped_allocator>
#include "spatial/spatial_types.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

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
    RegisterMeosCastFunction(loader,
        LogicalType::VARCHAR,
        STBOX(),
        StboxFunctions::Stbox_in_cast
    );

    RegisterMeosCastFunction(loader,
        STBOX(),
        LogicalType::VARCHAR,
        StboxFunctions::Stbox_out
    );

    RegisterMeosCastFunction(loader,
        GeoTypes::GEOMETRY(),
        STBOX(),
        StboxFunctions::Geo_to_stbox_cast
    );

    RegisterMeosCastFunction(loader,
        LogicalType::TIMESTAMP_TZ,
        STBOX(),
        StboxFunctions::Timestamptz_to_stbox_cast
    );

    RegisterMeosCastFunction(loader,
        SetTypes::tstzset(),
        STBOX(),
        StboxFunctions::Tstzset_to_stbox_cast
    );

    RegisterMeosCastFunction(loader,
        SpanTypes::TSTZSPAN(),
        STBOX(),
        StboxFunctions::Tstzspan_to_stbox_cast
    );

    RegisterMeosCastFunction(loader,
        SpansetTypes::tstzspanset(),
        STBOX(),
        StboxFunctions::Tstzspanset_to_stbox_cast
    );
}

void StboxType::RegisterScalarFunctions(ExtensionLoader &loader) {
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("stbox", {LogicalType::VARCHAR}, STBOX(), StboxFunctions::Stbox_in, nullptr, nullptr, nullptr,
                     nullptr, LogicalType(LogicalTypeId::INVALID), FunctionStability::VOLATILE));

    duckdb::RegisterSerializedScalarFunction(loader, 
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

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asText",
            {STBOX()},
            LogicalType::VARCHAR,
            StboxFunctions::Stbox_as_text
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
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

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox",
            {GeoTypes::GEOMETRY(), LogicalType::TIMESTAMP_TZ},
            StboxType::STBOX(),
            StboxFunctions::Geo_timestamptz_to_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox",
            {GeoTypes::GEOMETRY(), SpanTypes::TSTZSPAN()},
            StboxType::STBOX(),
            StboxFunctions::Geo_tstzspan_to_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox",
            {LogicalType::TIMESTAMP_TZ},
            StboxType::STBOX(),
            StboxFunctions::Timestamptz_to_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox",
            {SetTypes::tstzset()},
            StboxType::STBOX(),
            StboxFunctions::Tstzset_to_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox",
            {SpanTypes::TSTZSPAN()},
            StboxType::STBOX(),
            StboxFunctions::Tstzspan_to_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox",
            {SpansetTypes::tstzspanset()},
            StboxType::STBOX(),
            StboxFunctions::Tstzspanset_to_stbox
        )
    );
    

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox",
            {GeoTypes::GEOMETRY()},
            StboxType::STBOX(),
            StboxFunctions::Geo_to_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "geometry",
            {STBOX()},
            GeoTypes::GEOMETRY(),
            StboxFunctions::Stbox_to_geo
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "hasX",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_hasx
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "hasZ",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_hasz
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "hasT",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_hast
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "isGeodetic",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_isgeodetic
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Xmin",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_xmin
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Ymin",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_ymin
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Zmin",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_zmin
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Tmin",
            {STBOX()},
            LogicalType::TIMESTAMP_TZ,
            StboxFunctions::Stbox_tmin
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "TminInc",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_tmin_inc
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Xmax",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_xmax
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Ymax",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_ymax
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Zmax",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_zmax
        )
    );
 
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Tmax",
            {STBOX()},
            LogicalType::TIMESTAMP_TZ,
            StboxFunctions::Stbox_tmax
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "TmaxInc",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_tmax_inc
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "area",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_area
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "volume",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_volume
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "shiftTime",
            {STBOX(), LogicalType::INTERVAL},
            STBOX(),
            StboxFunctions::Stbox_shift_time
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "scaleTime",
            {STBOX(), LogicalType::INTERVAL},
            STBOX(),
            StboxFunctions::Stbox_scale_time
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "shiftScaleTime",
            {STBOX(), LogicalType::INTERVAL, LogicalType::INTERVAL},
            STBOX(),
            StboxFunctions::Stbox_shift_scale_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "getSpace",
            {STBOX()},
            STBOX(),
            StboxFunctions::Stbox_get_space
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "expandTime",
            {STBOX(), LogicalType::INTERVAL},
            STBOX(),
            StboxFunctions::Stbox_expand_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "expandSpace",
            {STBOX(), LogicalType::DOUBLE},
            STBOX(),
            StboxFunctions::Stbox_expand_space
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_contains",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contains_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_contained",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contained_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_overlaps",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overlaps_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_same",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Same_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_adjacent",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Adjacent_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "@>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contains_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<@",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contained_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&&",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overlaps_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "~=",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Same_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "-|-",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Adjacent_stbox_stbox
        )
    );

    /* ***************************************************
     * Tspatial topological predicates (5 ops × 3 type pairs)
     * Operators + MobilityDB-canonical named-function aliases.
     ****************************************************/
    {
        const auto P = TgeompointType::TGEOMPOINT();
        const auto B = STBOX();

#define REG_TSPATIAL_TOPO(L, R, FN_SUFFIX)                                                                                       \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("@>",                {L, R}, LogicalType::BOOLEAN, StboxFunctions::Contains_##FN_SUFFIX));   \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contains", {L, R}, LogicalType::BOOLEAN, StboxFunctions::Contains_##FN_SUFFIX));   \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<@",                 {L, R}, LogicalType::BOOLEAN, StboxFunctions::Contained_##FN_SUFFIX)); \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contained", {L, R}, LogicalType::BOOLEAN, StboxFunctions::Contained_##FN_SUFFIX)); \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&&",                {L, R}, LogicalType::BOOLEAN, StboxFunctions::Overlaps_##FN_SUFFIX));   \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overlaps", {L, R}, LogicalType::BOOLEAN, StboxFunctions::Overlaps_##FN_SUFFIX));   \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("~=",                {L, R}, LogicalType::BOOLEAN, StboxFunctions::Same_##FN_SUFFIX));       \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_same",     {L, R}, LogicalType::BOOLEAN, StboxFunctions::Same_##FN_SUFFIX));       \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-|-",                {L, R}, LogicalType::BOOLEAN, StboxFunctions::Adjacent_##FN_SUFFIX));  \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_adjacent", {L, R}, LogicalType::BOOLEAN, StboxFunctions::Adjacent_##FN_SUFFIX));

        REG_TSPATIAL_TOPO(P, B, tspatial_stbox)
        REG_TSPATIAL_TOPO(B, P, stbox_tspatial)
        REG_TSPATIAL_TOPO(P, P, tspatial_tspatial)
#undef REG_TSPATIAL_TOPO
    }

        duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "stbox_left",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Left_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_overleft",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overleft_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_right",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Right_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_overright",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overright_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_below",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Below_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_overbelow",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overbelow_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_above",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Above_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_overabove",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overabove_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_before",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Before_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_overbefore",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overbefore_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_after",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::After_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_overafter",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overafter_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_front",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Front_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_overfront",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overfront_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_back",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Back_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_overback",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overback_stbox_stbox
        )
    );

        duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<<",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Left_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&<",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overleft_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            ">>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Right_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overright_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<<|",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Below_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&<|",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overbelow_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "|>>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Above_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "|&>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overabove_stbox_stbox
        )
    );
// # is not a operator in Duckdb, fix later
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<<#",  
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Before_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&<#",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overbefore_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "#>>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::After_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "#&>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overafter_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<</",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Front_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&</",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overfront_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "/>>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Back_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "/&>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overback_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_union",
            {STBOX(), STBOX()},
            STBOX(),
            StboxFunctions::Union_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_intersection",
            {STBOX(), STBOX()},
            STBOX(),
            StboxFunctions::Intersection_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "+",
            {STBOX(), STBOX()},
            STBOX(),
            StboxFunctions::Union_stbox_stbox
        )
    );
    
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "*",
            {STBOX(), STBOX()},
            STBOX(),
            StboxFunctions::Intersection_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_cmp",
            {STBOX(), STBOX()},
            LogicalType::INTEGER,
            StboxFunctions::Stbox_cmp
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_eq",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_eq
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_ne",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_ne
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_lt",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_lt
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_le",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_le
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_ge",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_ge
        )
    );  
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_gt",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_gt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "=",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_eq
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_ne
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_lt
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<=",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_le
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            ">=",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_ge
        )
    );  
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            ">",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_gt
        )
    );

    /* ***************************************************
     * Tile / box emitters and single-tile getters
     ****************************************************/
    {
        const auto B  = STBOX();
        const auto P  = TgeompointType::TGEOMPOINT();
        const auto G  = GeoTypes::GEOMETRY();
        const auto D  = LogicalType::DOUBLE;
        const auto I  = LogicalType::INTERVAL;
        const auto TS = LogicalType::TIMESTAMP_TZ;
        const auto BB = LogicalType::BOOLEAN;
        const auto LB = LogicalType::LIST(B);

        // spaceTiles(stbox, xsz, ysz, zsz[, sorigin geom[, borderInc bool]])
        loader.RegisterFunction(ScalarFunction("spaceTiles", {B, D, D, D},          LB, StboxFunctions::Stbox_space_tiles));
        loader.RegisterFunction(ScalarFunction("spaceTiles", {B, D, D, D, G},       LB, StboxFunctions::Stbox_space_tiles));
        loader.RegisterFunction(ScalarFunction("spaceTiles", {B, D, D, D, G, BB},   LB, StboxFunctions::Stbox_space_tiles));

        // timeTiles(stbox, duration[, torigin tstz[, borderInc bool]])
        loader.RegisterFunction(ScalarFunction("timeTiles", {B, I},          LB, StboxFunctions::Stbox_time_tiles));
        loader.RegisterFunction(ScalarFunction("timeTiles", {B, I, TS},      LB, StboxFunctions::Stbox_time_tiles));
        loader.RegisterFunction(ScalarFunction("timeTiles", {B, I, TS, BB},  LB, StboxFunctions::Stbox_time_tiles));

        // spaceTimeTiles(stbox, xsz, ysz, zsz, duration[, sorigin[, torigin[, borderInc]]])
        loader.RegisterFunction(ScalarFunction("spaceTimeTiles", {B, D, D, D, I},                LB, StboxFunctions::Stbox_space_time_tiles));
        loader.RegisterFunction(ScalarFunction("spaceTimeTiles", {B, D, D, D, I, G},             LB, StboxFunctions::Stbox_space_time_tiles));
        loader.RegisterFunction(ScalarFunction("spaceTimeTiles", {B, D, D, D, I, G, TS},         LB, StboxFunctions::Stbox_space_time_tiles));
        loader.RegisterFunction(ScalarFunction("spaceTimeTiles", {B, D, D, D, I, G, TS, BB},     LB, StboxFunctions::Stbox_space_time_tiles));

        // spaceBoxes(tgeompoint, xsz, ysz, zsz[, sorigin[, borderInc]])
        loader.RegisterFunction(ScalarFunction("spaceBoxes", {P, D, D, D},          LB, StboxFunctions::Tgeo_space_boxes));
        loader.RegisterFunction(ScalarFunction("spaceBoxes", {P, D, D, D, G},       LB, StboxFunctions::Tgeo_space_boxes));
        loader.RegisterFunction(ScalarFunction("spaceBoxes", {P, D, D, D, G, BB},   LB, StboxFunctions::Tgeo_space_boxes));

        // spaceTimeBoxes(tgeompoint, xsz, ysz, zsz, duration[, sorigin[, torigin[, borderInc]]])
        loader.RegisterFunction(ScalarFunction("spaceTimeBoxes", {P, D, D, D, I},                LB, StboxFunctions::Tgeo_space_time_boxes));
        loader.RegisterFunction(ScalarFunction("spaceTimeBoxes", {P, D, D, D, I, G},             LB, StboxFunctions::Tgeo_space_time_boxes));
        loader.RegisterFunction(ScalarFunction("spaceTimeBoxes", {P, D, D, D, I, G, TS},         LB, StboxFunctions::Tgeo_space_time_boxes));
        loader.RegisterFunction(ScalarFunction("spaceTimeBoxes", {P, D, D, D, I, G, TS, BB},     LB, StboxFunctions::Tgeo_space_time_boxes));

        // getSpaceTile(point geometry, xsz, ysz, zsz[, sorigin])
        loader.RegisterFunction(ScalarFunction("getSpaceTile", {G, D, D, D},     B, StboxFunctions::Stbox_get_space_tile));
        loader.RegisterFunction(ScalarFunction("getSpaceTile", {G, D, D, D, G},  B, StboxFunctions::Stbox_get_space_tile));

        // getStboxTimeTile(t timestamptz, duration[, torigin])
        loader.RegisterFunction(ScalarFunction("getStboxTimeTile", {TS, I},      B, StboxFunctions::Stbox_get_time_tile));
        loader.RegisterFunction(ScalarFunction("getStboxTimeTile", {TS, I, TS},  B, StboxFunctions::Stbox_get_time_tile));

        // getSpaceTimeTile(point, t, xsz, ysz, zsz, duration[, sorigin[, torigin]])
        loader.RegisterFunction(ScalarFunction("getSpaceTimeTile", {G, TS, D, D, D, I},          B, StboxFunctions::Stbox_get_space_time_tile));
        loader.RegisterFunction(ScalarFunction("getSpaceTimeTile", {G, TS, D, D, D, I, G},       B, StboxFunctions::Stbox_get_space_time_tile));
        loader.RegisterFunction(ScalarFunction("getSpaceTimeTile", {G, TS, D, D, D, I, G, TS},   B, StboxFunctions::Stbox_get_space_time_tile));
    }
}

} // namespace duckdb
