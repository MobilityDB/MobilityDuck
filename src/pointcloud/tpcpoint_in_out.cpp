#include "pointcloud/tpcpoint.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include <regex>
#include <string>
#include <temporal/span.hpp>
#include "temporal/temporal_functions.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_geo.h>
    #include <meos_internal.h>
    #include <meos_internal_geo.h>
}

namespace duckdb {

// tpcpoint exposes NO type-specific tpcpoint_in / tpcpoint_out symbol.
// The canonical MobilityDB SQL binds tpcpoint_in / tpcpoint_out to the
// subtype-agnostic generic Temporal_in / Temporal_out, i.e.
// temporal_in(str, T_TPCPOINT) / temporal_out.  Likewise the canonical
// SQL exposes NO asText / asEWKT for tpcpoint (only asBinary / asHexWKB
// / asMFJSON, which are the generic Temporal_* wrappers), and the
// schema-dependent EWKT path is not meaningful for a pgpointcloud value
// without a registered PCSCHEMA — so this port does not register an
// asText / asEWKT overload (no unverified-parity over-emission).  Text
// I/O is provided by the VARCHAR <-> TPCPOINT cast below.

bool TpcpointFunctions::StringToTpcpoint(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_string) -> string_t {
            std::string input_str = input_string.GetString();

            // No tpcpoint_in; route through the generic dispatch with
            // the T_TPCPOINT temporal type, the same path the canonical
            // MobilityDB SQL binds tpcpoint_in to.
            Temporal *temp = temporal_in(input_str.c_str(), T_TPCPOINT);
            if (!temp) {
                throw InvalidInputException("Invalid TPCPOINT input: " + input_str);
            }

            size_t data_size = temporal_mem_size(temp);
            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(temp);
                throw InvalidInputException("Failed to allocate memory for TPCPOINT data");
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

bool TpcpointFunctions::TpcpointToString(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();

            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid TPCPOINT data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TPCPOINT deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TPCPOINT data: null pointer");
            }

            char *str = temporal_out(temp, 15);
            if (!str) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TPCPOINT to string");
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
// Used to register the `tpcpointFrom*` overloads.
// `temporal_from_wkb`, `temporal_from_hexwkb`, `temporal_from_mfjson`
// and `temporal_in` are all subtype-agnostic; tpcpoint has no
// type-specific *_in / *_from_mfjson symbol, so every constructor
// routes through the generic dispatch with the T_TPCPOINT temporal
// type, exactly as the canonical MobilityDB SQL binds tpcpointFromBinary
// / tpcpointFromHexWKB to the generic Temporal_from_wkb /
// Temporal_from_hexwkb handlers.  The result is stored as a raw blob,
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

inline void TpcpointFromTextExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            // No tpcpoint_in; route through the generic dispatch.
            Temporal *t = temporal_in(s.c_str(), T_TPCPOINT);
            if (!t) throw InvalidInputException("from*: invalid input");
            return StoreTempAsBlob(result, t);
        });
}

inline void TpcpointFromMFJSONExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            // tpcpoint exposes no header-declared *_from_mfjson symbol;
            // route through the generic dispatch with the T_TPCPOINT
            // temporal type, the same path the canonical MobilityDB SQL
            // binds tpcpointFromMFJSON to.
            Temporal *t = temporal_from_mfjson(s.c_str(), T_TPCPOINT);
            if (!t) throw InvalidInputException("fromMFJSON: invalid input");
            return StoreTempAsBlob(result, t);
        });
}

