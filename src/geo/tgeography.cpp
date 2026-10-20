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
#include "spatial/spatial_types.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

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

inline void Tgeog_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_geom_vec = args.data[0];
    
    UnaryExecutor::Execute<string_t, string_t>(
        input_geom_vec, result, count, 
        [&](string_t input_geom_str) -> string_t {
            std::string input = input_geom_str.GetString();
            
            Temporal *tinst = tgeography_in(input.c_str());
            if (!tinst) {
                throw InvalidInputException("Invalid tgeography input: " + input);
            }
            
            size_t data_size = temporal_mem_size(tinst);
            
            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(tinst);  
                throw InvalidInputException("Failed to allocate memory for tgeography data");
            }
            
            memcpy(data_buffer, tinst, data_size);
            
            string_t data_string_t(reinterpret_cast<const char*>(data_buffer), data_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, data_string_t);
            
            free(data_buffer);
            free(tinst);  
            
            return stored_data;
        });
    
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}




/*
 * Conversions
*/

inline void Temporal_to_tstzspan(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_geom_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_geom_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));
            
            if (!temp) {
                throw InvalidInputException("Invalid tgeography data: null pointer");
            }

            Span *timespan = temporal_to_tstzspan(temp);
            
            if (!timespan) {
                throw InvalidInputException("Failed to extract timespan from tgeography");
            }
            
            size_t span_size = sizeof(Span);
            
            uint8_t *span_buffer = (uint8_t*)malloc(span_size);
            if (!span_buffer) {
                free(timespan);
                throw InvalidInputException("Failed to allocate memory for timespan data");
            }
            
            memcpy(span_buffer, timespan, span_size);
            
            string_t span_string_t(reinterpret_cast<const char*>(span_buffer), span_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
            
            free(span_buffer);
            free(timespan);
            
            return stored_data;
        }
    );

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

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
                throw InvalidInputException("Invalid tgeography data: null pointer");
            }

            TInstant *inst = temporal_as_tinstant(temp);
            if (!inst) {
                throw InvalidInputException("Failed to convert tgeography to TInstant");
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


inline void Temporal_set_interp(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &tgeom_vec = args.data[0];
    auto &interp_vec = args.data[1];

    tgeom_vec.Flatten(count);
    interp_vec.Flatten(count);

    BinaryExecutor::Execute<string_t, string_t, string_t>(
        tgeom_vec, interp_vec, result, count,
        [&](string_t tgeom_str_t, string_t interp_str_t) -> string_t {
          
            std::string input = tgeom_str_t.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));
            if (!temp) {
                throw InvalidInputException("Invalid tgeography data: null pointer");
            }

            std::string interp_str = interp_str_t.GetString();
            interpType new_interp = interptype_from_string(interp_str.c_str());
            
            Temporal *result_temp = temporal_set_interp(temp, new_interp);
            if (!result_temp) {
                throw InvalidInputException("Failed to set interpolation");
            }
            
            // Serialize result back to binary
            size_t result_size = temporal_mem_size(result_temp);
            uint8_t *result_buffer = (uint8_t*)malloc(result_size);
            if (!result_buffer) {
                free(result_temp);
                throw InvalidInputException("Failed to allocate memory for result");
            }
            
            memcpy(result_buffer, result_temp, result_size);
            string_t result_string_t(reinterpret_cast<const char*>(result_buffer), result_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, result_string_t);
            
            free(result_buffer);
            free(result_temp);
            
            return stored_data;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}



/*
 * Accessor Functions
*/




inline void Tinstant_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];
    
    // Direct geometry to geometry conversion, no string conversion needed
    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            TInstant *tinst = reinterpret_cast<TInstant*>(const_cast<char*>(input.c_str()));
            
            Datum geo = tinstant_value(tinst);
            
            GSERIALIZED *geom = DatumGetGserializedP(geo);
            
            string_t geometry_blob = GSerializedToGeometry(geom, state, result);
            string_t stored_result = StringVector::AddStringOrBlob(result, geometry_blob);

            free(geom);
            
            return stored_result;
        });
    
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}









