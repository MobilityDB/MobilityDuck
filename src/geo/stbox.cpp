#include "meos_wrapper_simple.hpp"
#include "duckdb_version_compat.hpp"

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
        MobilityDuckGeometryType(),
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
        ScalarFunction("stbox", {LogicalType::VARCHAR}, stbox(), StboxFunctions::Stbox_in));

    /* Tspatial × {stbox, tspatial} topological predicates — the operators
     * (~=/@>/<@/&&/-|-) and the canonical bare named functions (same/contains/
     * contained/overlaps/adjacent, the names deployed by MobilityDB's .in.sql) —
     * are generated from the MEOS-API catalog; see
     * src/generated/generated_temporal_udfs.cpp. (reg_names also registers the
     * catalog-only *_bbox @sqlfn backing tag as an alias; that is internal, not a
     * user-facing canonical name.) */

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "*",
            {stbox(), stbox()},
            stbox(),
            StboxFunctions::Intersection_stbox_stbox
        )
    );

    /* ***************************************************
     * Tile / box emitters and single-tile getters
     ****************************************************/
    {
        const auto B  = stbox();
        const auto P  = TgeompointType::tgeompoint();
        const auto G  = MobilityDuckGeometryType();
        const auto D  = LogicalType::DOUBLE;
        const auto I  = LogicalType::INTERVAL;
        const auto TS = LogicalType::TIMESTAMP_TZ;
        const auto BB = LogicalType::BOOLEAN;
        const auto LB = LogicalType::LIST(B);

        // spaceTiles(stbox, xsz, ysz, zsz[, sorigin geom[, borderInc bool]])
        RegisterMeosFunction(loader, ScalarFunction("spaceTiles", {B, D, D, D},          LB, StboxFunctions::Stbox_space_tiles));
        RegisterMeosFunction(loader, ScalarFunction("spaceTiles", {B, D, D, D, G},       LB, StboxFunctions::Stbox_space_tiles));
        RegisterMeosFunction(loader, ScalarFunction("spaceTiles", {B, D, D, D, G, BB},   LB, StboxFunctions::Stbox_space_tiles));

        // timeTiles(stbox, duration[, torigin tstz[, borderInc bool]])
        RegisterMeosFunction(loader, ScalarFunction("timeTiles", {B, I},          LB, StboxFunctions::Stbox_time_tiles));
        RegisterMeosFunction(loader, ScalarFunction("timeTiles", {B, I, TS},      LB, StboxFunctions::Stbox_time_tiles));
        RegisterMeosFunction(loader, ScalarFunction("timeTiles", {B, I, TS, BB},  LB, StboxFunctions::Stbox_time_tiles));

        // spaceTimeTiles(stbox, xsz, ysz, zsz, duration[, sorigin[, torigin[, borderInc]]])
        RegisterMeosFunction(loader, ScalarFunction("spaceTimeTiles", {B, D, D, D, I},                LB, StboxFunctions::Stbox_space_time_tiles));
        RegisterMeosFunction(loader, ScalarFunction("spaceTimeTiles", {B, D, D, D, I, G},             LB, StboxFunctions::Stbox_space_time_tiles));
        RegisterMeosFunction(loader, ScalarFunction("spaceTimeTiles", {B, D, D, D, I, G, TS},         LB, StboxFunctions::Stbox_space_time_tiles));
        RegisterMeosFunction(loader, ScalarFunction("spaceTimeTiles", {B, D, D, D, I, G, TS, BB},     LB, StboxFunctions::Stbox_space_time_tiles));

        // spaceBoxes(tgeompoint, xsz, ysz, zsz[, sorigin[, borderInc]])
        RegisterMeosFunction(loader, ScalarFunction("spaceBoxes", {P, D, D, D},          LB, StboxFunctions::Tgeo_space_boxes));
        RegisterMeosFunction(loader, ScalarFunction("spaceBoxes", {P, D, D, D, G},       LB, StboxFunctions::Tgeo_space_boxes));
        RegisterMeosFunction(loader, ScalarFunction("spaceBoxes", {P, D, D, D, G, BB},   LB, StboxFunctions::Tgeo_space_boxes));

        // spaceTimeBoxes(tgeompoint, xsz, ysz, zsz, duration[, sorigin[, torigin[, borderInc]]])
        RegisterMeosFunction(loader, ScalarFunction("spaceTimeBoxes", {P, D, D, D, I},                LB, StboxFunctions::Tgeo_space_time_boxes));
        RegisterMeosFunction(loader, ScalarFunction("spaceTimeBoxes", {P, D, D, D, I, G},             LB, StboxFunctions::Tgeo_space_time_boxes));
        RegisterMeosFunction(loader, ScalarFunction("spaceTimeBoxes", {P, D, D, D, I, G, TS},         LB, StboxFunctions::Tgeo_space_time_boxes));
        RegisterMeosFunction(loader, ScalarFunction("spaceTimeBoxes", {P, D, D, D, I, G, TS, BB},     LB, StboxFunctions::Tgeo_space_time_boxes));

        // The single-tile getters getSpaceTile, getStboxTimeTile and
        // getSpaceTimeTile are registered by single_tile_getters.cpp.
    }
}

} // namespace duckdb
