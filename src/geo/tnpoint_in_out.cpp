#include "geo/tnpoint.hpp"
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
    #include <meos_npoint.h>
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
                throw InvalidInputException("Invalid TNPOINT data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TNPOINT deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TNPOINT data: null pointer");
            }

            char *str = tspatial_as_text(temp, 0);

            if (!str) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TNPOINT to text");
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
                throw InvalidInputException("Invalid TNPOINT data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TNPOINT deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TNPOINT data: null pointer");
            }

            char *ewkt = tspatial_as_ewkt(temp, 0);

            if (!ewkt) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TNPOINT to EWKT");
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


bool TnpointFunctions::StringToTnpoint(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_string) -> string_t {
            std::string input_str = input_string.GetString();

            Temporal *temp = tnpoint_in(input_str.c_str());
            if (!temp) {
                throw InvalidInputException("Invalid TNPOINT input: " + input_str);
            }

            size_t data_size = temporal_mem_size(temp);
            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(temp);
                throw InvalidInputException("Failed to allocate memory for TNPOINT data");
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

bool TnpointFunctions::TnpointToString(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();

            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid TNPOINT data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TNPOINT deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TNPOINT data: null pointer");
            }

            char *str = temporal_out(temp, 15);
            if (!str) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TNPOINT to string");
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
// Used to register the `tnpointFrom*` overloads.
// `temporal_from_wkb` and `temporal_from_hexwkb` are subtype-agnostic;
// `tnpoint_in` is per-type.  Unlike the tcbuffer port, MEOS does not
// expose a dedicated `tnpoint_from_mfjson(const char *)` symbol: the
// network-point MF-JSON support added on MobilityDB PR #951 is wired
// through the generic `temporal_from_mfjson(mfjson, T_TNPOINT)` dispatch
// (the MovingNetworkPoint typestring with a {"route":rid,"position":pos}
// value object), exactly as the canonical MobilityDB SQL binds it to the
// generic Temporal_from_mfjson handler.  The result is stored as a raw
// blob, the same format every other temporal type uses.

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

inline void TnpointFromTextExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            Temporal *t = tnpoint_in(s.c_str());
            if (!t) throw InvalidInputException("from*: invalid input");
            return StoreTempAsBlob(result, t);
        });
}

inline void TnpointFromMFJSONExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            // tnpoint has no dedicated *_from_mfjson symbol; route through
            // the generic dispatch with the T_TNPOINT temporal type, the
            // same path the canonical MobilityDB SQL binds.
            Temporal *t = temporal_from_mfjson(s.c_str(), T_TNPOINT);
            if (!t) throw InvalidInputException("fromMFJSON: invalid input");
            return StoreTempAsBlob(result, t);
        });
}

// Output serialization for tnpoint: asMFJSON (temporal_as_mfjson) and
// asBinary/asEWKB/asHexWKB/asHexEWKB (temporal_as_wkb / temporal_as_hexwkb).
// MobilityDB exposes all of these for tnpoint; the #150 port shipped only the
// From* parsers. Uniquely named per the ODR caveat. WKB_BASE (no SRID) is
// local; WKB_EXTENDED from meos_geo.h.
constexpr uint8_t WKB_BASE = 0x00;

static Temporal *TnpointBlobToTemp(const string_t &blob) {
    uint8_t *copy = (uint8_t *)malloc(blob.GetSize());
    if (!copy) throw InternalException("tnpoint blob->temporal: malloc failed");
    memcpy(copy, blob.GetData(), blob.GetSize());
    return reinterpret_cast<Temporal *>(copy);
}

void TnpointAsMfjsonExec(DataChunk &args, ExpressionState &state, Vector &result) {
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
        Temporal *t = TnpointBlobToTemp(in[row]);
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

void TnpointAsWkbExec(DataChunk &args, ExpressionState &state, Vector &result, uint8_t variant) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            Temporal *t = TnpointBlobToTemp(input);
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

void TnpointAsHexWkbExec(DataChunk &args, ExpressionState &state, Vector &result, uint8_t variant) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            Temporal *t = TnpointBlobToTemp(input);
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

void TNpointTypes::RegisterScalarInOutFunctions(ExtensionLoader &loader){
    auto TnpointAsText = ScalarFunction(
            "asText",
            {TNpointTypes::TNPOINT()},
            LogicalType::VARCHAR,
            Tspatial_as_text
        );
        duckdb::RegisterSerializedScalarFunction(loader,  TnpointAsText);

    auto TnpointAsEWKT = ScalarFunction(
        "asEWKT",
        {TNpointTypes::TNPOINT()},
        LogicalType::VARCHAR,
        Tspatial_as_ewkt
    );
    duckdb::RegisterSerializedScalarFunction(loader,  TnpointAsEWKT);

    // ---- tnpointFromBinary / FromEWKB (auto-detects format) ----
    const auto B = LogicalType::BLOB;
    const auto V = LogicalType::VARCHAR;
    const auto T = TNpointTypes::TNPOINT();
    const auto BL = LogicalType::BOOLEAN;
    const auto I = LogicalType::INTEGER;

    // asMFJSON(tnpoint[, with_bbox[, flags[, precision[, srs]]]])
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asMFJSON", {T},              V, TnpointAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asMFJSON", {T, BL},          V, TnpointAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asMFJSON", {T, BL, I},       V, TnpointAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asMFJSON", {T, BL, I, I},    V, TnpointAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asMFJSON", {T, BL, I, I, V}, V, TnpointAsMfjsonExec));

    // asBinary / asEWKB and asHexWKB / asHexEWKB
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asBinary", {T}, B,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TnpointAsWkbExec(a, s, r, WKB_BASE); }));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asEWKB",   {T}, B,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TnpointAsWkbExec(a, s, r, WKB_EXTENDED); }));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asHexWKB",  {T}, V,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TnpointAsHexWkbExec(a, s, r, WKB_BASE); }));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asHexEWKB", {T}, V,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TnpointAsHexWkbExec(a, s, r, WKB_EXTENDED); }));

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tnpointFromBinary", {B}, T, TspatialFromWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tnpointFromEWKB",   {B}, T, TspatialFromWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tnpointFromHexWKB",  {V}, T, TspatialFromHexWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tnpointFromHexEWKB", {V}, T, TspatialFromHexWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tnpointFromMFJSON", {V}, T, TnpointFromMFJSONExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tnpointFromText",   {V}, T, TnpointFromTextExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tnpointFromEWKT",   {V}, T, TnpointFromTextExec));
}


void TNpointTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    loader.RegisterCastFunction( LogicalType::VARCHAR, TNpointTypes::TNPOINT(), TnpointFunctions::StringToTnpoint);
    loader.RegisterCastFunction( TNpointTypes::TNPOINT(), LogicalType::VARCHAR, TnpointFunctions::TnpointToString);
}

}