inline void ExecuteTGeometrySeq(DataChunk &args, ExpressionState &state, Vector &result) {
    const char* default_interp = "step";
    auto count = args.size();
    auto &tgeography_vec = args.data[0];
    
    Vector interp_vec(LogicalType::VARCHAR, count);
    if (args.data.size() > 1) {
        interp_vec.Reference(args.data[1]);
    } else {
        for (idx_t i = 0; i < count; i++) {
            FlatVector::GetData<string_t>(interp_vec)[i] = StringVector::AddString(interp_vec, default_interp);
        }
    }
    
    BinaryExecutor::Execute<string_t, string_t, string_t>(
        tgeography_vec, interp_vec, result, count,
        [&](string_t tgeom_blob, string_t interp_str) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t data_size = tgeom_blob.GetSize();
            
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid tgeography data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for tgeography deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid tgeography data: null pointer");
            }
            
            std::string interp_string = interp_str.GetString();
            if (interp_string.empty()) {
                interp_string = default_interp;
            }
            
            interpType interp = interptype_from_string(interp_string.c_str());
            TSequence *seq = temporal_as_tsequence(temp, interp);
            
            if (!seq) {
                free(data_copy);
                throw InvalidInputException("Failed to create TSequence");
            }
            
            size_t seq_data_size = temporal_mem_size(reinterpret_cast<Temporal*>(seq));
            uint8_t *seq_data_buffer = (uint8_t*)malloc(seq_data_size);
            if (!seq_data_buffer) {
                free(data_copy);
                free(seq);
                throw InvalidInputException("Failed to allocate memory for TSequence data");
            }
            
            memcpy(seq_data_buffer, seq, seq_data_size);
            
            string_t seq_data_string_t(reinterpret_cast<const char*>(seq_data_buffer), seq_data_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, seq_data_string_t);
            
            free(seq_data_buffer);
            free(data_copy);
            free(seq);
            
            return stored_data;
        });
    
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}




void TGeographyTypes::RegisterScalarFunctions(ExtensionLoader &loader) {

    auto tgeography_function = ScalarFunction(
        "tgeography", 
        {LogicalType::VARCHAR}, 
        TGeographyTypes::tgeography(),
        Tgeog_constructor
    );
    RegisterMeosFunction(loader, tgeography_function);
        
    // timeSpan(tgeography) is generated (meos_temporal_conversion, RETIRED_GROUPS).

    auto tgeography_to_tinstant_function = ScalarFunction(
        "tgeographyInst",
        {TGeographyTypes::tgeography()},
        TGeographyTypes::tgeography(),  
        Temporal_to_tinstant);
    RegisterMeosFunction(loader, tgeography_to_tinstant_function);







    







    // ===================================================================
    // Foundational tgeography surface — accessors, time/value-restrict,
    // modifiers, and comparison. The MEOS C functions delegated to here
    // are subtype-agnostic (they take Temporal *), so we reuse the same
    // generic handlers wired for tgeompoint in temporal_functions.cpp.
    // ===================================================================

    const LogicalType TGEOM = TGeographyTypes::tgeography();
    const LogicalType GEOM  = MobilityDuckGeometryType();
    const LogicalType TSTZ  = LogicalType::TIMESTAMP_TZ;
    const LogicalType IVAL  = LogicalType::INTERVAL;
    const LogicalType BIGI  = LogicalType::BIGINT;

    // ---- Accessors ----
    // timestamps(tgeography) is generated from the catalog (temporal_timestamps) in generated_temporal_udfs.cpp.
    // ---- Modifiers (shift / scale / shiftScale / append / insert / update /
    // delete / round) ----
    RegisterMeosFunction(loader, ScalarFunction(
        "shiftScaleTime", {TGEOM, IVAL, IVAL}, TGEOM,
        TemporalFunctions::Temporal_shift_scale_time));

    // ---- Comparison operators (eq, ne, lt, le, gt and ge are generated) ----
    struct CmpEntry {
        const char *name;
        scalar_function_t fn;
    };

    // Operator forms — mirror the registrations tgeompoint.cpp does.
    const std::vector<CmpEntry> op_cmps = {
        {"=",  TemporalFunctions::Temporal_eq},
        {"<>", TemporalFunctions::Temporal_ne},
        {"<",  TemporalFunctions::Temporal_lt},
        {"<=", TemporalFunctions::Temporal_le},
        {">",  TemporalFunctions::Temporal_gt},
        {">=", TemporalFunctions::Temporal_ge},
    };
    for (const auto &c : op_cmps) {
        RegisterMeosFunction(loader, ScalarFunction(
            c.name, {TGEOM, TGEOM}, LogicalType::BOOLEAN, c.fn));
    }
}

void TGeographyTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType( "tgeography", TGeographyTypes::tgeography());
}


}
