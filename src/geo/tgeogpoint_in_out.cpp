#include "geo/tgeogpoint.hpp"
#include "geo/tgeogpoint_functions.hpp"
#include "mobilityduck/meos_exec_serial.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include <regex>
#include <string>
#include <temporal/span.hpp>

extern "C" {
    #include <meos.h>
    #include <meos_geo.h>
    #include <meos_internal.h>
    #include <meos_internal_geo.h>
}

namespace duckdb {

static void Tspatial_as_text(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_geom_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_geom_vec, result, count,
        [&](string_t input_geom_str) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_geom_str.GetData());
            size_t data_size = input_geom_str.GetSize();

            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid TGEOGPOINT data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TGEOGPOINT deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOGPOINT data: null pointer");
            }

            char *str = tspatial_as_text(temp, 15);
            
            if (!str) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TGEOGPOINT to text");
            }
            
            std::string result_str(str);
            string_t stored_result = StringVector::AddString(result, result_str);
            
            free(str);
            free(data_copy);
            
            return stored_result;
        }
    );

}

static void Tspatial_as_ewkt(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_geom_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_geom_vec, result, count,
        [&](string_t input_geom_str) -> string_t {

            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_geom_str.GetData());
            size_t data_size = input_geom_str.GetSize();

            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid TGEOGPOINT data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TGEOGPOINT deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOGPOINT data: null pointer");
            }

            char *ewkt = tspatial_as_ewkt(temp, 15);
            
            if (!ewkt) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TGEOGPOINT to EWKT");
            }
            
            std::string result_str(ewkt);
            string_t stored_result = StringVector::AddString(result, result_str);
            
            
            free(ewkt); 
            free(data_copy);
            
            return stored_result;
        }
    );

}


bool TgeogpointFunctions::StringToTgeogpoint(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_string) -> string_t {
            std::string input_str = input_string.GetString();

            Temporal *temp = tgeogpoint_in(input_str.c_str());
            if (!temp) {
                throw InvalidInputException("Invalid TGEOGPOINT input: " + input_str);
            }
            
            size_t data_size = temporal_mem_size(temp);
            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(temp);
                throw InvalidInputException("Failed to allocate memory for TGEOGPOINT data");
            }
            
            memcpy(data_buffer, temp, data_size);
            
            string_t data_string_t(reinterpret_cast<const char*>(data_buffer), data_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, data_string_t);
            
            free(data_buffer);
            free(temp);
            
            return stored_data;
        });
        
    return true;
}

bool TgeogpointFunctions::TgeogpointToString(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid TGEOGPOINT data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TGEOGPOINT deserialization");
            }
            memcpy(data_copy, data, data_size);
            
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOGPOINT data: null pointer");
            }
            
            char *str = temporal_out(temp, 15);
            if (!str) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TGEOGPOINT to string");
            }
            
            std::string output(str);
            string_t stored_result = StringVector::AddString(result, output);
            
            free(str);
            free(data_copy);
            
            return stored_result;
        });
        
    return true;   
}

void TGeogpointType::RegisterScalarInOutFunctions(ExtensionLoader &loader){
    auto TgeogpointAsText = ScalarFunction(
            "asText", 
            {TGeogpointType::TGEOGPOINT()},
            LogicalType::VARCHAR,
            Tspatial_as_text
        );
        loader.RegisterFunction( TgeogpointAsText);

    auto TgeogpointAsEWKT = ScalarFunction(
        "asEWKT",
        {TGeogpointType::TGEOGPOINT()},
        LogicalType::VARCHAR,
        Tspatial_as_ewkt
    );
    loader.RegisterFunction( TgeogpointAsEWKT);
}


void TGeogpointType::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader,  LogicalType::VARCHAR, TGeogpointType::TGEOGPOINT(), TgeogpointFunctions::StringToTgeogpoint);
    RegisterMeosCastFunction(loader,  TGeogpointType::TGEOGPOINT(), LogicalType::VARCHAR, TgeogpointFunctions::TgeogpointToString);
}

}
