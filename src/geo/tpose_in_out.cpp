#include "geo/tpose.hpp"
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
    #include <meos_pose.h>
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
                throw InvalidInputException("Invalid TPOSE data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TPOSE deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TPOSE data: null pointer");
            }

            char *str = tspatial_as_text(temp, 15);

            if (!str) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TPOSE to text");
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
                throw InvalidInputException("Invalid TPOSE data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TPOSE deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TPOSE data: null pointer");
            }

            char *ewkt = tspatial_as_ewkt(temp, 15);

            if (!ewkt) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TPOSE to EWKT");
            }

            std::string result_str(ewkt);
            string_t stored_result = StringVector::AddString(result, result_str);


            free(ewkt);
            free(data_copy);

            return stored_result;
        }
    );

}


bool TposeFunctions::StringToTpose(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_string) -> string_t {
            std::string input_str = input_string.GetString();

            Temporal *temp = tpose_in(input_str.c_str());
            if (!temp) {
                throw InvalidInputException("Invalid TPOSE input: " + input_str);
            }

            size_t data_size = temporal_mem_size(temp);
            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(temp);
                throw InvalidInputException("Failed to allocate memory for TPOSE data");
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

bool TposeFunctions::TposeToString(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();

            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid TPOSE data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TPOSE deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TPOSE data: null pointer");
            }

            char *str = temporal_out(temp, 15);
            if (!str) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TPOSE to string");
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
// Used to register the `tposeFrom*` overloads.
// `temporal_from_wkb` and `temporal_from_hexwkb` are subtype-agnostic;
// `tpose_in` is per-type.  The temporal-pose MF-JSON support already
// lives on MobilityDB master (MovingPose typestring + dispatch), so no
// preceding MEOS parity PR is needed; MEOS does not, however, expose a
// header-declared `tpose_from_mfjson(const char *)` symbol (it is built
// under #if MEOS and is itself only a thin wrapper that calls
// `temporal_from_mfjson(mfjson, T_TPOSE)`).  This port therefore routes
// through that subtype-agnostic dispatch directly, exactly as the
// canonical MobilityDB SQL binds `tposeFromMFJSON` to the generic
// Temporal_from_mfjson handler.  The result is stored as a raw blob,
// the same format every other temporal type uses.


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

static void TposeFromTextExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            Temporal *t = tpose_in(s.c_str());
            if (!t) throw InvalidInputException("from*: invalid input");
            return TemporalToBlob(result, t);
        });
}

static void TposeFromMFJSONExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            // tpose exposes no header-declared *_from_mfjson symbol;
            // route through the generic dispatch with the T_TPOSE
            // temporal type, the same path the canonical MobilityDB SQL
            // binds tposeFromMFJSON to.
            Temporal *t = temporal_from_mfjson(s.c_str(), T_TPOSE);
            if (!t) throw InvalidInputException("fromMFJSON: invalid input");
            return TemporalToBlob(result, t);
        });
}

// asMFJSON(tpose[, with_bbox[, flags[, precision[, srs]]]]). MobilityDB
// exposes asMFJSON for every temporal type via the generic Temporal_as_mfjson;
// #151 shipped tposeFromMFJSON but not asMFJSON, so this closes that gap.
// Uniquely named (the ODR caveat): a generic name would collide with the
// asMFJSON execs in the other geo .cpp. Defaults match MobilityDB:
// with_bbox=false, flags=0, precision=15, srs=NULL.
void TposeAsMfjsonExec(DataChunk &args, ExpressionState &state, Vector &result) {
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
        string_t blob = in[row];
        uint8_t *copy = (uint8_t *)malloc(blob.GetSize());
        if (!copy) throw InternalException("asMFJSON: malloc failed");
        memcpy(copy, blob.GetData(), blob.GetSize());
        Temporal *t = reinterpret_cast<Temporal *>(copy);
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
        free(copy);
        if (!json) { out_validity.SetInvalid(row); continue; }
        out_data[row] = StringVector::AddString(result, json);
        free(json);
    }
}

