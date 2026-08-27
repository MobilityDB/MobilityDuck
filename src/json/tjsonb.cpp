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

LogicalType TJsonbTypes::tjsonb() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("tjsonb");
    return type;
}

void TJsonbTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType("jsonb", TJsonbTypes::jsonb());
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

static void Tjsonb_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_str) -> string_t {
            std::string s = input_str.GetString();
            Temporal *temp = tjsonb_in(s.c_str());
            if (!temp)
                throw InvalidInputException("Invalid tjsonb input: " + s);
            return TemporalToBlob(result, temp);
        });
}

/* Two-argument instant constructor: tjsonb(json_text, timestamptz) */
static void Tjsonbinst_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, timestamp_tz_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t json_str, timestamp_tz_t t) -> string_t {
            std::string s = json_str.GetString();
            Jsonb *jb = jsonb_in(s.c_str());
            if (!jb)
                throw InvalidInputException("Invalid JSON input: " + s);
            timestamp_tz_t meos_ts = DuckDBToMeosTimestamp(t);
            TInstant *inst = tjsonbinst_make(jb, static_cast<TimestampTz>(meos_ts.value));
            free(jb);
            if (!inst)
                throw InvalidInputException("Failed to create tjsonb instant");
            return TemporalToBlob(result, reinterpret_cast<Temporal *>(inst));
        });
}

/* Step sequence from a constant JSON value over a tstzspan */
static void Tjsonb_sequence_from_tstzspan(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t json_str, string_t span_blob) -> string_t {
            std::string sj = json_str.GetString();
            Jsonb *jb = jsonb_in(sj.c_str());
            if (!jb)
                throw InvalidInputException("Invalid JSON input: " + sj);
            size_t sp_sz = span_blob.GetSize();
            uint8_t *sp_copy = static_cast<uint8_t *>(malloc(sp_sz));
            if (!sp_copy) { free(jb); throw InternalException("malloc failed for span copy"); }
            memcpy(sp_copy, span_blob.GetData(), sp_sz);
            Span *sp = reinterpret_cast<Span *>(sp_copy);
            TSequence *seq = tjsonbseq_from_base_tstzspan(jb, sp);
            free(jb);
            free(sp_copy);
            if (!seq)
                throw InvalidInputException("Failed to create tjsonb sequence");
            return TemporalToBlob(result, reinterpret_cast<Temporal *>(seq));
        });
}

/* Extract TInstant* array from a LIST(tjsonb) vector entry */
static TInstant **temparr_extract_tjsonb(Vector &arr_vec, list_entry_t entry, int *count) {
    auto &child = ListVector::GetEntry(arr_vec);
    auto len    = entry.length;
    auto off    = entry.offset;
    if (len == 0) { *count = 0; return nullptr; }
    *count = static_cast<int>(len);
    TInstant **insts = static_cast<TInstant **>(malloc(sizeof(TInstant *) * len));
    if (!insts) { *count = 0; return nullptr; }
    for (idx_t i = 0; i < len; i++) {
        string_t blob = FlatVector::GetData<string_t>(child)[off + i];
        size_t sz = blob.GetSize();
        uint8_t *copy = static_cast<uint8_t *>(malloc(sz));
        if (!copy) {
            for (idx_t j = 0; j < i; j++) free(insts[j]);
            free(insts); *count = 0; return nullptr;
        }
        memcpy(copy, blob.GetData(), sz);
        insts[i] = reinterpret_cast<TInstant *>(copy);
    }
    return insts;
}

