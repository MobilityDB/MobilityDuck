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
#include "spatial/spatial_types.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

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
 * Transformations
*/

inline void Temporal_to_tinstant(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_geom_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_geom_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));
            
            if (!temp) {
                throw InvalidInputException("Invalid tgeometry data: null pointer");
            }

            TInstant *inst = temporal_as_tinstant(temp);
            if (!inst) {
                throw InvalidInputException("Failed to convert tgeometry to TInstant");
            }
            
            size_t inst_size = temporal_mem_size((Temporal*)inst);
            
            uint8_t *inst_buffer = (uint8_t*)malloc(inst_size);
            if (!inst_buffer) {
                free(inst);
                throw InvalidInputException("Failed to allocate memory for TInstant data");
            }
            
            memcpy(inst_buffer, inst, inst_size);
            
            string_t inst_string_t(reinterpret_cast<const char*>(inst_buffer), inst_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, inst_string_t);
            
            free(inst_buffer);
            free(inst);
            
            return stored_data;
        }
    );

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}




/*
 * Accessor Functions
*/















void TGeometryTypes::RegisterScalarFunctions(ExtensionLoader &loader) {

    // timeSpan(tgeometry) is generated (meos_temporal_conversion, RETIRED_GROUPS).

    auto tgeometry_to_tinstant_function = ScalarFunction(
        "tgeometryInst",
        {TGeometryTypes::tgeometry()},
        TGeometryTypes::tgeometry(),  
        Temporal_to_tinstant);
    duckdb::RegisterSerializedScalarFunction(loader,  tgeometry_to_tinstant_function);







    








    // ===================================================================
    // Foundational tgeometry surface — accessors, time/value-restrict,
    // modifiers, and comparison. The MEOS C functions delegated to here
    // are subtype-agnostic (they take Temporal *), so we reuse the same
    // generic handlers wired for tgeompoint in temporal_functions.cpp.
    // ===================================================================

    const LogicalType TGEOM = TGeometryTypes::tgeometry();
    const LogicalType GEOM  = MobilityDuckGeometryType();
    const LogicalType TSTZ  = LogicalType::TIMESTAMP_TZ;
    const LogicalType IVAL  = LogicalType::INTERVAL;
    const LogicalType BIGI  = LogicalType::BIGINT;

    // ---- Accessors ----
    // timestamps(tgeometry) is generated from the catalog (temporal_timestamps) in generated_temporal_udfs.cpp.
    // ---- Modifiers (shift / scale / shiftScale / append / insert / update /
    // delete / round) ----
    RegisterMeosFunction(loader, ScalarFunction(
        "shiftScaleTime", {TGEOM, IVAL, IVAL}, TGEOM,
        TemporalFunctions::Temporal_shift_scale_time));
}

void TGeometryTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType( "tgeometry", TGeometryTypes::tgeometry());
}


}
