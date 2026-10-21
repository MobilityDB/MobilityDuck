#include "json/tjsonb.hpp"
#include "temporal/temporal_blob.hpp"
#include "temporal/span.hpp"
#include "temporal/temporal_functions.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "mobilityduck/meos_exec_serial.hpp"
#include "time_util.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_internal.h>
}

/* Jsonb comes from the MEOS headers (opaque pointer here). Forward-declare only
 * the MEOS entry points this file calls. */
extern "C" {
    extern Temporal  *tjsonb_in(const char *str);
    extern Jsonb     *jsonb_in(const char *str);
    extern TInstant  *tjsonbinst_make(const Jsonb *jsonb, TimestampTz t);
    extern TSequence *tjsonbseq_from_base_tstzspan(const Jsonb *jsonb, const Span *sp);
    extern Jsonb     *tjsonb_start_value(const Temporal *temp);
    extern Jsonb     *tjsonb_end_value(const Temporal *temp);
    extern bool       tjsonb_value_at_timestamptz(const Temporal *temp, TimestampTz t,
                                                  bool strict, Jsonb **value);
    extern char      *jsonb_out(const Jsonb *jb);
}

namespace duckdb {

LogicalType TJsonbTypes::jsonb() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("jsonb");
    return type;
}

/* JsonPath is a varlena (`int32 vl_len_; uint32 header; char data[]`), so the blob holds
 * the value whole and VARSIZE reads its length — the same marshalling jsonb uses. */
LogicalType TJsonbTypes::jsonpath() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("jsonpath");
    return type;
}

bool TjsonbFunctions::Jsonpath_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input) -> string_t {
            std::string s = input.GetString();
            JsonPath *jp = jsonpath_in(s.c_str());
            if (!jp)
                throw InvalidInputException("Invalid jsonpath input: " + s);
            string_t stored = StringVector::AddStringOrBlob(
                result, reinterpret_cast<const char *>(jp), VARSIZE(jp));
            free(jp);
            return stored;
        });
    return true;
}

bool TjsonbFunctions::Jsonpath_out_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            size_t sz = blob.GetSize();
            uint8_t *copy = (uint8_t *)malloc(sz);
            memcpy(copy, blob.GetData(), sz);
            char *str = jsonpath_out(reinterpret_cast<JsonPath *>(copy));
            free(copy);
            if (!str)
                throw InternalException("Failure in Jsonpath_out: jsonpath_out returned null");
            std::string out(str);
            free(str);
            return StringVector::AddString(result, out);
        });
    return true;
}

LogicalType TJsonbTypes::tjsonb() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("tjsonb");
    return type;
}

void TJsonbTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType("jsonb", TJsonbTypes::jsonb());
    loader.RegisterType("jsonpath", TJsonbTypes::jsonpath());
    loader.RegisterType("tjsonb", TJsonbTypes::tjsonb());
}

/* Base jsonb value (BLOB-alias) -> VARCHAR render cast, mirroring the cbuffer
 * sibling's Cbuffer_out_cast. The generated startValue/endValue return the jsonb
 * base value as a BLOB; DuckDB renders it through this cast (jsonb_out). */
bool TjsonbFunctions::Jsonb_out_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            size_t sz = blob.GetSize();
            uint8_t *copy = (uint8_t *)malloc(sz);
            memcpy(copy, blob.GetData(), sz);
            Jsonb *jb = reinterpret_cast<Jsonb *>(copy);
            char *str = jsonb_out(jb);
            free(jb);
            std::string s(str);
            free(str);
            return StringVector::AddString(result, s);
        });
    return true;
}

bool TjsonbFunctions::Jsonb_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input) -> string_t {
            std::string s = input.GetString();
            Jsonb *jb = jsonb_in(s.c_str());
            if (!jb)
                throw InvalidInputException("Invalid JSON input: " + s);
            string_t stored = StringVector::AddStringOrBlob(
                result, reinterpret_cast<const char *>(jb), VARSIZE(jb));
            free(jb);
            return stored;
        });
    return true;
}

/* ------------------------------------------------------------------
 * Constructors
 * ------------------------------------------------------------------ */

/* ------------------------------------------------------------------
 * Value accessors
 * ------------------------------------------------------------------ */

/* Tjsonb_start_value / Tjsonb_end_value retired: startValue/endValue are now
 * generated (return the jsonb base value, rendered via the jsonb->VARCHAR cast). */

/* ------------------------------------------------------------------
 * Registration
 * ------------------------------------------------------------------ */

void TJsonbTypes::RegisterScalarFunctions(ExtensionLoader &loader) {
    const auto T = TJsonbTypes::tjsonb();
    const auto V = LogicalType::VARCHAR;

    /* Constructors: tjsonb(jsonb, timestamptz | tstzset | tstzspan | tstzspanset) and
     * tjsonbSeq are generated (meos_json_constructor, meos_temporal_constructor). */

    /* Generic temporal functions applied to tjsonb. timeSpan/tempSubtype/interp/merge
     * are now GENERATED with identical signatures (generate-then-retire), so the hand
     * registrations are retired here. memSize/setInterp stay hand (not generated yet);
     * tjsonbInst stays hand (subtype cast). */
    RegisterSerializedScalarFunction(loader,
        ScalarFunction("tjsonbInst", {T}, T, TemporalFunctions::Temporal_to_tinstant));

    /* Value accessors — startValue/endValue, valueAtTimestamp and valueN are all
     * GENERATED (the out-parameter shape reaches the last two), so the whole
     * meos_json_accessor group is retired here and no name registers twice. */
}

} // namespace duckdb
