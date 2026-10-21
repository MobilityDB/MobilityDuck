#include "meos_wrapper_simple.hpp"
#include "duckdb_version_compat.hpp"

#include "common.hpp"
#include "geo/tgeogpoint.hpp"
#include "geo/tgeogpoint_functions.hpp"
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
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "spatial/spatial_types.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

#include "duckdb/common/extension_type_info.hpp"
#include <regex>
#include <string>
#include "geo_util.hpp"
#include "time_util.hpp"

extern "C" {
    #include <meos_internal.h>
    #include <meos_internal_geo.h>
}

namespace duckdb {

LogicalType TgeogpointType::tgeogpoint() {
    LogicalType type(LogicalTypeId::BLOB);
    type.SetAlias("tgeogpoint");
    return type;
}

void TgeogpointType::RegisterType(ExtensionLoader &loader) {
    loader.RegisterType("tgeogpoint", tgeogpoint());
}

void TgeogpointType::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, 
        LogicalType::VARCHAR,
        tgeogpoint(),
        TgeogpointFunctions::Tpoint_in
    );

    RegisterMeosCastFunction(loader, 
        tgeogpoint(),
        LogicalType::VARCHAR,
        TemporalFunctions::Temporal_out
    );

    RegisterMeosCastFunction(loader, 
        tgeogpoint(),
        StboxType::stbox(),
        TgeompointFunctions::Tspatial_to_stbox_cast
    );

    RegisterMeosCastFunction(loader, 
        tgeogpoint(),
        SpanTypes::tstzspan(),
        TgeompointFunctions::Temporal_to_tstzspan_cast
    );
}

void TgeogpointType::RegisterScalarFunctions(ExtensionLoader &loader) {

    /* ***************************************************
     * In/out functions
     ****************************************************/

    const auto varchar_list = LogicalType::LIST(LogicalType::VARCHAR);

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asText",
            {LogicalType::LIST(tgeogpoint())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asText",
            {LogicalType::LIST(tgeogpoint()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(tgeogpoint())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(tgeogpoint()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );


    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "stbox",
            {tgeogpoint()},
            StboxType::stbox(),
            TgeompointFunctions::Tspatial_to_stbox
        )
    );

    /* ***************************************************
     * Conversion functions
     ****************************************************/
    // timeSpan(tgeogpoint) (temporal_to_tstzspan, group meos_temporal_conversion) is
    // generated from the catalog in generated_temporal_udfs.cpp (RETIRED_GROUPS).

    /***************************************************
     * Transformation functions
     ****************************************************/











    /* ***************************************************
    * Accessor functions
    ****************************************************/




    // timestamps(tgeogpoint) is generated from the catalog (temporal_timestamps) in generated_temporal_udfs.cpp.

    /* ***************************************************
     * Shift and Scale functions
     ****************************************************/


    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "shiftScaleTime",
            {tgeogpoint(), LogicalType::INTERVAL, LogicalType::INTERVAL},
            tgeogpoint(),
            TemporalFunctions::Temporal_shift_scale_time
        )
    );

    /* ***************************************************
     * Restriction functions
     ****************************************************/


    /* ***************************************************
     * Modification function
     ****************************************************/












    /* ***************************************************
     * Stops function
     ****************************************************/

    /* ***************************************************
     * Spatial functions
     ****************************************************/










    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "makeSimple",
            {tgeogpoint()},
            LogicalType::LIST(tgeogpoint()),
            TgeompointFunctions::Tpoint_make_simple
        )
    );

    // round(tgeogpoint) at both arities is generated from the catalog temporal_round
    // signature (generated_temporal_udfs.cpp); no hand registration remains.

}

// ============================================================
// TGeogpointType (new full-parity surface)
// ============================================================

LogicalType TGeogpointType::tgeogpoint() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("tgeogpoint");
    return type;
}

/*
 * Constructors
*/

TInstant **temparr_extract_gp(Vector &tgeogpoint_arr_vec, list_entry_t list_entry, int *count) {
    auto &child_vector = ListVector::GetEntry(tgeogpoint_arr_vec);
    auto list_size = list_entry.length;
    auto list_offset = list_entry.offset;
    
    if (list_size == 0) {
        *count = 0;
        return nullptr;
    }
    
    *count = list_size;
    
    TInstant **instants = (TInstant**)malloc(sizeof(TInstant*) * list_size);
    if (!instants) {
        *count = 0;
        return nullptr;
    }
    
    for (idx_t i = 0; i < list_size; i++) {
        auto element_idx = list_offset + i;
        string_t tgeom_blob = FlatVector::GetData<string_t>(child_vector)[element_idx];
        
        const uint8_t *data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
        size_t data_size = tgeom_blob.GetSize();
        
        if (data_size < sizeof(void*)) {
            for (idx_t j = 0; j < i; j++) {
                if (instants[j]) free(instants[j]);
            }
            free(instants);
            *count = 0;
            return nullptr;
        }
        
        uint8_t *data_copy = (uint8_t*)malloc(data_size);
        if (!data_copy) {
            for (idx_t j = 0; j < i; j++) {
                if (instants[j]) free(instants[j]);
            }
            free(instants);
            *count = 0;
            return nullptr;
        }
        memcpy(data_copy, data, data_size);
        
        Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
        if (!temp) {
            free(data_copy);
            for (idx_t j = 0; j < i; j++) {
                if (instants[j]) free(instants[j]);
            }
            free(instants);
            *count = 0;
            return nullptr;
        }
        
        instants[i] = (TInstant*)temp;
    }
    
    return instants;
}







/*
 * Accessor Functions
*/















void TGeogpointType::RegisterScalarFunctions(ExtensionLoader &loader) {

    // timeSpan(tgeogpoint) is generated (meos_temporal_conversion, RETIRED_GROUPS).





    




    // ===================================================================
    // Foundational tgeogpoint surface — accessors, time/value-restrict,
    // modifiers, and comparison. The MEOS C functions delegated to here
    // are subtype-agnostic (they take Temporal *), so we reuse the same
    // generic handlers wired for tgeompoint in temporal_functions.cpp.
    // ===================================================================

    const LogicalType TGEOM = TGeogpointType::tgeogpoint();
    const LogicalType GEOM  = MobilityDuckGeometryType();
    const LogicalType TSTZ  = LogicalType::TIMESTAMP_TZ;
    const LogicalType IVAL  = LogicalType::INTERVAL;
    const LogicalType BIGI  = LogicalType::BIGINT;

    // ---- Accessors ----
    // timestamps(tgeogpoint) is generated from the catalog (temporal_timestamps) in generated_temporal_udfs.cpp.
    // ---- Modifiers (shift / scale / shiftScale / append / insert / update /
    // delete / round) ----
    RegisterMeosFunction(loader, ScalarFunction(
        "shiftScaleTime", {TGEOM, IVAL, IVAL}, TGEOM,
        TemporalFunctions::Temporal_shift_scale_time));
}

void TGeogpointType::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType( "tgeogpoint", TGeogpointType::tgeogpoint());
}


} // namespace duckdb
