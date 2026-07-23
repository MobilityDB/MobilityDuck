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

LogicalType StboxType::stbox() {
    LogicalType type(LogicalTypeId::BLOB);
    type.SetAlias("stbox");
    return type;
}

void StboxType::RegisterType(ExtensionLoader &loader) {
    loader.RegisterType( "stbox", stbox());
}

void StboxType::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, 
        LogicalType::VARCHAR,
        stbox(),
        StboxFunctions::Stbox_in_cast
    );

    RegisterMeosCastFunction(loader, 
        stbox(),
        LogicalType::VARCHAR,
        StboxFunctions::Stbox_out
    );

    RegisterMeosCastFunction(loader, 
        GeoTypes::GEOMETRY(),
        stbox(),
        StboxFunctions::Geo_to_stbox_cast
    );

    RegisterMeosCastFunction(loader, 
        LogicalType::TIMESTAMP_TZ,
        stbox(),
        StboxFunctions::Timestamptz_to_stbox_cast
    );

    RegisterMeosCastFunction(loader, 
        SetTypes::tstzset(),
        stbox(),
        StboxFunctions::Tstzset_to_stbox_cast
    );

    RegisterMeosCastFunction(loader, 
        SpanTypes::tstzspan(),
        stbox(),
        StboxFunctions::Tstzspan_to_stbox_cast
    );

    RegisterMeosCastFunction(loader, 
        SpansetTypes::tstzspanset(),
        stbox(),
        StboxFunctions::Tstzspanset_to_stbox_cast
    );
}

