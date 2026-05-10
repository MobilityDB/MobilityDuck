#include "geo/tgeometry.hpp"
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
                throw InvalidInputException("Invalid TGEOMETRY data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TGEOMETRY deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMETRY data: null pointer");
            }

            char *str = tspatial_as_text(temp, 0);
            
            if (!str) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TGEOMETRY to text");
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
                throw InvalidInputException("Invalid TGEOMETRY data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TGEOMETRY deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMETRY data: null pointer");
            }

            char *ewkt = tspatial_as_ewkt(temp, 0);
            
            if (!ewkt) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TGEOMETRY to EWKT");
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


bool TgeometryFunctions::StringToTgeometry(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_string) -> string_t {
            std::string input_str = input_string.GetString();

            Temporal *temp = tgeometry_in(input_str.c_str());
            if (!temp) {
                throw InvalidInputException("Invalid TGEOMETRY input: " + input_str);
            }
            
            size_t data_size = temporal_mem_size(temp);
            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(temp);
                throw InvalidInputException("Failed to allocate memory for TGEOMETRY data");
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

bool TgeometryFunctions::TgeometryToString(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid TGEOMETRY data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TGEOMETRY deserialization");
            }
            memcpy(data_copy, data, data_size);
            
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMETRY data: null pointer");
            }
            
            char *str = temporal_out(temp, 15);
            if (!str) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TGEOMETRY to string");
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

namespace {

void TgeometryAsWkbExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            size_t sz = input.GetSize();
            uint8_t *copy = (uint8_t *)malloc(sz);
            if (!copy) throw InternalException("asBinary: malloc failed");
            memcpy(copy, input.GetData(), sz);
            Temporal *t = reinterpret_cast<Temporal *>(copy);
            size_t wkb_sz = 0;
            uint8_t *wkb = temporal_as_wkb(t, WKB_EXTENDED, &wkb_sz);
            free(copy);
            if (!wkb || wkb_sz == 0) { if (wkb) free(wkb); throw InternalException("temporal_as_wkb failed"); }
            string_t stored = StringVector::AddStringOrBlob(
                result, string_t(reinterpret_cast<const char *>(wkb), wkb_sz));
            free(wkb);
            return stored;
        });
}

void TgeometryFromWkbExec(DataChunk &args, ExpressionState &, Vector &result) {
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
            if (!t) throw InvalidInputException("fromBinary: invalid tgeometry WKB");
            size_t sz = temporal_mem_size(t);
            string_t stored = StringVector::AddStringOrBlob(
                result, string_t(reinterpret_cast<const char *>(t), sz));
            free(t);
            return stored;
        });
}

void TgeometryAsHexWkbExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            size_t sz = input.GetSize();
            uint8_t *copy = (uint8_t *)malloc(sz);
            if (!copy) throw InternalException("asHexWKB: malloc failed");
            memcpy(copy, input.GetData(), sz);
            Temporal *t = reinterpret_cast<Temporal *>(copy);
            size_t hex_sz = 0;
            char *hex = temporal_as_hexwkb(t, WKB_EXTENDED, &hex_sz);
            free(copy);
            if (!hex || hex_sz == 0) { if (hex) free(hex); throw InternalException("temporal_as_hexwkb failed"); }
            string_t stored = StringVector::AddString(result, hex, hex_sz);
            free(hex);
            return stored;
        });
}

void TgeometryFromHexWkbExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            if (input.GetSize() == 0)
                throw InvalidInputException("fromHexWKB: empty hex input");
            std::string hex_str(input.GetData(), input.GetSize());
            Temporal *t = temporal_from_hexwkb(hex_str.c_str());
            if (!t) throw InvalidInputException("fromHexWKB: invalid tgeometry hex WKB");
            size_t sz = temporal_mem_size(t);
            string_t stored = StringVector::AddStringOrBlob(
                result, string_t(reinterpret_cast<const char *>(t), sz));
            free(t);
            return stored;
        });
}

} // anonymous namespace

void TGeometryTypes::RegisterScalarInOutFunctions(ExtensionLoader &loader){
    const auto T = TGeometryTypes::TGEOMETRY();
    const auto B = LogicalType::BLOB;
    const auto V = LogicalType::VARCHAR;

    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asBinary",           {T}, B, TgeometryAsWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeometryFromBinary", {B}, T, TgeometryFromWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asHexWKB",            {T}, V, TgeometryAsHexWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeometryFromHexWKB", {V}, T, TgeometryFromHexWkbExec));

    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asText",  {T}, V, Tspatial_as_text));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asEWKT",  {T}, V, Tspatial_as_ewkt));
}


void TGeometryTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    loader.RegisterCastFunction( LogicalType::VARCHAR, TGeometryTypes::TGEOMETRY(), TgeometryFunctions::StringToTgeometry);
    loader.RegisterCastFunction( TGeometryTypes::TGEOMETRY(), LogicalType::VARCHAR, TgeometryFunctions::TgeometryToString);
}

}