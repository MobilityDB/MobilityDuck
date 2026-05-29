#include "geo/tgeometry.hpp"
#include "temporal/temporal_blob.hpp"
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

static void Tspatial_as_text(DataChunk &args, ExpressionState &state, Vector &result) {
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

            char *str = tspatial_as_text(temp, 15);
            
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

            char *ewkt = tspatial_as_ewkt(temp, 15);
            
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
        
    return true;   
}

// ---- Spatial-temporal parsers (Binary / HexWKB / MFJSON / Text) ----
// Used to register the `tgeometryFrom*` and `tgeographyFrom*` overloads.
// `temporal_from_wkb` and `temporal_from_hexwkb` are subtype-agnostic;
// `tgeometry_from_mfjson` / `tgeography_from_mfjson` and `tgeometry_in` /
// `tgeography_in` are per-type.  The result is stored as a raw blob, the
// same format every other temporal type uses.


static void TspatialFromWkbExec(DataChunk &args, ExpressionState &, Vector &result) {
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
            return TemporalToBlob(result, t);
        });
}

static void TspatialFromHexWkbExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string hex(input.GetData(), input.GetSize());
            Temporal *t = temporal_from_hexwkb(hex.c_str());
            if (!t) throw InvalidInputException(
                "fromHexWKB: invalid hex-encoded MEOS-WKB");
            return TemporalToBlob(result, t);
        });
}

template <Temporal *(*FN)(const char *)>
static void TspatialFromStringExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            Temporal *t = FN(s.c_str());
            if (!t) throw InvalidInputException("from*: invalid input");
            return TemporalToBlob(result, t);
        });
}

void TGeometryTypes::RegisterScalarInOutFunctions(ExtensionLoader &loader){
    auto TgeometryAsText = ScalarFunction(
            "asText",
            {TGeometryTypes::TGEOMETRY()},
            LogicalType::VARCHAR,
            Tspatial_as_text
        );
        duckdb::RegisterSerializedScalarFunction(loader,  TgeometryAsText);

    auto TgeometryAsEWKT = ScalarFunction(
        "asEWKT",
        {TGeometryTypes::TGEOMETRY()},
        LogicalType::VARCHAR,
        Tspatial_as_ewkt
    );
    duckdb::RegisterSerializedScalarFunction(loader,  TgeometryAsEWKT);

    // ---- tgeometryFromBinary / FromEWKB (auto-detects format) ----
    const auto B = LogicalType::BLOB;
    const auto V = LogicalType::VARCHAR;
    const auto T = TGeometryTypes::TGEOMETRY();
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tgeometryFromBinary", {B}, T, TspatialFromWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tgeometryFromEWKB",   {B}, T, TspatialFromWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tgeometryFromHexWKB",  {V}, T, TspatialFromHexWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tgeometryFromHexEWKB", {V}, T, TspatialFromHexWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tgeometryFromMFJSON", {V}, T,
                       TspatialFromStringExec<&tgeometry_from_mfjson>));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tgeometryFromText",   {V}, T,
                       TspatialFromStringExec<&tgeometry_in>));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tgeometryFromEWKT",   {V}, T,
                       TspatialFromStringExec<&tgeometry_in>));
}


void TGeometryTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader,  LogicalType::VARCHAR, TGeometryTypes::TGEOMETRY(), TgeometryFunctions::StringToTgeometry);
    RegisterMeosCastFunction(loader,  TGeometryTypes::TGEOMETRY(), LogicalType::VARCHAR, TgeometryFunctions::TgeometryToString);
}

}