void StboxType::RegisterScalarFunctions(ExtensionLoader &loader) {
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("stbox", {LogicalType::VARCHAR}, stbox(), StboxFunctions::Stbox_in, nullptr, nullptr, nullptr,
                     nullptr, LogicalType(LogicalTypeId::INVALID), FunctionStability::VOLATILE));

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stboxFromBinary",
            {LogicalType::BLOB},
            stbox(),
            StboxFunctions::Stbox_from_wkb
        )
    );

    // ExtensionUtil::RegisterFunction(
    //     instance,
    //     ScalarFunction(
    //         "stboxFromHexWKB",
    //         {LogicalType::VARCHAR},
    //         stbox(),
    //         StboxFunctions::Stbox_from_hexwkb
    //     )
    // );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asText",
            {stbox()},
            LogicalType::VARCHAR,
            StboxFunctions::Stbox_as_text
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asBinary",
            {stbox()},
            LogicalType::BLOB,
            StboxFunctions::Stbox_as_wkb
        )
    );

    // ExtensionUtil::RegisterFunction(
    //     instance,
    //     ScalarFunction(
    //         "asHexWKB",
    //         {stbox()},
    //         LogicalType::VARCHAR,
    //         StboxFunctions::Stbox_as_hexwkb
    //     )
    // );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox",
            {GeoTypes::GEOMETRY(), LogicalType::TIMESTAMP_TZ},
            StboxType::stbox(),
            StboxFunctions::Geo_timestamptz_to_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox",
            {GeoTypes::GEOMETRY(), SpanTypes::tstzspan()},
            StboxType::stbox(),
            StboxFunctions::Geo_tstzspan_to_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox",
            {LogicalType::TIMESTAMP_TZ},
            StboxType::stbox(),
            StboxFunctions::Timestamptz_to_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox",
            {SetTypes::tstzset()},
            StboxType::stbox(),
            StboxFunctions::Tstzset_to_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox",
            {SpanTypes::tstzspan()},
            StboxType::stbox(),
            StboxFunctions::Tstzspan_to_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox",
            {SpansetTypes::tstzspanset()},
            StboxType::stbox(),
            StboxFunctions::Tstzspanset_to_stbox
        )
    );
    

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox",
            {GeoTypes::GEOMETRY()},
            StboxType::stbox(),
            StboxFunctions::Geo_to_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "geometry",
            {stbox()},
            GeoTypes::GEOMETRY(),
            StboxFunctions::Stbox_to_geo
        )
    );

    // hasX/hasZ/hasT/isGeodetic are generated from the catalog (box unary scalar
    // accessors) in src/generated/generated_temporal_udfs.cpp.

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "Xmin",
            {stbox()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_xmin
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Ymin",
            {stbox()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_ymin
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Zmin",
            {stbox()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_zmin
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Tmin",
            {stbox()},
            LogicalType::TIMESTAMP_TZ,
            StboxFunctions::Stbox_tmin
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "TminInc",
            {stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_tmin_inc
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Xmax",
            {stbox()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_xmax
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Ymax",
            {stbox()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_ymax
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Zmax",
            {stbox()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_zmax
        )
    );
 
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Tmax",
            {stbox()},
            LogicalType::TIMESTAMP_TZ,
            StboxFunctions::Stbox_tmax
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "TmaxInc",
            {stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_tmax_inc
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "area",
            {stbox()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_area
        )
    );

    // volume is generated from the catalog (box unary scalar accessor).

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "shiftTime",
            {stbox(), LogicalType::INTERVAL},
            stbox(),
            StboxFunctions::Stbox_shift_time
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "scaleTime",
            {stbox(), LogicalType::INTERVAL},
            stbox(),
            StboxFunctions::Stbox_scale_time
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "shiftScaleTime",
            {stbox(), LogicalType::INTERVAL, LogicalType::INTERVAL},
            stbox(),
            StboxFunctions::Stbox_shift_scale_time
        )
    );

    // getSpace is generated from the catalog (box unary accessor -> stbox).

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "expandTime",
            {stbox(), LogicalType::INTERVAL},
            stbox(),
            StboxFunctions::Stbox_expand_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "expandSpace",
            {stbox(), LogicalType::DOUBLE},
            stbox(),
            StboxFunctions::Stbox_expand_space
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_contains",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contains_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_contained",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contained_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_overlaps",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overlaps_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_same",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Same_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_adjacent",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Adjacent_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "@>",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contains_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<@",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contained_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&&",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overlaps_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "~=",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Same_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "-|-",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Adjacent_stbox_stbox
        )
    );

    /* Tspatial × {stbox, tspatial} topological predicates — the operators
     * (~=/@>/<@/&&/-|-) and the canonical bare named functions (same/contains/
     * contained/overlaps/adjacent, the names deployed by MobilityDB's .in.sql) —
     * are generated from the MEOS-API catalog; see
     * src/generated/generated_temporal_udfs.cpp. (reg_names also registers the
     * catalog-only *_bbox @sqlfn backing tag as an alias; that is internal, not a
     * user-facing canonical name.) */

        duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "stbox_left",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Left_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_overleft",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overleft_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_right",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Right_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_overright",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overright_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_below",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Below_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_overbelow",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overbelow_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_above",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Above_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_overabove",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overabove_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_before",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Before_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_overbefore",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overbefore_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_after",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::After_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_overafter",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overafter_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_front",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Front_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_overfront",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overfront_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_back",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Back_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_overback",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overback_stbox_stbox
        )
    );

        duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<<",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Left_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&<",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overleft_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            ">>",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Right_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&>",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overright_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<<|",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Below_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&<|",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overbelow_stbox_stbox
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "|>>",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Above_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "|&>",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overabove_stbox_stbox
        )
    );
// # is not a operator in Duckdb, fix later
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<<#",  
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Before_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&<#",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overbefore_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "#>>",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::After_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "#&>",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overafter_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<</",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Front_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&</",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overfront_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "/>>",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Back_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "/&>",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overback_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_union",
            {stbox(), stbox()},
            stbox(),
            StboxFunctions::Union_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_intersection",
            {stbox(), stbox()},
            stbox(),
            StboxFunctions::Intersection_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "+",
            {stbox(), stbox()},
            stbox(),
            StboxFunctions::Union_stbox_stbox
        )
    );
    
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "*",
            {stbox(), stbox()},
            stbox(),
            StboxFunctions::Intersection_stbox_stbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_cmp",
            {stbox(), stbox()},
            LogicalType::INTEGER,
            StboxFunctions::Stbox_cmp
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_eq",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_eq
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_ne",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_ne
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_lt",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_lt
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_le",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_le
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_ge",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_ge
        )
    );  
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "stbox_gt",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_gt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "=",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_eq
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<>",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_ne
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_lt
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<=",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_le
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            ">=",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_ge
        )
    );  
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            ">",
            {stbox(), stbox()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_gt
        )
    );

    /* ***************************************************
     * Tile / box emitters and single-tile getters
     ****************************************************/
    {
        const auto B  = stbox();
        const auto P  = TgeompointType::tgeompoint();
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