// asMFJSON(tpcpoint, options int4 DEFAULT 0, flags int4 DEFAULT 0,
//   maxdecimaldigits int4 DEFAULT 15) — the canonical MobilityDB signature.
// `options` is a bitmask whose low bit requests the bounding box. MEOS's
// generic temporal_as_mfjson renders the tpcpoint via its dedicated
// tpcpoint_coordinates_as_mfjson path, which degrades to empty coordinates
// (no crash) when no pgPointCloud PCSCHEMA is registered (the standalone
// case), so this is schema-free-safe.
static void TpcpointAsMfjsonExec(DataChunk &args, ExpressionState &, Vector &result) {
    const idx_t count = args.size();
    const idx_t cc = args.ColumnCount();
    for (idx_t i = 0; i < cc; i++) args.data[i].Flatten(count);
    auto in = FlatVector::GetData<string_t>(args.data[0]);
    auto out = FlatVector::GetData<string_t>(result);
    auto &valid = FlatVector::Validity(result);
    for (idx_t row = 0; row < count; row++) {
        if (!FlatVector::Validity(args.data[0]).RowIsValid(row)) { valid.SetInvalid(row); continue; }
        std::string buf(in[row].GetData(), in[row].GetSize());
        Temporal *t = reinterpret_cast<Temporal *>(const_cast<char *>(buf.c_str()));
        int options = (cc > 1) ? FlatVector::GetData<int32_t>(args.data[1])[row] : 0;
        int flags   = (cc > 2) ? FlatVector::GetData<int32_t>(args.data[2])[row] : 0;
        int maxdd   = (cc > 3) ? FlatVector::GetData<int32_t>(args.data[3])[row] : 15;
        // Match the canonical Temporal_as_mfjson clamp (precision in [0, 15]).
        if (maxdd > 15) maxdd = 15; else if (maxdd < 0) maxdd = 0;
        char *json = temporal_as_mfjson(t, (options & 1) != 0, flags, maxdd, nullptr);
        if (!json) { valid.SetInvalid(row); continue; }
        out[row] = StringVector::AddString(result, json);
        free(json);
    }
    if (count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

void TPcpointTypes::RegisterScalarInOutFunctions(ExtensionLoader &loader){
    // ---- tpcpointFromBinary / FromHexWKB / FromMFJSON / FromText ----
    const auto B = LogicalType::BLOB;
    const auto V = LogicalType::VARCHAR;
    const auto T = TPcpointTypes::TPCPOINT();
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tpcpointFromBinary", {B}, T, TspatialFromWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tpcpointFromHexWKB",  {V}, T, TspatialFromHexWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tpcpointFromMFJSON", {V}, T, TpcpointFromMFJSONExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("tpcpointFromText",   {V}, T, TpcpointFromTextExec));

    // ---- asBinary / asHexWKB (subtype-agnostic MEOS-WKB output) ----
    // Mirror the canonical MobilityDB tpcpoint signatures
    // asBinary(tpcpoint, endianencoding text DEFAULT '') and
    // asHexWKB(tpcpoint, endianencoding text DEFAULT ''): the optional
    // second argument selects the WKB endianness. Both bind to the
    // generic Temporal_as_wkb / Temporal_as_hexwkb wrappers (a tpcpoint
    // serializes as a plain subtype-agnostic MEOS-WKB temporal).
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asBinary", {T},    B, TemporalFunctions::Temporal_as_wkb));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asBinary", {T, V}, B, TemporalFunctions::Temporal_as_wkb));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asHexWKB", {T},    V, TemporalFunctions::Temporal_as_hexwkb));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asHexWKB", {T, V}, V, TemporalFunctions::Temporal_as_hexwkb));

    // ---- asMFJSON(tpcpoint[, options int[, flags int[, maxdecimaldigits int]]]) ----
    const auto I = LogicalType::INTEGER;
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asMFJSON", {T},          V, TpcpointAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asMFJSON", {T, I},       V, TpcpointAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asMFJSON", {T, I, I},    V, TpcpointAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("asMFJSON", {T, I, I, I}, V, TpcpointAsMfjsonExec));
}


void TPcpointTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    loader.RegisterCastFunction( LogicalType::VARCHAR, TPcpointTypes::TPCPOINT(), TpcpointFunctions::StringToTpcpoint);
    loader.RegisterCastFunction( TPcpointTypes::TPCPOINT(), LogicalType::VARCHAR, TpcpointFunctions::TpcpointToString);
}

}