/* Sequence constructor from LIST(tjsonb) instants */
static void Tjsonb_sequence_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    const char *default_interp = "step";
    auto count = args.size();

    args.data[0].Flatten(count);
    result.Flatten(count);

    auto arr_data    = FlatVector::GetData<list_entry_t>(args.data[0]);
    auto result_data = FlatVector::GetData<string_t>(result);
    auto &arr_val    = FlatVector::Validity(args.data[0]);
    auto &res_val    = FlatVector::Validity(result);

    interpType interp = interptype_from_string(default_interp);

    for (idx_t i = 0; i < count; i++) {
        if (!arr_val.RowIsValid(i)) { res_val.SetInvalid(i); continue; }
        int ninsts = 0;
        TInstant **insts = temparr_extract_tjsonb(args.data[0], arr_data[i], &ninsts);
        if (!insts || ninsts == 0) { res_val.SetInvalid(i); continue; }
        TSequence *seq = tsequence_make(
            (TInstant **) insts, ninsts,
            true, true, interp, true);
        for (int j = 0; j < ninsts; j++) free(insts[j]);
        free(insts);
        if (!seq) { res_val.SetInvalid(i); continue; }
        size_t sz = temporal_mem_size(reinterpret_cast<Temporal *>(seq));
        result_data[i] = StringVector::AddStringOrBlob(
            result, reinterpret_cast<const char *>(seq), sz);
        free(seq);
    }
}

/* ------------------------------------------------------------------
 * Value accessors
 * ------------------------------------------------------------------ */

/* Tjsonb_start_value / Tjsonb_end_value retired: startValue/endValue are now
 * generated (return the jsonb base value, rendered via the jsonb->VARCHAR cast). */

static void Tjsonb_value_at_timestamp(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, timestamp_tz_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_blob, timestamp_tz_t t) -> string_t {
            Temporal *temp = BlobToTemporal(input_blob);
            timestamp_tz_t meos_ts = DuckDBToMeosTimestamp(t);
            Jsonb *jb = nullptr;
            bool found = tjsonb_value_at_timestamptz(
                temp, static_cast<TimestampTz>(meos_ts.value), true, &jb);
            free(temp);
            if (!found || !jb)
                throw InvalidInputException("tjsonb valueAtTimestamp: no value at given timestamp");
            char *str = jsonb_out(jb);
            free(jb);
            if (!str)
                throw InvalidInputException("tjsonb valueAtTimestamp: jsonb_out failed");
            string_t out = StringVector::AddString(result, str);
            free(str);
            return out;
        });
}

/* ------------------------------------------------------------------
 * Registration
 * ------------------------------------------------------------------ */

void TJsonbTypes::RegisterScalarFunctions(ExtensionLoader &loader) {
    const auto T = TJsonbTypes::tjsonb();
    const auto V = LogicalType::VARCHAR;
    const auto TS = LogicalType::TIMESTAMP_TZ;

    /* Constructors */
    RegisterSerializedScalarFunction(loader,
        ScalarFunction("tjsonb", {V}, T, Tjsonb_constructor));
    RegisterSerializedScalarFunction(loader,
        ScalarFunction("tjsonb", {V, TS}, T, Tjsonbinst_constructor));
    RegisterSerializedScalarFunction(loader,
        ScalarFunction("tjsonb", {V, SpanTypes::tstzspan()}, T,
                       Tjsonb_sequence_from_tstzspan));

    RegisterSerializedScalarFunction(loader,
        ScalarFunction("tjsonbSeq", {LogicalType::LIST(T)}, T,
                       Tjsonb_sequence_constructor));

    /* Generic temporal functions applied to tjsonb. timeSpan/tempSubtype/interp/merge
     * are now GENERATED with identical signatures (generate-then-retire), so the hand
     * registrations are retired here. memSize/setInterp stay hand (not generated yet);
     * tjsonbInst stays hand (subtype cast). */
    RegisterSerializedScalarFunction(loader,
        ScalarFunction("memSize", {T}, LogicalType::INTEGER, TemporalFunctions::Temporal_mem_size));
    RegisterSerializedScalarFunction(loader,
        ScalarFunction("setInterp", {T, V}, T, TemporalFunctions::Temporal_set_interp));
    RegisterSerializedScalarFunction(loader,
        ScalarFunction("tjsonbInst", {T}, T, TemporalFunctions::Temporal_to_tinstant));

    /* Value accessors — startValue/endValue are now GENERATED (they return the
     * jsonb base value, rendered as JSON text via the jsonb->VARCHAR cast),
     * mirroring the cbuffer sibling; retired here to avoid an ambiguous overload.
     * valueAtTimestamp stays hand (its out-param shape is not generated yet). */
    RegisterSerializedScalarFunction(loader,
        ScalarFunction("valueAtTimestamp", {T, TS}, V, Tjsonb_value_at_timestamp));
}

} // namespace duckdb