// WKB output for tpose. #151 shipped the From{Binary,HexWKB,...} parsers but
// not the as{Binary,EWKB,HexWKB,HexEWKB} emitters; MobilityDB exposes both
// (via the generic temporal_as_wkb / temporal_as_hexwkb). Uniquely named per
// the ODR caveat. Static helper for the blob -> Temporal copy.
// WKB_BASE (no SRID) is a local constant; WKB_EXTENDED (0x04) is from meos_geo.h.
constexpr uint8_t WKB_BASE = 0x00;
static Temporal *TposeBlobToTemp(const string_t &blob) {
    uint8_t *copy = (uint8_t *)malloc(blob.GetSize());
    if (!copy) throw InternalException("tpose blob->temporal: malloc failed");
    memcpy(copy, blob.GetData(), blob.GetSize());
    return reinterpret_cast<Temporal *>(copy);
}

void TposeAsWkbExec(DataChunk &args, ExpressionState &state, Vector &result, uint8_t variant) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            Temporal *t = TposeBlobToTemp(input);
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

void TposeAsHexWkbExec(DataChunk &args, ExpressionState &state, Vector &result, uint8_t variant) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            Temporal *t = TposeBlobToTemp(input);
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

void TPoseTypes::RegisterScalarInOutFunctions(ExtensionLoader &loader){
    auto TposeAsText = ScalarFunction(
            "asText",
            {TPoseTypes::TPOSE()},
            LogicalType::VARCHAR,
            Tspatial_as_text
        );
        duckdb::RegisterSerializedScalarFunction(loader,  TposeAsText);

    auto TposeAsEWKT = ScalarFunction(
        "asEWKT",
        {TPoseTypes::TPOSE()},
        LogicalType::VARCHAR,
        Tspatial_as_ewkt
    );
    duckdb::RegisterSerializedScalarFunction(loader,  TposeAsEWKT);

    // ---- tposeFromBinary / FromEWKB (auto-detects format) ----
    const auto B = LogicalType::BLOB;
    const auto V = LogicalType::VARCHAR;
    const auto T = TPoseTypes::TPOSE();
    const auto BL = LogicalType::BOOLEAN;
    const auto I = LogicalType::INTEGER;

    // asMFJSON(tpose[, with_bbox[, flags[, precision[, srs]]]])
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asMFJSON", {T},                 V, TposeAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asMFJSON", {T, BL},             V, TposeAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asMFJSON", {T, BL, I},          V, TposeAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asMFJSON", {T, BL, I, I},       V, TposeAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asMFJSON", {T, BL, I, I, V},    V, TposeAsMfjsonExec));

    // asBinary / asEWKB (base + extended WKB) and asHexWKB / asHexEWKB
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asBinary", {T}, B,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TposeAsWkbExec(a, s, r, WKB_BASE); }));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asEWKB",   {T}, B,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TposeAsWkbExec(a, s, r, WKB_EXTENDED); }));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asHexWKB",  {T}, V,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TposeAsHexWkbExec(a, s, r, WKB_BASE); }));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asHexEWKB", {T}, V,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TposeAsHexWkbExec(a, s, r, WKB_EXTENDED); }));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tposeFromBinary", {B}, T, TspatialFromWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tposeFromEWKB",   {B}, T, TspatialFromWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tposeFromHexWKB",  {V}, T, TspatialFromHexWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tposeFromHexEWKB", {V}, T, TspatialFromHexWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tposeFromMFJSON", {V}, T, TposeFromMFJSONExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tposeFromText",   {V}, T, TposeFromTextExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tposeFromEWKT",   {V}, T, TposeFromTextExec));
}


void TPoseTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader,  LogicalType::VARCHAR, TPoseTypes::TPOSE(), TposeFunctions::StringToTpose);
    RegisterMeosCastFunction(loader,  TPoseTypes::TPOSE(), LogicalType::VARCHAR, TposeFunctions::TposeToString);
}

}
