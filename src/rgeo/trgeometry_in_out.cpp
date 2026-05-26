#include "rgeo/trgeometry.hpp"
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
    #include <meos_pose.h>
}

// meos_rgeo.h is intentionally not included (its trgeometry_append_tinstant
// prototype takes a MEOS Interval, which collides with duckdb::Interval).
// Declare only the trgeometry_* I/O symbols this translation unit needs;
// every name is the post-uniformization symbol exported from meos_rgeo.h.
extern "C" {
    extern Temporal *trgeometry_in(const char *str);
    extern Temporal *trgeometry_from_mfjson(const char *mfjson);
    extern char *trgeometry_out(const Temporal *temp);
    // trgeo_parse is the correct WKT parser for a temporal rigid geometry
    // ('Geometry;Pose@t...'): it parses the reference geometry first (split on
    // ';') then the temporal pose. It is exported from libmeos (declared in
    // the internal rgeo/trgeo_parser.h). See trgeometry_parse_wkt below.
    extern Temporal *trgeo_parse(const char **str, MeosType temptype);
    // WKT/EWKT output for a temporal rigid geometry ('geometry;pose' with the
    // reference geometry as WKT). trgeometry_out (above) emits the HexWKB geom
    // form; asText/asEWKT need this WKT form (MobilityDB's Trgeometry_as_text /
    // Trgeometry_as_ewkt wrap it with extended=false/true). meos_rgeo.h does
    // not expose it, but it is exported from libmeos (internal trgeo.c).
    extern char *trgeo_wkt_out(const Temporal *temp, int maxdd, bool extended);
}

namespace duckdb {

// MEOS BUG (pinned bb659c693): the public trgeometry_in wrapper calls
// tspatial_parse, which has no reference-geometry handling, so any WKT
// trgeometry literal fails "parse error - invalid geometry". The correct
// (and exported) parser is trgeo_parse. Route to it directly until the pin
// includes the upstream one-line fix (trgeometry_in must call trgeo_parse).
static inline Temporal *trgeometry_parse_wkt(const char *str) {
    const char *p = str;
    return trgeo_parse(&p, T_TRGEOMETRY);
}

inline void Trgeometry_as_text_exec(DataChunk &args, ExpressionState &state, Vector &result) {
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

            // asText: reference geometry as WKT, a ';' delimiter, then the
            // temporal pose. trgeo_wkt_out(extended=false) builds exactly that
            // (trgeometry_out emits the HexWKB geom form instead).
            char *str = trgeo_wkt_out(temp, 15, false);

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

inline void Trgeometry_as_ewkt_exec(DataChunk &args, ExpressionState &state, Vector &result) {
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

            // asEWKT: like asText but the reference geometry carries its SRID
            // prefix. trgeo_wkt_out(extended=true) builds the rigid-geometry
            // EWKT (the generic tspatial_as_ewkt drops the reference geometry).
            char *ewkt = trgeo_wkt_out(temp, 15, true);

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


bool TrgeometryFunctions::StringToTrgeometry(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_string) -> string_t {
            std::string input_str = input_string.GetString();

            Temporal *temp = trgeometry_parse_wkt(input_str.c_str());
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

bool TrgeometryFunctions::TrgeometryToString(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
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

            char *str = trgeometry_out(temp);
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
// `trgeometry_in` and `trgeometry_from_mfjson` are the renamed,
// header-exported per-type entry points (post API-uniformization they
// are real linkable symbols, so this is a clean clone of the canonical
// template rather than a tspatial_parse work-around).  The result is
// stored as a raw blob, the same format every other temporal type uses.

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

inline void TrgeometryFromTextExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            Temporal *t = trgeometry_parse_wkt(s.c_str());
            if (!t) throw InvalidInputException("from*: invalid input");
            return StoreTempAsBlob(result, t);
        });
}

inline void TrgeometryFromMFJSONExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            // The renamed trgeometry_from_mfjson symbol is exported from
            // meos_rgeo.h; it is the canonical per-type MF-JSON entry
            // point the MobilityDB SQL binds trgeometryFromMFJSON to.
            Temporal *t = trgeometry_from_mfjson(s.c_str());
            if (!t) throw InvalidInputException("fromMFJSON: invalid input");
            return StoreTempAsBlob(result, t);
        });
}

// Output serialization for trgeometry: asMFJSON (temporal_as_mfjson) and
// asBinary/asEWKB/asHexWKB/asHexEWKB (temporal_as_wkb / temporal_as_hexwkb).
// MobilityDB exposes all of these for trgeometry; the #153 port shipped only
// the From* parsers. Uniquely named per the ODR caveat. WKB_BASE (no SRID) is
// local; WKB_EXTENDED from meos_geo.h.
constexpr uint8_t WKB_BASE = 0x00;

static Temporal *TrgeometryBlobToTemp(const string_t &blob) {
    uint8_t *copy = (uint8_t *)malloc(blob.GetSize());
    if (!copy) throw InternalException("trgeometry blob->temporal: malloc failed");
    memcpy(copy, blob.GetData(), blob.GetSize());
    return reinterpret_cast<Temporal *>(copy);
}

