#include "json/tjsonb.hpp"
#include "temporal/temporal_blob.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_internal.h>
}

/* meos_json.h requires pgtypes headers not installed in the public MEOS package.
 * Forward-declare the three entry points used by this translation unit. */
extern "C" {
    extern Temporal *tjsonb_in(const char *str);
    extern char     *tjsonb_out(const Temporal *temp);
    extern Temporal *tjsonb_from_mfjson(const char *str);
}

namespace duckdb {

bool TjsonbFunctions::StringToTjsonb(Vector &source, Vector &result, idx_t count,
                                      CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_string) -> string_t {
            std::string s = input_string.GetString();
            Temporal *temp = tjsonb_in(s.c_str());
            if (!temp)
                throw InvalidInputException("Invalid TJSONB input: " + s);
            return TemporalToBlob(result, temp);
        });
    return true;
}

bool TjsonbFunctions::TjsonbToString(Vector &source, Vector &result, idx_t count,
                                      CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) -> string_t {
            Temporal *temp = BlobToTemporal(input_blob);
            char *str = tjsonb_out(temp);
            free(temp);
            if (!str)
                throw InvalidInputException("Failed to serialize TJSONB to string");
            string_t stored = StringVector::AddString(result, str);
            free(str);
            return stored;
        });
    return true;
}

static void TjsonbFromWkbExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            if (input.GetSize() == 0)
                throw InvalidInputException("tjsonbFromBinary: empty input");
            uint8_t *wkb = static_cast<uint8_t *>(malloc(input.GetSize()));
            if (!wkb) throw InternalException("tjsonbFromBinary: malloc failed");
            memcpy(wkb, input.GetData(), input.GetSize());
            Temporal *t = temporal_from_wkb(wkb, input.GetSize());
            free(wkb);
            if (!t) throw InvalidInputException("tjsonbFromBinary: invalid WKB");
            return TemporalToBlob(result, t);
        });
}

static void TjsonbFromHexWkbExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string hex(input.GetData(), input.GetSize());
            Temporal *t = temporal_from_hexwkb(hex.c_str());
            if (!t) throw InvalidInputException("tjsonbFromHexWKB: invalid hex-encoded WKB");
            return TemporalToBlob(result, t);
        });
}

template <Temporal *(*FN)(const char *)>
static void TjsonbFromStringExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            Temporal *t = FN(s.c_str());
            if (!t) throw InvalidInputException("tjsonbFrom*: invalid input");
            return TemporalToBlob(result, t);
        });
}

static void TjsonbAsHexWkbExec(DataChunk &args, ExpressionState &, Vector &result,
                                uint8_t variant) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            Temporal *t = BlobToTemporal(input);
            size_t sz = 0;
            char *hex = temporal_as_hexwkb(t, variant, &sz);
            free(t);
            (void) sz;
            if (!hex) throw InternalException("asHexWKB: temporal_as_hexwkb failed");
            string_t stored = StringVector::AddString(result, hex);
            free(hex);
            return stored;
        });
}

void TJsonbTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, TJsonbTypes::TJSONB(),
                             TjsonbFunctions::StringToTjsonb);
    RegisterMeosCastFunction(loader, TJsonbTypes::TJSONB(), LogicalType::VARCHAR,
                             TjsonbFunctions::TjsonbToString);
}

void TJsonbTypes::RegisterScalarInOutFunctions(ExtensionLoader &loader) {
    const auto B = LogicalType::BLOB;
    const auto V = LogicalType::VARCHAR;
    const auto T = TJsonbTypes::TJSONB();

    RegisterSerializedScalarFunction(loader,
        ScalarFunction("tjsonbFromBinary",  {B}, T, TjsonbFromWkbExec));
    RegisterSerializedScalarFunction(loader,
        ScalarFunction("tjsonbFromEWKB",    {B}, T, TjsonbFromWkbExec));
    RegisterSerializedScalarFunction(loader,
        ScalarFunction("tjsonbFromHexWKB",  {V}, T, TjsonbFromHexWkbExec));
    RegisterSerializedScalarFunction(loader,
        ScalarFunction("tjsonbFromHexEWKB", {V}, T, TjsonbFromHexWkbExec));
    RegisterSerializedScalarFunction(loader,
        ScalarFunction("tjsonbFromMFJSON",  {V}, T,
                       TjsonbFromStringExec<&tjsonb_from_mfjson>));
    RegisterSerializedScalarFunction(loader,
        ScalarFunction("tjsonbFromText",    {V}, T,
                       TjsonbFromStringExec<&tjsonb_in>));

    RegisterSerializedScalarFunction(loader,
        ScalarFunction("asHexWKB", {T}, V,
            [](DataChunk &a, ExpressionState &s, Vector &r) {
                TjsonbAsHexWkbExec(a, s, r, 0x00);
            }));
    RegisterSerializedScalarFunction(loader,
        ScalarFunction("asHexEWKB", {T}, V,
            [](DataChunk &a, ExpressionState &s, Vector &r) {
                TjsonbAsHexWkbExec(a, s, r, 0x04);
            }));
}

} // namespace duckdb
