#include "geo/tgeography.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include <regex>
#include <string>
#include <temporal/span.hpp>
#include "mobilityduck/meos_exec_serial.hpp"

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
                throw InvalidInputException("Invalid TGEOGRAPHY data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TGEOGRAPHY deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOGRAPHY data: null pointer");
            }

            char *str = tspatial_as_text(temp, 0);
            
            if (!str) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TGEOGRAPHY to text");
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
                throw InvalidInputException("Invalid TGEOGRAPHY data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TGEOGRAPHY deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOGRAPHY data: null pointer");
            }

            char *ewkt = tspatial_as_ewkt(temp, 0);
            
            if (!ewkt) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TGEOGRAPHY to EWKT");
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


bool TgeographyFunctions::StringToTgeography(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_string) -> string_t {
            std::string input_str = input_string.GetString();

            Temporal *temp = tgeography_in(input_str.c_str());
            if (!temp) {
                throw InvalidInputException("Invalid TGEOGRAPHY input: " + input_str);
            }
            
            size_t data_size = temporal_mem_size(temp);
            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(temp);
                throw InvalidInputException("Failed to allocate memory for TGEOGRAPHY data");
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

bool TgeographyFunctions::TgeographyToString(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid TGEOGRAPHY data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TGEOGRAPHY deserialization");
            }
            memcpy(data_copy, data, data_size);
            
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOGRAPHY data: null pointer");
            }
            
            char *str = temporal_out(temp, 15);
            if (!str) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TGEOGRAPHY to string");
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

// ---- Spatial-temporal parsers (Binary / HexWKB / MFJSON / Text) ----
// Mirror of the helpers in tgeometry_in_out.cpp; per-type for MFJSON /
// text since MEOS uses tgeography_from_mfjson / tgeography_in there.

inline string_t StoreTempAsBlobGeog(Vector &result, Temporal *t) {
    size_t sz = temporal_mem_size(t);
    string_t stored = StringVector::AddStringOrBlob(
        result, string_t(reinterpret_cast<const char *>(t), sz));
    free(t);
    return stored;
}

inline void TgeographyFromWkbExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            if (input.GetSize() == 0)
                throw InvalidInputException("fromBinary: empty WKB input");
            uint8_t *wkb = (uint8_t *)malloc(input.GetSize());
            if (!wkb) throw InternalException("fromBinary: malloc failed");
            memcpy(wkb, input.GetData(), input.GetSize());
            Temporal *t = temporal_from_wkb(wkb, input.GetSize());
            free(wkb);
            if (!t) throw InvalidInputException("fromBinary: invalid MEOS-WKB");
            return StoreTempAsBlobGeog(result, t);
        });
}

inline void TgeographyFromHexWkbExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string hex(input.GetData(), input.GetSize());
            Temporal *t = temporal_from_hexwkb(hex.c_str());
            if (!t) throw InvalidInputException(
                "fromHexWKB: invalid hex-encoded MEOS-WKB");
            return StoreTempAsBlobGeog(result, t);
        });
}

template <Temporal *(*FN)(const char *)>
inline void TgeographyFromStringExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            Temporal *t = FN(s.c_str());
            if (!t) throw InvalidInputException("from*: invalid input");
            return StoreTempAsBlobGeog(result, t);
        });
}

void TGeographyTypes::RegisterScalarInOutFunctions(ExtensionLoader &loader){
    auto TgeographyAsText = ScalarFunction(
            "asText",
            {TGeographyTypes::TGEOGRAPHY()},
            LogicalType::VARCHAR,
            Tspatial_as_text
        );
        loader.RegisterFunction( TgeographyAsText);

    auto TgeographyAsEWKT = ScalarFunction(
        "asEWKT",
        {TGeographyTypes::TGEOGRAPHY()},
        LogicalType::VARCHAR,
        Tspatial_as_ewkt
    );
    loader.RegisterFunction( TgeographyAsEWKT);

    const auto B = LogicalType::BLOB;
    const auto V = LogicalType::VARCHAR;
    const auto T = TGeographyTypes::TGEOGRAPHY();
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tgeographyFromBinary",  {B}, T, TgeographyFromWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tgeographyFromEWKB",    {B}, T, TgeographyFromWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tgeographyFromHexWKB",  {V}, T, TgeographyFromHexWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tgeographyFromHexEWKB", {V}, T, TgeographyFromHexWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tgeographyFromMFJSON",  {V}, T,
                       TgeographyFromStringExec<&tgeography_from_mfjson>));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tgeographyFromText",    {V}, T,
                       TgeographyFromStringExec<&tgeography_in>));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tgeographyFromEWKT",    {V}, T,
                       TgeographyFromStringExec<&tgeography_in>));
}


void TGeographyTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader,  LogicalType::VARCHAR, TGeographyTypes::TGEOGRAPHY(), TgeographyFunctions::StringToTgeography);
    RegisterMeosCastFunction(loader,  TGeographyTypes::TGEOGRAPHY(), LogicalType::VARCHAR, TgeographyFunctions::TgeographyToString);
}

}
