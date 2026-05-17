#include "geo/trgeo.hpp"
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
    // meos_rgeo.h is deliberately NOT included: its Interval-typed
    // trgeo_append_tinstant prototype makes `Interval` ambiguous against
    // duckdb::Interval at this namespace scope (see geo/trgeo.cpp). Only
    // trgeo_out is needed here; declared locally.
    extern char *trgeo_out(const Temporal *temp);
    // See geo/trgeo.cpp: trgeo_in is a MEOS inline (#if MEOS) wrapper
    // with no linkable symbol and is not declared in the installed
    // meos_rgeo.h. This port routes text/EWKT input through the
    // subtype-agnostic spatial parser tspatial_parse with T_TRGEOMETRY
    // (the exact path trgeo_in itself delegates to and the canonical
    // MobilityDB SQL binds trgeometryFromText / trgeometry_in to).
    extern Temporal *tspatial_parse(const char **str, MeosType temptype);
}

namespace duckdb {

static inline Temporal *Trgeo_parse_in(const std::string &str) {
    const char *ptr = str.c_str();
    return tspatial_parse(&ptr, T_TRGEOMETRY);
}

inline void Tspatial_as_text(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_geom_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_geom_vec, result, count,
        [&](string_t input_geom_str) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_geom_str.GetData());
            size_t data_size = input_geom_str.GetSize();

            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid TRGEOMETRY data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TRGEOMETRY deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TRGEOMETRY data: null pointer");
            }

            char *str = tspatial_as_text(temp, 0);

            if (!str) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TRGEOMETRY to text");
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
                throw InvalidInputException("Invalid TRGEOMETRY data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TRGEOMETRY deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TRGEOMETRY data: null pointer");
            }

            char *ewkt = tspatial_as_ewkt(temp, 0);

            if (!ewkt) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TRGEOMETRY to EWKT");
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


bool TrgeoFunctions::StringToTrgeo(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_string) -> string_t {
            std::string input_str = input_string.GetString();

            Temporal *temp = Trgeo_parse_in(input_str);
            if (!temp) {
                throw InvalidInputException("Invalid TRGEOMETRY input: " + input_str);
            }

            size_t data_size = temporal_mem_size(temp);
            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(temp);
                throw InvalidInputException("Failed to allocate memory for TRGEOMETRY data");
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

bool TrgeoFunctions::TrgeoToString(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();

            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid TRGEOMETRY data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TRGEOMETRY deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TRGEOMETRY data: null pointer");
            }

            // The trgeometry value is a moving rigid geometry; trgeo_out
            // is the type-specific serializer that renders the reference
            // geometry plus the embedded temporal pose. The generic
            // temporal_out does not understand the rigid value model.
            char *str = trgeo_out(temp);
            if (!str) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TRGEOMETRY to string");
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
// Used to register the `trgeometryFrom*` overloads.
// `temporal_from_wkb` and `temporal_from_hexwkb` are subtype-agnostic;
// the trgeometry text/EWKT parser routes through tspatial_parse with
// T_TRGEOMETRY.  The temporal-rigid-geometry MF-JSON support already
// lives on MobilityDB master (MovingRigidGeometry typestring + dispatch),
// so no preceding MEOS parity PR is needed; MEOS does not, however,
// expose a header-declared `trgeo_from_mfjson(const char *)` symbol (it
// is an `#if MEOS` inline that calls `temporal_from_mfjson(mfjson,
// T_TRGEOMETRY)`).  This port therefore routes through that
// subtype-agnostic dispatch directly, exactly as the canonical
// MobilityDB SQL binds `trgeometryFromMFJSON` to the generic
// Temporal_from_mfjson handler.  The result is stored as a raw blob,
// the same format every other temporal type uses.

inline string_t StoreTempAsBlob(Vector &result, Temporal *t) {
    size_t sz = temporal_mem_size(t);
    string_t stored = StringVector::AddStringOrBlob(
        result, string_t(reinterpret_cast<const char *>(t), sz));
    free(t);
    return stored;
}

inline void TspatialFromWkbExec(DataChunk &args, ExpressionState &, Vector &result) {
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
            return StoreTempAsBlob(result, t);
        });
}

inline void TspatialFromHexWkbExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string hex(input.GetData(), input.GetSize());
            Temporal *t = temporal_from_hexwkb(hex.c_str());
            if (!t) throw InvalidInputException(
                "fromHexWKB: invalid hex-encoded MEOS-WKB");
            return StoreTempAsBlob(result, t);
        });
}

inline void TrgeoFromTextExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            Temporal *t = Trgeo_parse_in(s);
            if (!t) throw InvalidInputException("from*: invalid input");
            return StoreTempAsBlob(result, t);
        });
}

inline void TrgeoFromMFJSONExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            // trgeo exposes no header-declared *_from_mfjson symbol;
            // route through the generic dispatch with the T_TRGEOMETRY
            // temporal type, the same path the canonical MobilityDB SQL
            // binds trgeometryFromMFJSON to.
            Temporal *t = temporal_from_mfjson(s.c_str(), T_TRGEOMETRY);
            if (!t) throw InvalidInputException("fromMFJSON: invalid input");
            return StoreTempAsBlob(result, t);
        });
}

void TRGeoTypes::RegisterScalarInOutFunctions(ExtensionLoader &loader){
    auto TrgeoAsText = ScalarFunction(
            "asText",
            {TRGeoTypes::TRGEOMETRY()},
            LogicalType::VARCHAR,
            Tspatial_as_text
        );
        duckdb::RegisterSerializedScalarFunction(loader,  TrgeoAsText);

    auto TrgeoAsEWKT = ScalarFunction(
        "asEWKT",
        {TRGeoTypes::TRGEOMETRY()},
        LogicalType::VARCHAR,
        Tspatial_as_ewkt
    );
    duckdb::RegisterSerializedScalarFunction(loader,  TrgeoAsEWKT);

    // ---- trgeometryFromBinary / FromEWKB (auto-detects format) ----
    const auto B = LogicalType::BLOB;
    const auto V = LogicalType::VARCHAR;
    const auto T = TRGeoTypes::TRGEOMETRY();
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("trgeometryFromBinary", {B}, T, TspatialFromWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("trgeometryFromEWKB",   {B}, T, TspatialFromWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("trgeometryFromHexWKB",  {V}, T, TspatialFromHexWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("trgeometryFromHexEWKB", {V}, T, TspatialFromHexWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("trgeometryFromMFJSON", {V}, T, TrgeoFromMFJSONExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("trgeometryFromText",   {V}, T, TrgeoFromTextExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("trgeometryFromEWKT",   {V}, T, TrgeoFromTextExec));
}


void TRGeoTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    loader.RegisterCastFunction( LogicalType::VARCHAR, TRGeoTypes::TRGEOMETRY(), TrgeoFunctions::StringToTrgeo);
    loader.RegisterCastFunction( TRGeoTypes::TRGEOMETRY(), LogicalType::VARCHAR, TrgeoFunctions::TrgeoToString);
}

}
