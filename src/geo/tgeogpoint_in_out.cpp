#include "geo/tgeogpoint.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include <regex>
#include <string>
#include <temporal/span.hpp>
#include "temporal/temporal_functions.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_geo.h>
    #include <meos_internal.h>
    #include <meos_internal_geo.h>
}

namespace duckdb {

inline void Tspatial_as_text(DataChunk &args, ExpressionState &state, Vector &result) {
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

            char *str = tspatial_as_text(temp, 0);
            
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

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

inline void Tspatial_as_ewkt(DataChunk &args, ExpressionState &state, Vector &result) {
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

            char *ewkt = tspatial_as_ewkt(temp, 0);
            
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

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
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
        
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
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
        
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
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

    const auto TGGP = TGeogpointType::TGEOGPOINT();
    loader.RegisterFunction(ScalarFunction("asMFJSON",
        {TGGP}, LogicalType::VARCHAR, TemporalFunctions::Temporal_as_mfjson));
    loader.RegisterFunction(ScalarFunction("asMFJSON",
        {TGGP, LogicalType::BOOLEAN}, LogicalType::VARCHAR,
        TemporalFunctions::Temporal_as_mfjson));
    loader.RegisterFunction(ScalarFunction("asMFJSON",
        {TGGP, LogicalType::BOOLEAN, LogicalType::INTEGER},
        LogicalType::VARCHAR, TemporalFunctions::Temporal_as_mfjson));
    loader.RegisterFunction(ScalarFunction("asHexWKB",
        {TGGP}, LogicalType::VARCHAR, TemporalFunctions::Temporal_as_hexwkb));
    loader.RegisterFunction(ScalarFunction("asHexEWKB",
        {TGGP}, LogicalType::VARCHAR, TemporalFunctions::Temporal_as_hexwkb));
    loader.RegisterFunction(ScalarFunction("asEWKB",
        {TGGP}, LogicalType::BLOB, TemporalFunctions::Temporal_as_ewkb));
    loader.RegisterFunction(ScalarFunction("tgeogpointFromMFJSON",
        {LogicalType::VARCHAR}, TGGP, TemporalFunctions::Tgeogpoint_from_mfjson));

    // tprecision / tsample for tgeogpoint.
    loader.RegisterFunction(ScalarFunction("tprecision",
        {TGGP, LogicalType::INTERVAL}, TGGP,
        TemporalFunctions::Temporal_tprecision));
    loader.RegisterFunction(ScalarFunction("tprecision",
        {TGGP, LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ}, TGGP,
        TemporalFunctions::Temporal_tprecision));
    loader.RegisterFunction(ScalarFunction("tsample",
        {TGGP, LogicalType::INTERVAL}, TGGP,
        TemporalFunctions::Temporal_tsample));
    loader.RegisterFunction(ScalarFunction("tsample",
        {TGGP, LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ}, TGGP,
        TemporalFunctions::Temporal_tsample));
    loader.RegisterFunction(ScalarFunction("tsample",
        {TGGP, LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ,
         LogicalType::VARCHAR}, TGGP,
        TemporalFunctions::Temporal_tsample));

    // transformPipeline(tgeogpoint, pipeline text, srid int, is_forward bool)
    loader.RegisterFunction(ScalarFunction("transformPipeline",
        {TGGP, LogicalType::VARCHAR, LogicalType::INTEGER, LogicalType::BOOLEAN},
        TGGP, TemporalFunctions::Tspatial_transform_pipeline));
}


void TGeogpointType::RegisterCastFunctions(ExtensionLoader &loader) {
    loader.RegisterCastFunction( LogicalType::VARCHAR, TGeogpointType::TGEOGPOINT(), TgeogpointFunctions::StringToTgeogpoint);
    loader.RegisterCastFunction( TGeogpointType::TGEOGPOINT(), LogicalType::VARCHAR, TgeogpointFunctions::TgeogpointToString);
}

}