void TrgeometryAsMfjsonExec(DataChunk &args, ExpressionState &state, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t i = 0; i < args.ColumnCount(); i++) args.data[i].Flatten(row_count);
    auto in = FlatVector::GetData<string_t>(args.data[0]);
    auto out_data = FlatVector::GetData<string_t>(result);
    auto &out_validity = FlatVector::Validity(result);
    const idx_t cc = args.ColumnCount();
    for (idx_t row = 0; row < row_count; row++) {
        if (!FlatVector::Validity(args.data[0]).RowIsValid(row)) {
            out_validity.SetInvalid(row);
            continue;
        }
        Temporal *t = TrgeometryBlobToTemp(in[row]);
        bool with_bbox = (cc > 1) ? FlatVector::GetData<bool>(args.data[1])[row] : false;
        int flags     = (cc > 2) ? FlatVector::GetData<int32_t>(args.data[2])[row] : 0;
        int precision = (cc > 3) ? FlatVector::GetData<int32_t>(args.data[3])[row] : 15;
        std::string srs;
        const char *srs_cstr = nullptr;
        if (cc > 4) {
            string_t s = FlatVector::GetData<string_t>(args.data[4])[row];
            srs.assign(s.GetData(), s.GetSize());
            srs_cstr = srs.empty() ? nullptr : srs.c_str();
        }
        char *json = temporal_as_mfjson(t, with_bbox, flags, precision, srs_cstr);
        free(t);
        if (!json) { out_validity.SetInvalid(row); continue; }
        out_data[row] = StringVector::AddString(result, json);
        free(json);
    }
    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

void TrgeometryAsWkbExec(DataChunk &args, ExpressionState &state, Vector &result, uint8_t variant) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            Temporal *t = TrgeometryBlobToTemp(input);
            size_t sz = 0;
            uint8_t *wkb = temporal_as_wkb(t, variant, &sz);
            free(t);
            if (!wkb || sz == 0) {
                if (wkb) free(wkb);
                throw InternalException("temporal_as_wkb returned null");
            }
            string_t blob(reinterpret_cast<const char *>(wkb), sz);
            string_t stored = StringVector::AddStringOrBlob(result, blob);
            free(wkb);
            return stored;
        });
}

void TrgeometryAsHexWkbExec(DataChunk &args, ExpressionState &state, Vector &result, uint8_t variant) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            Temporal *t = TrgeometryBlobToTemp(input);
            size_t sz = 0;
            char *hex = temporal_as_hexwkb(t, variant, &sz);
            (void) sz;
            free(t);
            if (!hex) throw InternalException("temporal_as_hexwkb returned null");
            string_t stored = StringVector::AddString(result, hex);
            free(hex);
            return stored;
        });
}

void TRGeometryTypes::RegisterScalarInOutFunctions(ExtensionLoader &loader){
    auto TrgeometryAsText = ScalarFunction(
            "asText",
            {TRGeometryTypes::TRGEOMETRY()},
            LogicalType::VARCHAR,
            Trgeometry_as_text_exec
        );
        duckdb::RegisterSerializedScalarFunction(loader,  TrgeometryAsText);

    auto TrgeometryAsEWKT = ScalarFunction(
        "asEWKT",
        {TRGeometryTypes::TRGEOMETRY()},
        LogicalType::VARCHAR,
        Trgeometry_as_ewkt_exec
    );
    duckdb::RegisterSerializedScalarFunction(loader,  TrgeometryAsEWKT);

    // ---- trgeometryFromBinary / FromEWKB (auto-detects format) ----
    const auto B = LogicalType::BLOB;
    const auto V = LogicalType::VARCHAR;
    const auto T = TRGeometryTypes::TRGEOMETRY();
    const auto BL = LogicalType::BOOLEAN;
    const auto I = LogicalType::INTEGER;

    // asMFJSON(trgeometry[, with_bbox[, flags[, precision[, srs]]]])
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asMFJSON", {T},              V, TrgeometryAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asMFJSON", {T, BL},          V, TrgeometryAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asMFJSON", {T, BL, I},       V, TrgeometryAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asMFJSON", {T, BL, I, I},    V, TrgeometryAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asMFJSON", {T, BL, I, I, V}, V, TrgeometryAsMfjsonExec));

    // asBinary / asEWKB and asHexWKB / asHexEWKB
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asBinary", {T}, B,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TrgeometryAsWkbExec(a, s, r, WKB_BASE); }));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asEWKB",   {T}, B,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TrgeometryAsWkbExec(a, s, r, WKB_EXTENDED); }));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asHexWKB",  {T}, V,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TrgeometryAsHexWkbExec(a, s, r, WKB_BASE); }));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asHexEWKB", {T}, V,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TrgeometryAsHexWkbExec(a, s, r, WKB_EXTENDED); }));

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("trgeometryFromBinary", {B}, T, TspatialFromWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("trgeometryFromEWKB",   {B}, T, TspatialFromWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("trgeometryFromHexWKB",  {V}, T, TspatialFromHexWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("trgeometryFromHexEWKB", {V}, T, TspatialFromHexWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("trgeometryFromMFJSON", {V}, T, TrgeometryFromMFJSONExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("trgeometryFromText",   {V}, T, TrgeometryFromTextExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("trgeometryFromEWKT",   {V}, T, TrgeometryFromTextExec));
}


void TRGeometryTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    loader.RegisterCastFunction( LogicalType::VARCHAR, TRGeometryTypes::TRGEOMETRY(), TrgeometryFunctions::StringToTrgeometry);
    loader.RegisterCastFunction( TRGeometryTypes::TRGEOMETRY(), LogicalType::VARCHAR, TrgeometryFunctions::TrgeometryToString);
}

}
