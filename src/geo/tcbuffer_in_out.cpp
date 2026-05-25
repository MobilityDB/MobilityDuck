#include "geo/tcbuffer.hpp"
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
    #include <meos_cbuffer.h>
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
                throw InvalidInputException("Invalid TCBUFFER data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TCBUFFER deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TCBUFFER data: null pointer");
            }

            // maxdecimaldigits = 15, matching MobilityDB's asText(tcbuffer)
            // default; 0 truncated the cbuffer radius to an integer (0.5 -> 0).
            char *str = tspatial_as_text(temp, 15);

            if (!str) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TCBUFFER to text");
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
                throw InvalidInputException("Invalid TCBUFFER data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TCBUFFER deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TCBUFFER data: null pointer");
            }

            // maxdecimaldigits = 15 (see Tspatial_as_text): 0 truncated the
            // cbuffer radius to an integer.
            char *ewkt = tspatial_as_ewkt(temp, 15);

            if (!ewkt) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TCBUFFER to EWKT");
            }

            std::string result_str(ewkt);
            string_t stored_result = StringVector::AddString(result, result_str);


            free(ewkt);
            free(data_copy);

            return stored_result;
        }
    );

}


bool TcbufferFunctions::StringToTcbuffer(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_string) -> string_t {
            std::string input_str = input_string.GetString();

            Temporal *temp = tcbuffer_in(input_str.c_str());
            if (!temp) {
                throw InvalidInputException("Invalid TCBUFFER input: " + input_str);
            }

            size_t data_size = temporal_mem_size(temp);
            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(temp);
                throw InvalidInputException("Failed to allocate memory for TCBUFFER data");
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

bool TcbufferFunctions::TcbufferToString(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();

            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid TCBUFFER data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TCBUFFER deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TCBUFFER data: null pointer");
            }

            char *str = temporal_out(temp, 15);
            if (!str) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TCBUFFER to string");
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
// Used to register the `tcbufferFrom*` overloads.
// `temporal_from_wkb` and `temporal_from_hexwkb` are subtype-agnostic;
// `tcbuffer_from_mfjson` and `tcbuffer_in` are per-type.  The result is
// stored as a raw blob, the same format every other temporal type uses.


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

void TCBufferTypes::RegisterScalarInOutFunctions(ExtensionLoader &loader){
    auto TcbufferAsText = ScalarFunction(
            "asText",
            {TCBufferTypes::TCBUFFER()},
            LogicalType::VARCHAR,
            Tspatial_as_text
        );
        duckdb::RegisterSerializedScalarFunction(loader,  TcbufferAsText);

    auto TcbufferAsEWKT = ScalarFunction(
        "asEWKT",
        {TCBufferTypes::TCBUFFER()},
        LogicalType::VARCHAR,
        Tspatial_as_ewkt
    );
    duckdb::RegisterSerializedScalarFunction(loader,  TcbufferAsEWKT);

    // ---- tcbufferFromBinary / FromEWKB (auto-detects format) ----
    const auto B = LogicalType::BLOB;
    const auto V = LogicalType::VARCHAR;
    const auto T = TCBufferTypes::TCBUFFER();
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tcbufferFromBinary", {B}, T, TspatialFromWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tcbufferFromEWKB",   {B}, T, TspatialFromWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tcbufferFromHexWKB",  {V}, T, TspatialFromHexWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tcbufferFromHexEWKB", {V}, T, TspatialFromHexWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tcbufferFromMFJSON", {V}, T,
                       TspatialFromStringExec<&tcbuffer_from_mfjson>));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tcbufferFromText",   {V}, T,
                       TspatialFromStringExec<&tcbuffer_in>));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tcbufferFromEWKT",   {V}, T,
                       TspatialFromStringExec<&tcbuffer_in>));
}


void TCBufferTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader,  LogicalType::VARCHAR, TCBufferTypes::TCBUFFER(), TcbufferFunctions::StringToTcbuffer);
    RegisterMeosCastFunction(loader,  TCBufferTypes::TCBUFFER(), LogicalType::VARCHAR, TcbufferFunctions::TcbufferToString);
}

}
