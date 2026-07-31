#include <cmath>
#include <cstdint>
#define MOBILITYDUCK_EXTENSION_TYPES

#include "temporal/span.hpp"
#include "temporal/spanset.hpp"
#include "temporal/span_functions.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/common/vector_operations/binary_executor.hpp"
#include "duckdb/common/vector_operations/ternary_executor.hpp"
#include "duckdb/common/vector_operations/generic_executor.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "time_util.hpp"

#include <regex>
#include <string>

extern "C" {
    #include <meos.h>
    #include <meos_geo.h>
    #include <meos_internal.h>
    #include <assert.h>
}

namespace duckdb {

    // --- AsText ---
void SpanFunctions::Span_as_text(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input_vec = args.data[0];
    input_vec.Flatten(args.size());

    bool has_digits = args.ColumnCount() > 1;
    Vector *digits_vec_ptr = has_digits ? &args.data[1] : nullptr;
    if (has_digits) digits_vec_ptr->Flatten(args.size());

    for (idx_t i = 0; i < args.size(); i++) {
        if (FlatVector::IsNull(input_vec, i) || (has_digits && FlatVector::IsNull(*digits_vec_ptr, i))) {
            FlatVector::SetNull(result, i, true);
            continue;
        }

        auto blob = FlatVector::GetData<string_t>(input_vec)[i];
        int digits = has_digits ? FlatVector::GetData<int32_t>(*digits_vec_ptr)[i] : 15;

        const uint8_t *data = (const uint8_t *)blob.GetData();
        size_t size = blob.GetSize();

        Span *s = (Span *)malloc(size);
        memcpy(s, data, size);

        char *cstr = span_out(s, digits);
        auto str = StringVector::AddString(result, cstr);
        FlatVector::GetData<string_t>(result)[i] = str;

        free(s);
        free(cstr);
    }
}

// --- Cast From String ---
bool SpanFunctions::Span_to_text(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
   UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) -> string_t {
            // Convert binary string_t back to span using direct memory access
            const uint8_t *span_data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t span_size = input_blob.GetSize();
            
            // Cast directly to Span*
            const Span *s = reinterpret_cast<const Span*>(span_data);
            
            char *cstr = span_out(s, 15);
            std::string output(cstr);
            free(cstr);
            
            return StringVector::AddString(result, output);
        });

    return true;
}

bool SpanFunctions::Text_to_span(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    auto &result_type = result.GetType();
    std::string type_alias = result_type.GetAlias();
    
    // Map the alias to the correct MEOS type
    MeosType target_meos_type = SpanTypeMapping::GetMeosTypeFromAlias(type_alias);
    
    if (target_meos_type == T_UNKNOWN) {
        throw InvalidInputException("Unknown span type: " + type_alias);
    }

    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();
            
            // Use the correct MEOS type for parsing
            Span *span = span_in(input.c_str(), target_meos_type);
            
            if (span == NULL) {
                throw InvalidInputException("Invalid " + type_alias + " format: " + input);
            }

            // Use memcpy instead of WKB format
            size_t span_size = sizeof(*span);
            uint8_t *span_buffer = (uint8_t*) malloc(span_size);
            memcpy(span_buffer, span, span_size);

            // Create string_t from binary data and add to result vector
            string_t span_string_t(reinterpret_cast<const char*>(span_buffer), span_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
            
            free(span_buffer);
            free(span);
            
            return stored_data;
        });

    return true;
}

// --- WKB / HexWKB I/O (subtype-agnostic; format encodes the span type) ---
void SpanFunctions::Span_as_binary(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            uint8_t *copy = (uint8_t *)malloc(input.GetSize());
            memcpy(copy, input.GetData(), input.GetSize());
            Span *s = reinterpret_cast<Span *>(copy);
            size_t wkb_size = 0;
            uint8_t *wkb = span_as_wkb(s, WKB_EXTENDED, &wkb_size);
            free(copy);
            if (!wkb) throw InternalException("asBinary: span_as_wkb failed");
            string_t stored = StringVector::AddStringOrBlob(
                result, string_t(reinterpret_cast<const char *>(wkb), wkb_size));
            free(wkb);
            return stored;
        });
}

void SpanFunctions::Span_as_hexwkb(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            uint8_t *copy = (uint8_t *)malloc(input.GetSize());
            memcpy(copy, input.GetData(), input.GetSize());
            Span *s = reinterpret_cast<Span *>(copy);
            size_t hex_size = 0;
            char *hex = span_as_hexwkb(s, WKB_EXTENDED, &hex_size);
            (void)hex_size;
            free(copy);
            if (!hex) throw InternalException("asHexWKB: span_as_hexwkb failed");
            string_t stored = StringVector::AddString(result, hex);
            free(hex);
            return stored;
        });
}

void SpanFunctions::Span_from_binary(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            if (input.GetSize() == 0)
                throw InvalidInputException("spanFromBinary: empty WKB input");
            uint8_t *wkb = (uint8_t *)malloc(input.GetSize());
            memcpy(wkb, input.GetData(), input.GetSize());
            Span *s = span_from_wkb(wkb, input.GetSize());
            free(wkb);
            if (!s) throw InvalidInputException(
                "spanFromBinary: invalid MEOS-WKB");
            size_t sz = sizeof(Span);
            string_t stored = StringVector::AddStringOrBlob(
                result, string_t(reinterpret_cast<const char *>(s), sz));
            free(s);
            return stored;
        });
}

void SpanFunctions::Span_from_hexwkb(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string hex(input.GetData(), input.GetSize());
            Span *s = span_from_hexwkb(hex.c_str());
            if (!s) throw InvalidInputException(
                "spanFromHexWKB: invalid hex-encoded MEOS-WKB");
            size_t sz = sizeof(Span);
            string_t stored = StringVector::AddStringOrBlob(
                result, string_t(reinterpret_cast<const char *>(s), sz));
            free(s);
            return stored;
        });
}

// --- Span constructor from string ---
void SpanFunctions::Span_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input_vec = args.data[0];
    auto &result_type = result.GetType();
    std::string type_alias = result_type.GetAlias();
    
    MeosType target_meos_type = SpanTypeMapping::GetMeosTypeFromAlias(type_alias);
    
    if (target_meos_type == T_UNKNOWN) {
        throw InvalidInputException("Unknown span type: " + type_alias);
    }

    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, args.size(),
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();
            
            Span *span = span_in(input.c_str(), target_meos_type);
            
            if (span == NULL) {
                throw InvalidInputException("Invalid " + type_alias + " format: " + input);
            }


            size_t span_size = sizeof(*span);
            uint8_t *span_buffer = (uint8_t*) malloc(span_size);

            memcpy(span_buffer, span, span_size);

            string_t span_string_t((char *) span_buffer, span_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
            
            free(span_buffer);
            free(span);
            
            return stored_data;
        });
}



// --- Span binary constructor ---
static string_t Span_make_blob(Datum lower_dat, Datum upper_dat, bool lower_inc, bool upper_inc, MeosType span_type,
                               Vector &result) {
    MeosType basetype = spantype_basetype(span_type);
    Span *span = span_make(lower_dat, upper_dat, lower_inc, upper_inc, basetype);
    if (span == NULL) {
        throw InvalidInputException("Failed to create span from bounds");
    }
    size_t span_size = sizeof(Span);
    string_t out = StringVector::AddStringOrBlob(result, reinterpret_cast<const char *>(span), span_size);
    free(span);
    return out;
}

void SpanFunctions::Span_binary_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    D_ASSERT(args.ColumnCount() == 2 || args.ColumnCount() == 4);
    auto &args0 = args.data[0];
    auto &args1 = args.data[1];
    Vector *args2 = args.ColumnCount() == 4 ? &args.data[2] : nullptr;
    Vector *args3 = args.ColumnCount() == 4 ? &args.data[3] : nullptr;

    auto out_type = result.GetType();
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(out_type.GetAlias());
    const idx_t count = args.size();

    switch (span_type) {
        case T_INTSPAN:
            if (args.ColumnCount() == 2) {
                BinaryExecutor::Execute<int32_t, int32_t, string_t>(
                    args0, args1, result, count, [&](int32_t lo, int32_t hi) {
                        return Span_make_blob(Datum(lo), Datum(hi), true, false, span_type, result);
                    });
            } else {
                GenericExecutor::ExecuteQuaternary<PrimitiveType<int32_t>, PrimitiveType<int32_t>, PrimitiveType<bool>,
                                                   PrimitiveType<bool>, PrimitiveType<string_t>>(
                    args0, args1, *args2, *args3, result, count,
                    [&](PrimitiveType<int32_t> lo, PrimitiveType<int32_t> hi, PrimitiveType<bool> li,
                        PrimitiveType<bool> ui) {
                        return Span_make_blob(Datum(lo.val), Datum(hi.val), li.val, ui.val, span_type, result);
                    });
            }
            break;
        case T_BIGINTSPAN:
            if (args.ColumnCount() == 2) {
                BinaryExecutor::Execute<int64_t, int64_t, string_t>(
                    args0, args1, result, count, [&](int64_t lo, int64_t hi) {
                        return Span_make_blob(Datum(lo), Datum(hi), true, false, span_type, result);
                    });
            } else {
                GenericExecutor::ExecuteQuaternary<PrimitiveType<int64_t>, PrimitiveType<int64_t>, PrimitiveType<bool>,
                                                   PrimitiveType<bool>, PrimitiveType<string_t>>(
                    args0, args1, *args2, *args3, result, count,
                    [&](PrimitiveType<int64_t> lo, PrimitiveType<int64_t> hi, PrimitiveType<bool> li,
                        PrimitiveType<bool> ui) {
                        return Span_make_blob(Datum(lo.val), Datum(hi.val), li.val, ui.val, span_type, result);
                    });
            }
            break;
        case T_FLOATSPAN:
            if (args.ColumnCount() == 2) {
                BinaryExecutor::Execute<double, double, string_t>(
                    args0, args1, result, count, [&](double lo, double hi) {
                        return Span_make_blob(Float8GetDatum(lo), Float8GetDatum(hi), true, false, span_type, result);
                    });
            } else {
                GenericExecutor::ExecuteQuaternary<PrimitiveType<double>, PrimitiveType<double>, PrimitiveType<bool>,
                                                   PrimitiveType<bool>, PrimitiveType<string_t>>(
                    args0, args1, *args2, *args3, result, count,
                    [&](PrimitiveType<double> lo, PrimitiveType<double> hi, PrimitiveType<bool> li,
                        PrimitiveType<bool> ui) {
                        return Span_make_blob(Float8GetDatum(lo.val), Float8GetDatum(hi.val), li.val, ui.val, span_type,
                                              result);
                    });
            }
            break;
        case T_DATESPAN:
            if (args.ColumnCount() == 2) {
                BinaryExecutor::Execute<date_t, date_t, string_t>(
                    args0, args1, result, count, [&](date_t lo, date_t hi) {
                        return Span_make_blob(Datum(ToMeosDate(lo)), Datum(ToMeosDate(hi)), true, false, span_type,
                                              result);
                    });
            } else {
                GenericExecutor::ExecuteQuaternary<PrimitiveType<date_t>, PrimitiveType<date_t>, PrimitiveType<bool>,
                                                   PrimitiveType<bool>, PrimitiveType<string_t>>(
                    args0, args1, *args2, *args3, result, count,
                    [&](PrimitiveType<date_t> lo, PrimitiveType<date_t> hi, PrimitiveType<bool> li,
                        PrimitiveType<bool> ui) {
                        return Span_make_blob(Datum(ToMeosDate(lo.val)), Datum(ToMeosDate(hi.val)), li.val, ui.val,
                                              span_type, result);
                    });
            }
            break;
        case T_TSTZSPAN:
            if (args.ColumnCount() == 2) {
                BinaryExecutor::Execute<timestamp_tz_t, timestamp_tz_t, string_t>(
                    args0, args1, result, count, [&](timestamp_tz_t lo_duck, timestamp_tz_t hi_duck) {
                        timestamp_tz_t lo_meos = DuckDBToMeosTimestamp(lo_duck);
                        timestamp_tz_t hi_meos = DuckDBToMeosTimestamp(hi_duck);
                        Datum lo_dat = (Datum) static_cast<TimestampTz>(lo_meos.value);
                        Datum hi_dat = (Datum) static_cast<TimestampTz>(hi_meos.value);
                        return Span_make_blob(lo_dat, hi_dat, true, false, span_type, result);
                    });
            } else {
                GenericExecutor::ExecuteQuaternary<PrimitiveType<timestamp_tz_t>, PrimitiveType<timestamp_tz_t>,
                                                   PrimitiveType<bool>, PrimitiveType<bool>, PrimitiveType<string_t>>(
                    args0, args1, *args2, *args3, result, count,
                    [&](PrimitiveType<timestamp_tz_t> lo, PrimitiveType<timestamp_tz_t> hi, PrimitiveType<bool> li,
                        PrimitiveType<bool> ui) {
                        timestamp_tz_t lo_meos = DuckDBToMeosTimestamp(lo.val);
                        timestamp_tz_t hi_meos = DuckDBToMeosTimestamp(hi.val);
                        Datum lo_dat = (Datum) static_cast<TimestampTz>(lo_meos.value);
                        Datum hi_dat = (Datum) static_cast<TimestampTz>(hi_meos.value);
                        return Span_make_blob(lo_dat, hi_dat, li.val, ui.val, span_type, result);
                    });
            }
            break;
        default:
            throw NotImplementedException("span(<type>, <type>) not yet implemented for type: " + out_type.GetAlias());
    }
}



static inline void Write_span(Vector &result, idx_t row, Span *s) {
    size_t span_size = sizeof(*s);
    auto out = FlatVector::GetData<string_t>(result);
    out[row] = StringVector::AddStringOrBlob(result, (const char *)s, span_size);
    free(s);
}

static void Value_to_span_core(Vector &source, Vector &result, idx_t count, MeosType base_type){
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);

    auto handle_null = [&](idx_t row) {
        FlatVector::SetNull(result, row, true);
    };
            
    switch(base_type){
        case T_INT4: {
            auto in = FlatVector::GetData<int32> (source);
            for (idx_t i = 0; i < count; i++){
                if (FlatVector::IsNull(source, i)) {
                    handle_null(i); continue;
                }
                Datum d = Datum(in[i]);
                Span *s = value_span(d, T_INT4);
                Write_span(result,i,s);
            }
            break;
        }
        case T_INT8: {
            auto in = FlatVector::GetData<int64_t>(source);
            for (idx_t i = 0; i < count; ++i) {
                if (FlatVector::IsNull(source, i)) { handle_null(i); continue; }
                Datum d = Datum(in[i]);               
                Span *s = value_span(d, T_INT8);
                Write_span(result, i, s);
            }
            break;
        }
        case T_FLOAT8: {
            auto in = FlatVector::GetData<double>(source);
            for (idx_t i = 0; i < count; ++i) {
                if (FlatVector::IsNull(source, i)) { handle_null(i); continue; }
                Datum d = Float8GetDatum(in[i]);
                Span *s = value_span(d, T_FLOAT8);
                Write_span(result, i, s);
            }
            break;
        }
        case T_DATE: {
            auto in = FlatVector::GetData<date_t>(source);
            for (idx_t i = 0; i < count; ++i) {
                if (FlatVector::IsNull(source, i)) { handle_null(i); continue; }
                int32_t days = (int32_t)ToMeosDate(in[i]);
                Datum d = Datum(days);
                Span *s = value_span(d, T_DATE);
                Write_span(result, i, s);
            }
            break;
        }
        case T_TIMESTAMPTZ: {
            auto in = FlatVector::GetData<timestamp_tz_t>(source);
            for (idx_t i = 0; i < count; ++i) {
                if (FlatVector::IsNull(source, i)) { handle_null(i); continue; }
                auto meos_ts = ToMeosTimestamp(in[i]);        
                Datum d = Datum(meos_ts);
                Span *s = value_span(d, T_TIMESTAMPTZ);
                Write_span(result, i, s);
            }
            break;
        }
        default:
            throw NotImplementedException("value_to_span not implemented for type: " + source.GetType().GetAlias());
    }
}

void SpanFunctions::Value_to_span(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &source = args.data[0];
    auto out_type = result.GetType();
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(out_type.GetAlias());
    MeosType base_type = spantype_basetype(span_type);

    Value_to_span_core(source, result, args.size(), base_type);

}

bool SpanFunctions::Value_to_span_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(result.GetType().GetAlias());
    MeosType base_type = spantype_basetype(span_type);
    Value_to_span_core(source, result, count, base_type);
    return true;
}

static void Set_to_span_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) -> string_t {
            const uint8_t *set_data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t set_size = input_blob.GetSize();
            
            const Set *s = reinterpret_cast<const Set*>(set_data);
            Span *span = set_to_span(s);
            
            if (span == NULL) {
                throw InvalidInputException("Failed to convert set to span");
            }
            
            size_t span_size = sizeof(*span);
            string_t out = StringVector::AddStringOrBlob(result, (const char *)span, span_size);
            free(span);
            return out;
        }
    );
}
// --- SCALAR: set -> span ---

void SpanFunctions::Set_to_span(DataChunk &args, ExpressionState &state, Vector &result) {
    Set_to_span_common(args.data[0], result, args.size());
}

// --- CAST: set -> span ---

bool SpanFunctions::Set_to_span_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Set_to_span_common(source, result, count);
    return true;
}

// spans(<set_type>) is generated from the catalog (set_spans) in generated_temporal_udfs.cpp.

// --- Conversion: intspan <-> floatspan ---
static void Intspan_to_floatspan_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) -> string_t {
            const uint8_t *span_data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t span_size = input_blob.GetSize();
            
            const Span *src_span = reinterpret_cast<const Span*>(span_data);
            Span *dst_span = intspan_to_floatspan(src_span);
            
            if (dst_span == NULL) {
                throw InvalidInputException("Failed to convert intspan to floatspan");
            }
            
            string_t out = StringVector::AddStringOrBlob(result, (const char *)dst_span, span_size);
            free(dst_span);
            return out;
        }
    );
}

static void Floatspan_to_intspan_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const uint8_t *span_data = reinterpret_cast<const uint8_t*>(blob.GetData());
            size_t span_size = blob.GetSize();
            
            const Span *src_span = reinterpret_cast<const Span*>(span_data);
            Span *dst_span = floatspan_to_intspan(src_span);
            
            if (dst_span == NULL) {
                throw InvalidInputException("Failed to convert floatspan to intspan");
            }
            
            string_t out = StringVector::AddStringOrBlob(result, (const char *)dst_span, span_size);
            free(dst_span);
            return out;
        }
    );
}

void SpanFunctions::Intspan_to_floatspan(DataChunk &args, ExpressionState &state, Vector &result) {
    Intspan_to_floatspan_common(args.data[0], result, args.size());
}

void SpanFunctions::Floatspan_to_intspan(DataChunk &args, ExpressionState &state, Vector &result) {
    Floatspan_to_intspan_common(args.data[0], result, args.size());
}

bool SpanFunctions::Intspan_to_floatspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Intspan_to_floatspan_common(source, result, count);
    return true;    
}

bool SpanFunctions::Floatspan_to_intspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Floatspan_to_intspan_common(source, result, count);
    return true;
}

// --- Conversion: tstzspan <-> datespan ---

// datespan -> tstzspan
static void Datespan_to_tstzspan_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const uint8_t *span_data = reinterpret_cast<const uint8_t*>(blob.GetData());
            size_t span_size = blob.GetSize();
            
            const Span *src_span = reinterpret_cast<const Span*>(span_data);
            Span *dst = datespan_to_tstzspan(src_span);
            
            if (dst == NULL) {
                throw InvalidInputException("Failed to convert datespan to tstzspan");
            }
            
            string_t out = StringVector::AddStringOrBlob(result, (const char *)dst, span_size);
            free(dst);
            return out;
        }
    );
}

// tstzspan -> datespan
static void Tstzspan_to_datespan_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const uint8_t *span_data = reinterpret_cast<const uint8_t*>(blob.GetData());
            size_t span_size = blob.GetSize();
            
            const Span *src_span = reinterpret_cast<const Span*>(span_data);
            Span *dst = tstzspan_to_datespan(src_span);
            
            if (dst == NULL) {
                throw InvalidInputException("Failed to convert tstzspan to datespan");
            }
            
            string_t out = StringVector::AddStringOrBlob(result, (const char *)dst, span_size);
            free(dst);
            return out;
        }
    );
}

// --- SCALAR: datespan -> tstzspan ---
void SpanFunctions::Datespan_to_tstzspan(DataChunk &args, ExpressionState &state, Vector &result) {
    Datespan_to_tstzspan_common(args.data[0], result, args.size());
}

// --- SCALAR: tstzspan -> datespan ---
void SpanFunctions::Tstzspan_to_datespan(DataChunk &args, ExpressionState &state, Vector &result) {
    Tstzspan_to_datespan_common(args.data[0], result, args.size());
}

// --- CAST: datespan -> tstzspan ---
bool SpanFunctions::Datespan_to_tstzspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Datespan_to_tstzspan_common(source, result, count);
    return true;
}

// --- CAST: tstzspan -> datespan ---
bool SpanFunctions::Tstzspan_to_datespan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Tstzspan_to_datespan_common(source, result, count);
    return true;
}

void SpanFunctions::Set_split_n_spans(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &set_vec = args.data[0];
    auto &n_vec = args.data[1];
    idx_t row_count = args.size();
    set_vec.Flatten(row_count);
    n_vec.Flatten(row_count);

    auto &result_validity = FlatVector::Validity(result);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &child_vector = ListVector::GetEntry(result);
    child_vector.SetVectorType(VectorType::FLAT_VECTOR);
    ListVector::Reserve(result, row_count);

    idx_t total_offset = 0;
    const size_t span_bytes = sizeof(Span);

    for (idx_t i = 0; i < row_count; ++i) {
        if (set_vec.GetValue(i).IsNull() || n_vec.GetValue(i).IsNull()) {
            result_validity.SetInvalid(i);
            continue;
        }

        string_t set_blob = FlatVector::GetData<string_t>(set_vec)[i];
        int32_t n = FlatVector::GetData<int32_t>(n_vec)[i];

        Set *s = (Set *)malloc(set_blob.GetSize());
        memcpy(s, set_blob.GetData(), set_blob.GetSize());

        int out_count = 0;
        Span *spans = set_split_n_spans(s, n, &out_count);
        free(s);

        if (!spans || out_count <= 0) {
            result_validity.SetInvalid(i);
            continue;
        }

        ListVector::SetListSize(result, total_offset + out_count);
        list_entries[i] = list_entry_t{total_offset, static_cast<uint64_t>(out_count)};

        auto *child_data = FlatVector::GetData<string_t>(child_vector);
        for (int j = 0; j < out_count; ++j) {
            child_data[total_offset + j] =
                StringVector::AddStringOrBlob(child_vector, reinterpret_cast<const char *>(&spans[j]), span_bytes);
        }

        free(spans);
        total_offset += out_count;
        result_validity.SetValid(i);
    }
}

void SpanFunctions::Set_split_each_n_spans(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &set_vec = args.data[0];
    auto &n_vec = args.data[1];
    idx_t row_count = args.size();
    set_vec.Flatten(row_count);
    n_vec.Flatten(row_count);

    auto &result_validity = FlatVector::Validity(result);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &child_vector = ListVector::GetEntry(result);
    child_vector.SetVectorType(VectorType::FLAT_VECTOR);
    ListVector::Reserve(result, row_count);

    idx_t total_offset = 0;
    const size_t span_bytes = sizeof(Span);

    for (idx_t i = 0; i < row_count; ++i) {
        if (set_vec.GetValue(i).IsNull() || n_vec.GetValue(i).IsNull()) {
            result_validity.SetInvalid(i);
            continue;
        }

        string_t set_blob = FlatVector::GetData<string_t>(set_vec)[i];
        int32_t n = FlatVector::GetData<int32_t>(n_vec)[i];

        Set *s = (Set *)malloc(set_blob.GetSize());
        memcpy(s, set_blob.GetData(), set_blob.GetSize());

        int out_count = 0;
        Span *spans = set_split_each_n_spans(s, n, &out_count);
        free(s);

        if (!spans || out_count <= 0) {
            result_validity.SetInvalid(i);
            continue;
        }

        ListVector::SetListSize(result, total_offset + out_count);
        list_entries[i] = list_entry_t{total_offset, static_cast<uint64_t>(out_count)};

        auto *child_data = FlatVector::GetData<string_t>(child_vector);
        for (int j = 0; j < out_count; ++j) {
            child_data[total_offset + j] =
                StringVector::AddStringOrBlob(child_vector, reinterpret_cast<const char *>(&spans[j]), span_bytes);
        }

        free(spans);
        total_offset += out_count;
        result_validity.SetValid(i);
    }
}



void SpanFunctions::Span_lower(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    auto span_type = SpanTypeMapping::GetMeosTypeFromAlias(input.GetType().ToString());
    auto base_type = spantype_basetype(span_type);

    switch (base_type) {
    case T_INT4:
        UnaryExecutor::Execute<string_t, int32_t>(
            input, result, args.size(),
            [&](string_t blob) -> int32_t {
                Span *s = (Span *)malloc(blob.GetSize());
                memcpy(s, blob.GetData(), blob.GetSize());
                int v = intspan_lower(s);
                free(s);
                return v;
            });
        break;
    case T_INT8:
        UnaryExecutor::Execute<string_t, int64_t>(
            input, result, args.size(),
            [&](string_t blob) -> int64_t {
                Span *s = (Span *)malloc(blob.GetSize());
                memcpy(s, blob.GetData(), blob.GetSize());
                int64_t v = bigintspan_lower(s);
                free(s);
                return v;
            });
        break;
    case T_FLOAT8:
        UnaryExecutor::Execute<string_t, double>(
            input, result, args.size(),
            [&](string_t blob) -> double {
                Span *s = (Span *)malloc(blob.GetSize());
                memcpy(s, blob.GetData(), blob.GetSize());
                double v = floatspan_lower(s);
                free(s);
                return v;
            });
        break;
    case T_DATE:
        UnaryExecutor::Execute<string_t, date_t>(
            input, result, args.size(),
            [&](string_t blob) -> date_t {
                Span *s = (Span *)malloc(blob.GetSize());
                memcpy(s, blob.GetData(), blob.GetSize());
                DateADT d = datespan_lower(s);
                free(s);
                return FromMeosDate(static_cast<int32_t>(d));
            });
        break;
    case T_TIMESTAMPTZ:
        UnaryExecutor::Execute<string_t, timestamp_tz_t>(
            input, result, args.size(),
            [&](string_t blob) -> timestamp_tz_t {
                Span *s = (Span *)malloc(blob.GetSize());
                memcpy(s, blob.GetData(), blob.GetSize());
                TimestampTz t = tstzspan_lower(s);
                free(s);
                timestamp_tz_t meos_ts;
                meos_ts.value = static_cast<int64_t>(t);
                return MeosToDuckDBTimestamp(meos_ts);
            });
        break;
    default:
        throw NotImplementedException("lower(span): unsupported span type");
    }
}

void SpanFunctions::Span_upper(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    auto span_type = SpanTypeMapping::GetMeosTypeFromAlias(input.GetType().ToString());
    auto base_type = spantype_basetype(span_type);

    switch (base_type) {
    case T_INT4:
        UnaryExecutor::Execute<string_t, int32_t>(
            input, result, args.size(),
            [&](string_t blob) -> int32_t {
                Span *s = (Span *)malloc(blob.GetSize());
                memcpy(s, blob.GetData(), blob.GetSize());
                int v = intspan_upper(s);
                free(s);
                return v;
            });
        break;
    case T_INT8:
        UnaryExecutor::Execute<string_t, int64_t>(
            input, result, args.size(),
            [&](string_t blob) -> int64_t {
                Span *s = (Span *)malloc(blob.GetSize());
                memcpy(s, blob.GetData(), blob.GetSize());
                int64_t v = bigintspan_upper(s);
                free(s);
                return v;
            });
        break;
    case T_FLOAT8:
        UnaryExecutor::Execute<string_t, double>(
            input, result, args.size(),
            [&](string_t blob) -> double {
                Span *s = (Span *)malloc(blob.GetSize());
                memcpy(s, blob.GetData(), blob.GetSize());
                double v = floatspan_upper(s);
                free(s);
                return v;
            });
        break;
    case T_DATE:
        UnaryExecutor::Execute<string_t, date_t>(
            input, result, args.size(),
            [&](string_t blob) -> date_t {
                Span *s = (Span *)malloc(blob.GetSize());
                memcpy(s, blob.GetData(), blob.GetSize());
                DateADT d = datespan_upper(s);
                free(s);
                return FromMeosDate(static_cast<int32_t>(d));
            });
        break;
    case T_TIMESTAMPTZ:
        UnaryExecutor::Execute<string_t, timestamp_tz_t>(
            input, result, args.size(),
            [&](string_t blob) -> timestamp_tz_t {
                Span *s = (Span *)malloc(blob.GetSize());
                memcpy(s, blob.GetData(), blob.GetSize());
                TimestampTz t = tstzspan_upper(s);
                free(s);
                timestamp_tz_t meos_ts;
                meos_ts.value = static_cast<int64_t>(t);
                return MeosToDuckDBTimestamp(meos_ts);
            });
        break;
    default:
        throw NotImplementedException("upper(span): unsupported span type");
    }
}

void SpanFunctions::Span_lower_inc(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    auto span_type = SpanTypeMapping::GetMeosTypeFromAlias(input.GetType().ToString());
    auto base_type = spantype_basetype(span_type);

    UnaryExecutor::Execute<string_t, bool>(
        input, result, args.size(),
        [&](string_t blob) -> bool {
            Span *s = (Span *)malloc(blob.GetSize());
            memcpy(s, blob.GetData(), blob.GetSize());
            bool inc = span_lower_inc(s);
            free(s);
            return inc;
        });
}

void SpanFunctions::Span_upper_inc(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    auto span_type = SpanTypeMapping::GetMeosTypeFromAlias(input.GetType().ToString());
    auto base_type = spantype_basetype(span_type);

    UnaryExecutor::Execute<string_t, bool>(
        input, result, args.size(),
        [&](string_t blob) -> bool {
            Span *s = (Span *)malloc(blob.GetSize());
            memcpy(s, blob.GetData(), blob.GetSize());
            bool inc = span_upper_inc(s);
            free(s);
            return inc;
        });
}   

void SpanFunctions::Numspan_width(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    auto span_type = SpanTypeMapping::GetMeosTypeFromAlias(input.GetType().ToString());
    auto base_type = spantype_basetype(span_type);

    switch (base_type) {
    case T_INT4:
        UnaryExecutor::Execute<string_t, int64_t>(
            input, result, args.size(),
            [&](string_t blob) -> int64_t {
                Span *s = (Span *)malloc(blob.GetSize());
                memcpy(s, blob.GetData(), blob.GetSize());
                int64_t width = intspan_width(s);
                free(s);
                return width;
            });
        break;
    case T_INT8:
        UnaryExecutor::Execute<string_t, int64_t>(
            input, result, args.size(),
            [&](string_t blob) -> int64_t {
                Span *s = (Span *)malloc(blob.GetSize());
                memcpy(s, blob.GetData(), blob.GetSize());
                int64_t width = bigintspan_width(s);
                free(s);
                return width;
            });
        break;
    case T_FLOAT8:
        UnaryExecutor::Execute<string_t, double>(
            input, result, args.size(),
            [&](string_t blob) -> double {
                Span *s = (Span *)malloc(blob.GetSize());
                memcpy(s, blob.GetData(), blob.GetSize());
                double width = floatspan_width(s);
                free(s);
                return width;
            });
        break;
    default:
        throw NotImplementedException("width(span): unsupported span type");
    }
}

void SpanFunctions::Datespan_duration(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    UnaryExecutor::Execute<string_t, interval_t>(
        input, result, args.size(),
        [&](string_t blob) -> interval_t {
            Span *s = (Span *)malloc(blob.GetSize());
            memcpy(s, blob.GetData(), blob.GetSize());
            MeosInterval *iv = datespan_duration(s);
            free(s);
            if (!iv) {
                throw InvalidInputException("datespan_duration: invalid input or not a datespan");
            }
            interval_t out = IntervalToIntervalt(iv);
            free(iv);
            return out;
        });
}

void SpanFunctions::Tstzspan_duration(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    UnaryExecutor::Execute<string_t, interval_t>(
        input, result, args.size(),
        [&](string_t blob) -> interval_t {
            Span *s = (Span *)malloc(blob.GetSize());
            memcpy(s, blob.GetData(), blob.GetSize());
            MeosInterval *iv = tstzspan_duration(s);
            free(s);
            if (!iv) {
                throw InvalidInputException("tstzspan_duration: invalid input or not a tstzspan");
            }
            interval_t out = IntervalToIntervalt(iv);
            free(iv);
            return out;
        });
}

static inline string_t Numspan_expand_common(const string_t &blob, Datum value, MeosType validate_span_type, Vector &result) {
    const uint8_t *data = (const uint8_t *)blob.GetData();
    size_t size = blob.GetSize();
    
    Span *s = (Span *)malloc(size);
    memcpy(s, data, size);
    
    switch(validate_span_type) {
        case T_INTSPAN: VALIDATE_INTSPAN(s, NULL); break;
        case T_FLOATSPAN: VALIDATE_FLOATSPAN(s, NULL); break;
        case T_BIGINTSPAN: VALIDATE_BIGINTSPAN(s, NULL); break;
        case T_DATESPAN: VALIDATE_DATESPAN(s, NULL); break;
        default: break;
    }    
    
    Span *r = numspan_expand(s, value);
    
    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, size);
    
    free(s);
    free(r);
    return out;
}

static inline string_t Tstzspan_expand_common(const string_t &blob, interval_t duckdb_interval,
                                              Vector &result, ValidityMask &mask, idx_t idx) {
    const uint8_t *data = (const uint8_t *)blob.GetData();
    size_t size = blob.GetSize();

    Span *s = (Span *)malloc(size);
    memcpy(s, data, size);

    VALIDATE_TSTZSPAN(s, string_t());

    // Convert DuckDB interval_t to MEOS Interval
    MeosInterval meos_interval = IntervaltToInterval(duckdb_interval);

    Span *r = tstzspan_expand(s, &meos_interval);
    free(s);
    // A shrinking (negative) interval can collapse the span to empty; MEOS
    // returns NULL, which maps to SQL NULL.
    if (!r) { mask.SetInvalid(idx); return string_t(); }
    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, size);
    free(r);
    return out;
}
void SpanFunctions::Numspan_expand(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[0];
    auto out_type  = result.GetType();    
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(out_type.GetAlias());    
    
    switch (span_type) {
        case T_INTSPAN: { // expand(intspan, integer) -> intspan
            BinaryExecutor::Execute<string_t, int32_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t blob, int32_t value) -> string_t {
                    return Numspan_expand_common(blob, Datum(value), span_type, result);
                });
            break;
        }
        case T_BIGINTSPAN: { // expand(bigintspan, bigint) -> bigintspan
            BinaryExecutor::Execute<string_t, int64_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t blob, int64_t value) -> string_t {
                    return Numspan_expand_common(blob, Datum(value), span_type, result);
                });
            break;
        }
        case T_FLOATSPAN: { // expand(floatspan, double) -> floatspan
            BinaryExecutor::Execute<string_t, double, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t blob, double value) -> string_t {
                    return Numspan_expand_common(blob, Float8GetDatum(value), span_type, result);
                });
            break;
        }
        case T_DATESPAN: { // expand(datespan, integer) -> datespan
            BinaryExecutor::Execute<string_t, int32_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t blob, int32_t value) -> string_t {
                    return Numspan_expand_common(blob, Datum(value), span_type, result);
                });
            break;
        }
        default:
            throw NotImplementedException("expand(<span>): unsupported span type");
    }
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void SpanFunctions::Tstzspan_expand(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[0];
    auto out_type  = result.GetType();    
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(out_type.GetAlias());    
    BinaryExecutor::ExecuteWithNulls<string_t, interval_t, string_t>(
        span_vec, args.data[1], result, args.size(),
        [&](string_t blob, interval_t value, ValidityMask &mask, idx_t idx) -> string_t {
            return Tstzspan_expand_common(blob, value, result, mask, idx);
        });
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

static inline string_t Numspan_shift_common(const string_t &blob, Datum shift_datum,
                                        MeosType validate_span_type, Vector &result) {
    const uint8_t *data = (const uint8_t *)blob.GetData();
    size_t size = blob.GetSize();
    
    Span *s = (Span *)malloc(size);
    memcpy(s, data, size);
    
    switch(validate_span_type) {
        case T_INTSPAN: VALIDATE_INTSPAN(s, NULL); break;
        case T_FLOATSPAN: VALIDATE_FLOATSPAN(s, NULL); break;
        case T_BIGINTSPAN: VALIDATE_BIGINTSPAN(s, NULL); break;
        case T_DATESPAN: VALIDATE_DATESPAN(s, NULL); break;
        case T_TSTZSPAN: VALIDATE_TSTZSPAN(s, NULL); break;
        default: break;
    }    
    
    Span *r = numspan_shift_scale(s, shift_datum, 0, /*do_shift=*/true, /*do_scale=*/false);
    
    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, size);
    
    free(s);
    free(r);
    return out;
}

static inline string_t Tstzspan_shift_common(const string_t &blob, interval_t duckdb_interval, Vector &result) {
    const uint8_t *data = (const uint8_t *)blob.GetData();
    size_t size = blob.GetSize();
    
    Span *s = (Span *)malloc(size);
    memcpy(s, data, size);
    
    VALIDATE_TSTZSPAN(s, NULL);
    
    // Convert DuckDB interval_t to MEOS Interval
    MeosInterval meos_interval = IntervaltToInterval(duckdb_interval);
    
    Span *r = tstzspan_shift_scale(s, &meos_interval, NULL);
    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, size);
    
    free(s);
    free(r);
    return out;
}

void SpanFunctions::Numspan_shift(DataChunk &args, ExpressionState &state, Vector &result) {    
    auto &span_vec = args.data[0];
    auto out_type  = result.GetType();    
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(out_type.GetAlias());    
    
    switch (span_type) {
        case T_INTSPAN: { // shift(intspan, integer) -> intspan
            BinaryExecutor::Execute<string_t, int32_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t blob, int32_t shift) -> string_t {
                    return Numspan_shift_common(blob, Datum(shift), span_type, result);
                });
            break;
        }
        case T_BIGINTSPAN: { // shift(bigintspan, bigint) -> bigintspan
            BinaryExecutor::Execute<string_t, int64_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t blob, int64_t shift) -> string_t {
                    return Numspan_shift_common(blob, Datum(shift), span_type, result);
                });
            break;
        }
        case T_FLOATSPAN: { // shift(floatspan, double) -> floatspan
            BinaryExecutor::Execute<string_t, double, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t blob, double shift) -> string_t {                    
                    return Numspan_shift_common(blob, Float8GetDatum(shift), span_type, result);
                });
            break;
        }
        case T_DATESPAN: { // shift(datespan, integer) -> datespan
            BinaryExecutor::Execute<string_t, int32_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t blob, int32_t shift_days) -> string_t {
                    return Numspan_shift_common(blob, Datum(shift_days), span_type, result);
                });
            break;
        }
        default:
            throw NotImplementedException("shift(<span>): unsupported span type");
    }
}

void SpanFunctions::Tstzspan_shift(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[0];
    auto out_type  = result.GetType();    
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(out_type.GetAlias());    
    BinaryExecutor::Execute<string_t, interval_t, string_t>(
        span_vec, args.data[1], result, args.size(),
        [&](string_t blob, interval_t shift_interval) -> string_t {
            return Tstzspan_shift_common(blob, shift_interval, result);
        });
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

static inline string_t Numspan_scale_common(const string_t &blob, Datum scale_datum,
                                        MeosType validate_span_type, Vector &result) {
    const uint8_t *data = (const uint8_t *)blob.GetData();
    size_t size = blob.GetSize();
    
    Span *s = (Span *)malloc(size);
    memcpy(s, data, size);
    
    switch(validate_span_type) {
        case T_INTSPAN: VALIDATE_INTSPAN(s, NULL); break;
        case T_FLOATSPAN: VALIDATE_FLOATSPAN(s, NULL); break;
        case T_BIGINTSPAN: VALIDATE_BIGINTSPAN(s, NULL); break;
        case T_DATESPAN: VALIDATE_DATESPAN(s, NULL); break;
        default: break;
    }    
    
    Span *r = numspan_shift_scale(s, 0, scale_datum, /*do_shift=*/false, /*do_scale=*/true);
    
    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, size);
    
    free(s);
    free(r);
    return out;
}

void SpanFunctions::Numspan_scale(DataChunk &args, ExpressionState &state, Vector &result) {    
    auto &span_vec = args.data[0];
    auto out_type  = result.GetType();    
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(out_type.GetAlias());    
    
    switch (span_type) {
        case T_INTSPAN: { // scale(intspan, integer) -> intspan
            BinaryExecutor::Execute<string_t, int32_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t blob, int32_t scale) -> string_t {
                    return Numspan_scale_common(blob, Datum(scale), span_type, result);
                });
            break;
        }
        case T_BIGINTSPAN: { // scale(bigintspan, bigint) -> bigintspan
            BinaryExecutor::Execute<string_t, int64_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t blob, int64_t scale) -> string_t {
                    return Numspan_scale_common(blob, Datum(scale), span_type, result);
                });
            break;
        }
        case T_FLOATSPAN: { // scale(floatspan, double) -> floatspan
            BinaryExecutor::Execute<string_t, double, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t blob, double scale) -> string_t {                    
                    return Numspan_scale_common(blob, Float8GetDatum(scale), span_type, result);
                });
            break;
        }
        case T_DATESPAN: { // scale(datespan, integer) -> datespan
            BinaryExecutor::Execute<string_t, int32_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t blob, int32_t scale) -> string_t {
                    return Numspan_scale_common(blob, Datum(scale), span_type, result);
                });
            break;
        }
        default:
            throw NotImplementedException("scale(<span>): unsupported span type");
    }
}

static inline string_t Tstzspan_scale_common(const string_t &blob, interval_t duckdb_scale, Vector &result) {
    const uint8_t *data = (const uint8_t *)blob.GetData();
    size_t size = blob.GetSize();
    
    Span *s = (Span *)malloc(size);
    memcpy(s, data, size);
    
    VALIDATE_TSTZSPAN(s, NULL);
    
    // Convert DuckDB interval_t to MEOS Interval
    MeosInterval meos_interval = IntervaltToInterval(duckdb_scale);
    
    Span *r = tstzspan_shift_scale(s, NULL, &meos_interval);
    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, size);
    
    free(s);
    free(r);
    return out;
}

void SpanFunctions::Tstzspan_scale(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[0];
    auto out_type  = result.GetType();    
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(out_type.GetAlias());    
    BinaryExecutor::Execute<string_t, interval_t, string_t>(
        span_vec, args.data[1], result, args.size(),
        [&](string_t blob, interval_t scale_interval) -> string_t {
            return Tstzspan_scale_common(blob, scale_interval, result);
        });
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

static inline string_t Tstzspan_shift_scale_common(const string_t &blob, interval_t shift_iv, interval_t scale_iv,
                                                   Vector &result) {
    const uint8_t *data = (const uint8_t *)blob.GetData();
    size_t size = blob.GetSize();

    Span *s = (Span *)malloc(size);
    memcpy(s, data, size);

    VALIDATE_TSTZSPAN(s, NULL);

    MeosInterval meos_shift = IntervaltToInterval(shift_iv);
    MeosInterval meos_scale = IntervaltToInterval(scale_iv);

    Span *r = tstzspan_shift_scale(s, &meos_shift, &meos_scale);
    free(s);
    if (!r) {
        throw InvalidInputException("tstzspan_shift_scale failed");
    }
    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, size);
    free(r);
    return out;
}

static inline string_t Numspan_shift_scale_common(const string_t &blob, Datum shift_datum, Datum scale_datum,
                                                 MeosType validate_span_type, Vector &result) {
    const uint8_t *data = (const uint8_t *)blob.GetData();
    size_t size = blob.GetSize();

    Span *s = (Span *)malloc(size);
    memcpy(s, data, size);

    switch (validate_span_type) {
        case T_INTSPAN:
            VALIDATE_INTSPAN(s, NULL);
            break;
        case T_FLOATSPAN:
            VALIDATE_FLOATSPAN(s, NULL);
            break;
        case T_BIGINTSPAN:
            VALIDATE_BIGINTSPAN(s, NULL);
            break;
        case T_DATESPAN:
            VALIDATE_DATESPAN(s, NULL);
            break;
        default:
            break;
    }

    Span *r = numspan_shift_scale(s, shift_datum, scale_datum, /*do_shift=*/true, /*do_scale=*/true);
    free(s);
    if (!r) {
        throw InvalidInputException("numspan_shift_scale failed");
    }
    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, size);
    free(r);
    return out;
}

void SpanFunctions::Numspan_shift_scale(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[0];
    auto out_type = result.GetType();
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(out_type.GetAlias());

    switch (span_type) {
        case T_INTSPAN: {
            TernaryExecutor::Execute<string_t, int32_t, int32_t, string_t>(
                span_vec, args.data[1], args.data[2], result, args.size(),
                [&](string_t blob, int32_t shift, int32_t scale) -> string_t {
                    return Numspan_shift_scale_common(blob, Datum(shift), Datum(scale), span_type, result);
                });
            break;
        }
        case T_BIGINTSPAN: {
            TernaryExecutor::Execute<string_t, int64_t, int64_t, string_t>(
                span_vec, args.data[1], args.data[2], result, args.size(),
                [&](string_t blob, int64_t shift, int64_t scale) -> string_t {
                    return Numspan_shift_scale_common(blob, Datum(shift), Datum(scale), span_type, result);
                });
            break;
        }
        case T_FLOATSPAN: {
            TernaryExecutor::Execute<string_t, double, double, string_t>(
                span_vec, args.data[1], args.data[2], result, args.size(),
                [&](string_t blob, double shift, double scale) -> string_t {
                    return Numspan_shift_scale_common(blob, Float8GetDatum(shift), Float8GetDatum(scale), span_type,
                                                      result);
                });
            break;
        }
        case T_DATESPAN: {
            TernaryExecutor::Execute<string_t, int32_t, int32_t, string_t>(
                span_vec, args.data[1], args.data[2], result, args.size(),
                [&](string_t blob, int32_t shift_days, int32_t scale_days) -> string_t {
                    return Numspan_shift_scale_common(blob, Datum(shift_days), Datum(scale_days), span_type, result);
                });
            break;
        }
        default:
            throw NotImplementedException("shiftScale(<span>): unsupported span type for this overload");
    }
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void SpanFunctions::Tstzspan_shift_scale(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[0];
    TernaryExecutor::Execute<string_t, interval_t, interval_t, string_t>(
        span_vec, args.data[1], args.data[2], result, args.size(),
        [&](string_t blob, interval_t shift, interval_t scale) -> string_t {
            return Tstzspan_shift_scale_common(blob, shift, scale, result);
        });
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// floor/ceil/round/degrees/radians on floatspan are generated from the catalog
// (generated_temporal_udfs.cpp); only round(DOUBLE) below, the scalar base helper
// with no catalog signature, remains hand-written.

void SpanFunctions::Float_round(DataChunk &args, ExpressionState &state, Vector &result) {
    D_ASSERT(args.ColumnCount() == 1 || args.ColumnCount() == 2);
    auto &args0 = args.data[0];
    Vector *args1 = args.ColumnCount() == 2 ? &args.data[1] : 0;
    if (args.ColumnCount() == 2) {
        BinaryExecutor::Execute<double_t, int32_t, double_t>(
            args0, *args1, result, args.size(),
            [&](double_t float_value, int32_t precision) -> double_t {
                double factor = std::pow(10.0, (double)precision);
                return std::round(float_value * factor) / factor;
            });
    } else {
        UnaryExecutor::Execute<double_t, double_t>(
            args0, result, args.size(),
            [&](double_t float_value) -> double_t {
                return std::round(float_value);
            });
    }
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// --- OPERATOR: span = span ---
void SpanFunctions::Span_eq(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b) -> bool {
            const uint8_t *a_data = reinterpret_cast<const uint8_t*>(a.GetData());
            size_t a_data_size = a.GetSize();
            uint8_t *a_data_copy = (uint8_t*)malloc(a_data_size);
            memcpy(a_data_copy, a_data, a_data_size);
            Span *a_span = reinterpret_cast<Span*>(a_data_copy);
            const uint8_t *b_data = reinterpret_cast<const uint8_t*>(b.GetData());
            size_t b_data_size = b.GetSize();
            uint8_t *b_data_copy = (uint8_t*)malloc(b_data_size);
            memcpy(b_data_copy, b_data, b_data_size);
            Span *b_span = reinterpret_cast<Span*>(b_data_copy);
            if (!a_span || !b_span) {
                free(a_data_copy);
                free(b_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }
            bool ret = span_eq(a_span, b_span);
            free(a_data_copy);
            free(b_data_copy);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}
// --- OPERATOR: span <> span ---
void SpanFunctions::Span_ne(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b) -> bool {
            const uint8_t *a_data = reinterpret_cast<const uint8_t*>(a.GetData());
            size_t a_data_size = a.GetSize();
            uint8_t *a_data_copy = (uint8_t*)malloc(a_data_size);
            memcpy(a_data_copy, a_data, a_data_size);
            Span *a_span = reinterpret_cast<Span*>(a_data_copy);
            const uint8_t *b_data = reinterpret_cast<const uint8_t*>(b.GetData());
            size_t b_data_size = b.GetSize();
            uint8_t *b_data_copy = (uint8_t*)malloc(b_data_size);
            memcpy(b_data_copy, b_data, b_data_size);
            Span *b_span = reinterpret_cast<Span*>(b_data_copy);
            if (!a_span || !b_span) {
                free(a_data_copy);
                free(b_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }
            bool ret = span_ne(a_span, b_span);
            free(a_data_copy);
            free(b_data_copy);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}
// --- OPERATOR: span < span ---
void SpanFunctions::Span_lt(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b) -> bool {
            const uint8_t *a_data = reinterpret_cast<const uint8_t*>(a.GetData());
            size_t a_data_size = a.GetSize();
            uint8_t *a_data_copy = (uint8_t*)malloc(a_data_size);
            memcpy(a_data_copy, a_data, a_data_size);
            Span *a_span = reinterpret_cast<Span*>(a_data_copy);
            const uint8_t *b_data = reinterpret_cast<const uint8_t*>(b.GetData());
            size_t b_data_size = b.GetSize();
            uint8_t *b_data_copy = (uint8_t*)malloc(b_data_size);
            memcpy(b_data_copy, b_data, b_data_size);
            Span *b_span = reinterpret_cast<Span*>(b_data_copy);
            if (!a_span || !b_span) {
                free(a_data_copy);
                free(b_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }
            bool ret = span_lt(a_span, b_span);
            free(a_data_copy);
            free(b_data_copy);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}
// --- OPERATOR: span <= span ---
void SpanFunctions::Span_le(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b) -> bool {
            const uint8_t *a_data = reinterpret_cast<const uint8_t*>(a.GetData());
            size_t a_data_size = a.GetSize();
            uint8_t *a_data_copy = (uint8_t*)malloc(a_data_size);
            memcpy(a_data_copy, a_data, a_data_size);
            Span *a_span = reinterpret_cast<Span*>(a_data_copy);
            const uint8_t *b_data = reinterpret_cast<const uint8_t*>(b.GetData());
            size_t b_data_size = b.GetSize();
            uint8_t *b_data_copy = (uint8_t*)malloc(b_data_size);
            memcpy(b_data_copy, b_data, b_data_size);
            Span *b_span = reinterpret_cast<Span*>(b_data_copy);
            if (!a_span || !b_span) {
                free(a_data_copy);
                free(b_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }
            bool ret = span_le(a_span, b_span);
            free(a_data_copy);
            free(b_data_copy);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void SpanFunctions::Span_gt(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b) -> bool {
            const uint8_t *a_data = reinterpret_cast<const uint8_t*>(a.GetData());
            size_t a_data_size = a.GetSize();
            uint8_t *a_data_copy = (uint8_t*)malloc(a_data_size);
            memcpy(a_data_copy, a_data, a_data_size);
            Span *a_span = reinterpret_cast<Span*>(a_data_copy);
            const uint8_t *b_data = reinterpret_cast<const uint8_t*>(b.GetData());
            size_t b_data_size = b.GetSize();
            uint8_t *b_data_copy = (uint8_t*)malloc(b_data_size);
            memcpy(b_data_copy, b_data, b_data_size);
            Span *b_span = reinterpret_cast<Span*>(b_data_copy);
            if (!a_span || !b_span) {
                free(a_data_copy);
                free(b_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }
            bool ret = span_gt(a_span, b_span);
            free(a_data_copy);
            free(b_data_copy);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}
// --- OPERATOR: span >= span ---
void SpanFunctions::Span_ge(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b) -> bool {
            const uint8_t *a_data = reinterpret_cast<const uint8_t*>(a.GetData());
            size_t a_data_size = a.GetSize();
            uint8_t *a_data_copy = (uint8_t*)malloc(a_data_size);
            memcpy(a_data_copy, a_data, a_data_size);
            Span *a_span = reinterpret_cast<Span*>(a_data_copy);
            const uint8_t *b_data = reinterpret_cast<const uint8_t*>(b.GetData());
            size_t b_data_size = b.GetSize();
            uint8_t *b_data_copy = (uint8_t*)malloc(b_data_size);
            memcpy(b_data_copy, b_data, b_data_size);
            Span *b_span = reinterpret_cast<Span*>(b_data_copy);
            if (!a_span || !b_span) {
                free(a_data_copy);
                free(b_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }
            bool ret = span_ge(a_span, b_span);
            free(a_data_copy);
            free(b_data_copy);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void SpanFunctions::Span_cmp(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, int32_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b) -> int32_t {
            const uint8_t *a_data = reinterpret_cast<const uint8_t*>(a.GetData());
            size_t a_data_size = a.GetSize();
            uint8_t *a_data_copy = (uint8_t*)malloc(a_data_size);
            memcpy(a_data_copy, a_data, a_data_size);
            Span *a_span = reinterpret_cast<Span*>(a_data_copy);
            const uint8_t *b_data = reinterpret_cast<const uint8_t*>(b.GetData());
            size_t b_data_size = b.GetSize();
            uint8_t *b_data_copy = (uint8_t*)malloc(b_data_size);
            memcpy(b_data_copy, b_data, b_data_size);
            Span *b_span = reinterpret_cast<Span*>(b_data_copy);
            if (!a_span || !b_span) {
                free(a_data_copy);
                free(b_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }
            int32_t ret = span_cmp(a_span, b_span);
            free(a_data_copy);
            free(b_data_copy);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}
// --- OPERATOR: span @> value ---
void SpanFunctions::Contains_span_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[0];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span_vec.GetType().GetAlias());    
    
    switch (span_type){
        case T_INTSPAN: { // intspan @> integer
            BinaryExecutor::Execute<string_t, int32_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int32_t value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    bool ret = contains_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
            }
        );
            break;
        }
        case T_BIGINTSPAN: { // bigintspan @> bigint
            BinaryExecutor::Execute<string_t, int64_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int64_t value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);       
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    bool ret = contains_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
                }
            ); 
            break;
        }
        case T_FLOATSPAN: { // floatspan @> double
            BinaryExecutor::Execute<string_t, double, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, double value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid floatspan data: null pointer");
                    }
                    bool ret = contains_span_value(span, Float8GetDatum(value));
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_DATESPAN: { // datespan @> date
            BinaryExecutor::Execute<string_t, int32_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int32_t value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid datespan data: null pointer");
                    }
                    bool ret = contains_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_TSTZSPAN: { // tstzspan @> timestamptz
            BinaryExecutor::Execute<string_t, timestamp_tz_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, timestamp_tz_t ts_duckdb) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    bool ret = contains_span_value(span, Datum(ts_meos.value));
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        default:
            throw NotImplementedException("SPAN @> value: unsupported span type");
    }
}
// --- OPERATOR: span @> span ---

void SpanFunctions::Contains_span_span(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec1 = args.data[0];
    auto &span_vec2 = args.data[1];
    BinaryExecutor::Execute<string_t, string_t, bool>(
        span_vec1, span_vec2, result, args.size(),
        [&](string_t span1_blob, string_t span2_blob) -> bool {
            const uint8_t *span1_data = reinterpret_cast<const uint8_t*>(span1_blob.GetData());
            size_t span1_data_size = span1_blob.GetSize();
            uint8_t *span1_data_copy = (uint8_t*)malloc(span1_data_size);
            memcpy(span1_data_copy, span1_data, span1_data_size);
            Span *span1 = reinterpret_cast<Span*>(span1_data_copy);
            if (!span1) {
                free(span1_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");     
            }
            const uint8_t *span2_data = reinterpret_cast<const uint8_t*>(span2_blob.GetData());
            size_t span2_data_size = span2_blob.GetSize();
            uint8_t *span2_data_copy = (uint8_t*)malloc(span2_data_size);
            memcpy(span2_data_copy, span2_data, span2_data_size);
            Span *span2 = reinterpret_cast<Span*>(span2_data_copy);
            if (!span2) {
                free(span2_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer"); 
            }
            bool ret = contains_span_span(span1, span2);
            free(span1_data_copy);
            free(span2_data_copy);
            return ret;
    }
);
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


// --- OPERATOR: value <@ span ---
void SpanFunctions::Contained_value_span(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[1];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span_vec.GetType().GetAlias());    
    
    switch (span_type){
        case T_INTSPAN: { // integer <@ intspan
            BinaryExecutor::Execute<int32_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](int32_t value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = contains_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
            }
        );
            break;
        }
        case T_BIGINTSPAN: { // bigint <@ bigintspan
            BinaryExecutor::Execute<int64_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](int64_t value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);       
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = contains_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
                }
            ); 
            break;
        }
        case T_FLOATSPAN: { // double <@ floatspan
            BinaryExecutor::Execute<double, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](double value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);      
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid floatspan data: null pointer");
                    }
                    bool ret = contains_span_value(span, Float8GetDatum(value));
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_DATESPAN: { // date <@ datespan
            BinaryExecutor::Execute<int32_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](int32_t value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid datespan data: null pointer");
                    }
                    bool ret = contains_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_TSTZSPAN: { // timestamptz <@ tstzspan
            BinaryExecutor::Execute<timestamp_tz_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](timestamp_tz_t ts_duckdb, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    bool ret = contains_span_value(span, Datum(ts_meos.value));
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        default:
            throw NotImplementedException("value <@ SPAN: unsupported span type");
    }
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// --- OPERATOR: span <@ span ---

void SpanFunctions::Contained_span_span(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec1 = args.data[0];
    auto &span_vec2 = args.data[1];
    BinaryExecutor::Execute<string_t, string_t, bool>(
        span_vec1, span_vec2, result, args.size(),
        [&](string_t span1_blob, string_t span2_blob) -> bool {
            const uint8_t *span1_data = reinterpret_cast<const uint8_t*>(span1_blob.GetData());
            size_t span1_data_size = span1_blob.GetSize();
            uint8_t *span1_data_copy = (uint8_t*)malloc(span1_data_size);
            memcpy(span1_data_copy, span1_data, span1_data_size);
            Span *span1 = reinterpret_cast<Span*>(span1_data_copy);
            if (!span1) {
                free(span1_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");     
            }
            const uint8_t *span2_data = reinterpret_cast<const uint8_t*>(span2_blob.GetData());
            size_t span2_data_size = span2_blob.GetSize();
            uint8_t *span2_data_copy = (uint8_t*)malloc(span2_data_size);
            memcpy(span2_data_copy, span2_data, span2_data_size);
            Span *span2 = reinterpret_cast<Span*>(span2_data_copy);
            if (!span2) {
                free(span2_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer"); 
            }
            bool ret = contains_span_span(span2, span1); // note the order of arguments is reversed
            free(span1_data_copy);
            free(span2_data_copy);
            return ret;
    }
);
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// --- OPERATOR: span && span ---
void SpanFunctions::Overlaps_span_span(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t span1_blob, string_t span2_blob) -> bool {
            const uint8_t *span1_data = reinterpret_cast<const uint8_t*>(span1_blob.GetData());
            size_t span1_data_size = span1_blob.GetSize();
            uint8_t *span1_data_copy = (uint8_t*)malloc(span1_data_size);
            memcpy(span1_data_copy, span1_data, span1_data_size);
            Span *span1 = reinterpret_cast<Span*>(span1_data_copy);
            if (!span1) {
                free(span1_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }

            const uint8_t *span2_data = reinterpret_cast<const uint8_t*>(span2_blob.GetData());
            size_t span2_data_size = span2_blob.GetSize();
            uint8_t *span2_data_copy = (uint8_t*)malloc(span2_data_size);
            memcpy(span2_data_copy, span2_data, span2_data_size);
            Span *span2 = reinterpret_cast<Span*>(span2_data_copy);
            if (!span2) {
                free(span2_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }

            bool ret = overlaps_span_span(span1, span2);
            free(span1);
            free(span2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// --- OPERATOR: value -|- span---
void SpanFunctions::Adjacent_value_span(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[1];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span_vec.GetType().GetAlias());    
    
    switch (span_type){
        case T_INTSPAN: { // integer -|- intspan
            BinaryExecutor::Execute<int32_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](int32_t value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = adjacent_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
            }
        );
            break;
        }
        case T_BIGINTSPAN: { // bigint -|- bigintspan
            BinaryExecutor::Execute<int64_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](int64_t value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);       
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = adjacent_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
                }
            ); 
            break;
        }
        case T_FLOATSPAN: { // double -|- floatspan
            BinaryExecutor::Execute<double, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](double value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);      
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid floatspan data: null pointer");
                    }
                    bool ret = adjacent_span_value(span, Float8GetDatum(value));
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_DATESPAN: { // date -|- datespan
            BinaryExecutor::Execute<int32_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](int32_t value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);     
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid datespan data: null pointer");
                    }
                    bool ret = adjacent_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_TSTZSPAN: { // timestamptz -|- tstzspan
            BinaryExecutor::Execute<timestamp_tz_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](timestamp_tz_t ts_duckdb, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    bool ret = adjacent_span_value(span, Datum(ts_meos.value));
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        default:
            throw NotImplementedException("value -|- SPAN: unsupported span type");
    }
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// --- OPERATOR: span -|- value ---
void SpanFunctions::Adjacent_span_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[0];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span_vec.GetType().GetAlias());    
    
    switch (span_type){
        case T_INTSPAN: { // intspan -|- integer
            BinaryExecutor::Execute<string_t, int32_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int32_t value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = adjacent_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
            }
        );
            break;
        }
        case T_BIGINTSPAN: { // bigintspan -|- bigint
            BinaryExecutor::Execute<string_t, int64_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int64_t value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);       
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = adjacent_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
                }
            ); 
            break;
        }
        case T_FLOATSPAN: { // floatspan -|- double
            BinaryExecutor::Execute<string_t, double, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, double value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);      
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);   
                        throw InvalidInputException("Invalid floatspan data: null pointer");
                    }
                    bool ret = adjacent_span_value(span, Float8GetDatum(value));
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_DATESPAN: { // datespan -|- date
            BinaryExecutor::Execute<string_t, int32_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int32_t value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid datespan data: null pointer");
                    }
                    bool ret = adjacent_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
                });
            break;      
        }
        case T_TSTZSPAN: { // tstzspan -|- timestamptz
            BinaryExecutor::Execute<string_t, timestamp_tz_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, timestamp_tz_t ts_duckdb) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    bool ret = adjacent_span_value(span, Datum(ts_meos.value));
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        default:
            throw NotImplementedException("SPAN -|- value: unsupported span type");
    }
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// --- OPERATOR: span -|- span ---
void SpanFunctions::Adjacent_span_span(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t span1_blob, string_t span2_blob) -> bool {
            const uint8_t *span1_data = reinterpret_cast<const uint8_t*>(span1_blob.GetData());
            size_t span1_data_size = span1_blob.GetSize();
            uint8_t *span1_data_copy = (uint8_t*)malloc(span1_data_size);
            memcpy(span1_data_copy, span1_data, span1_data_size);
            Span *span1 = reinterpret_cast<Span*>(span1_data_copy);
            if (!span1) {
                free(span1_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }   
            const uint8_t *span2_data = reinterpret_cast<const uint8_t*>(span2_blob.GetData());
            size_t span2_data_size = span2_blob.GetSize();
            uint8_t *span2_data_copy = (uint8_t*)malloc(span2_data_size);
            memcpy(span2_data_copy, span2_data, span2_data_size);
            Span *span2 = reinterpret_cast<Span*>(span2_data_copy);
            if (!span2) {
                free(span2_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }
            bool ret = adjacent_span_span(span1, span2);
            free(span1);
            free(span2);
            return ret;
        }
    );
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// --- POSITION OPERATORS ---

// --- OPERATOR: value << span ---
void SpanFunctions::Left_value_span(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[1];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span_vec.GetType().GetAlias());    
    
    switch (span_type){
        case T_INTSPAN: { // integer << intspan
            BinaryExecutor::Execute<int32_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](int32_t value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = left_value_span(Datum(value), span);
                    free(span_data_copy);
                    return ret;
            }
        );
            break;
        }
        case T_BIGINTSPAN: { // bigint << bigintspan
            BinaryExecutor::Execute<int64_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](int64_t value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);       
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = left_value_span(Datum(value), span);
                    free(span_data_copy);
                    return ret;
                }
            ); 
            break;
        }
        case T_FLOATSPAN: { // double << floatspan
            BinaryExecutor::Execute<double, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](double value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);      
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid floatspan data: null pointer");
                    }
                    bool ret = left_value_span(Float8GetDatum(value), span);
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_DATESPAN: { // date << datespan
            BinaryExecutor::Execute<int32_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](int32_t value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid datespan data: null pointer");
                    }
                    bool ret = left_value_span(Datum(value), span);
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_TSTZSPAN: { // timestamptz << tstzspan
            BinaryExecutor::Execute<timestamp_tz_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](timestamp_tz_t ts_duckdb, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    bool ret = left_value_span(Datum(ts_meos.value), span);
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        default:
            throw NotImplementedException("value << SPAN: unsupported span type");
    }
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}
// --- OPERATOR: span << value ---
void SpanFunctions::Left_span_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[0];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span_vec.GetType().GetAlias());    
    
    switch (span_type){
        case T_INTSPAN: { // intspan << integer
            BinaryExecutor::Execute<string_t, int32_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int32_t value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = left_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
            }
        );
            break;
        }
        case T_BIGINTSPAN: { // bigintspan << bigint
            BinaryExecutor::Execute<string_t, int64_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int64_t value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);       
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = left_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
                }
            ); 
            break;
        }
        case T_FLOATSPAN: { // floatspan << double
            BinaryExecutor::Execute<string_t, double, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, double value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);      
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);       
                        throw InvalidInputException("Invalid floatspan data: null pointer");
                    }
                    bool ret = left_span_value(span, Float8GetDatum(value));
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_DATESPAN: { // datespan << date  
            BinaryExecutor::Execute<string_t, int32_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int32_t value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid datespan data: null pointer");
                    }
                    bool ret = left_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
                });
            break;      
        }
        case T_TSTZSPAN: { // tstzspan << timestamptz
            BinaryExecutor::Execute<string_t, timestamp_tz_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, timestamp_tz_t ts_duckdb) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    bool ret = left_span_value(span, Datum(ts_meos.value));
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        default:
            throw NotImplementedException("SPAN << value: unsupported span type");
    }
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}
// --- OPERATOR: span << span ---
void SpanFunctions::Left_span_span(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t span1_blob, string_t span2_blob) -> bool {
            const uint8_t *span1_data = reinterpret_cast<const uint8_t*>(span1_blob.GetData());
            size_t span1_data_size = span1_blob.GetSize();
            uint8_t *span1_data_copy = (uint8_t*)malloc(span1_data_size);
            memcpy(span1_data_copy, span1_data, span1_data_size);
            Span *span1 = reinterpret_cast<Span*>(span1_data_copy);
            if (!span1) {
                free(span1_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }   
            const uint8_t *span2_data = reinterpret_cast<const uint8_t*>(span2_blob.GetData());
            size_t span2_data_size = span2_blob.GetSize();
            uint8_t *span2_data_copy = (uint8_t*)malloc(span2_data_size);
            memcpy(span2_data_copy, span2_data, span2_data_size);
            Span *span2 = reinterpret_cast<Span*>(span2_data_copy);
            if (!span2) {
                free(span2_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }
            bool ret = left_span_span(span1, span2);
            free(span1);
            free(span2);
            return ret;
        }
    );
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}
// --- OPERATOR: value >> span ---
void SpanFunctions::Right_value_span(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[1];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span_vec.GetType().GetAlias());    
    
    switch (span_type){
        case T_INTSPAN: { // integer >> intspan
            BinaryExecutor::Execute<int32_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](int32_t value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = right_value_span(Datum(value), span);
                    free(span_data_copy);
                    return ret;
            }
        );
            break;
        }
        case T_BIGINTSPAN: { // bigint >> bigintspan
            BinaryExecutor::Execute<int64_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](int64_t value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);       
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = right_value_span(Datum(value), span);
                    free(span_data_copy);
                    return ret;
                }
            ); 
            break;
        }
        case T_FLOATSPAN: { // double >> floatspan
            BinaryExecutor::Execute<double, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](double value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);      
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid floatspan data: null pointer");
                    }
                    bool ret = right_value_span(Float8GetDatum(value), span);
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_DATESPAN: { // date >> datespan
            BinaryExecutor::Execute<int32_t, string_t, bool>(       
                args.data[0], span_vec, result, args.size(),
                [&](int32_t value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid datespan data: null pointer");
                    }
                    bool ret = right_value_span(Datum(value), span);
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_TSTZSPAN: { // timestamptz >> tstzspan
            BinaryExecutor::Execute<timestamp_tz_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](timestamp_tz_t ts_duckdb, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    bool ret = right_value_span(Datum(ts_meos.value), span);
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        default:
            throw NotImplementedException("value >> SPAN: unsupported span type");
    }
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}
// --- OPERATOR: span >> value ---
void SpanFunctions::Right_span_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[0];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span_vec.GetType().GetAlias());
    switch (span_type){
        case T_INTSPAN: { // intspan >> integer
            BinaryExecutor::Execute<string_t, int32_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int32_t value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = right_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
            }
        );
            break;
        }
        case T_BIGINTSPAN: { // bigintspan >> bigint
            BinaryExecutor::Execute<string_t, int64_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int64_t value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);       
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = right_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
                }
            ); 
            break;
        }
        case T_FLOATSPAN: { // floatspan >> double
            BinaryExecutor::Execute<string_t, double, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, double value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);      
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid floatspan data: null pointer");
                    }
                    bool ret = right_span_value(span, Float8GetDatum(value));
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_DATESPAN: { // datespan >> date
            BinaryExecutor::Execute<string_t, int32_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int32_t value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid datespan data: null pointer");
                    }
                    bool ret = right_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
                });
            break;      
        }
        case T_TSTZSPAN: { // tstzspan >> timestamptz
            BinaryExecutor::Execute<string_t, timestamp_tz_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, timestamp_tz_t ts_duckdb) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    bool ret = right_span_value(span, Datum(ts_meos.value));
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        default:
            throw NotImplementedException("SPAN >> value: unsupported span type");
    }
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}
// --- OPERATOR: span >> span ---
void SpanFunctions::Right_span_span(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t span1_blob, string_t span2_blob) -> bool {
            const uint8_t *span1_data = reinterpret_cast<const uint8_t*>(span1_blob.GetData());
            size_t span1_data_size = span1_blob.GetSize();
            uint8_t *span1_data_copy = (uint8_t*)malloc(span1_data_size);
            memcpy(span1_data_copy, span1_data, span1_data_size);
            Span *span1 = reinterpret_cast<Span*>(span1_data_copy);
            if (!span1) {
                free(span1_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }   
            const uint8_t *span2_data = reinterpret_cast<const uint8_t*>(span2_blob.GetData());
            size_t span2_data_size = span2_blob.GetSize();
            uint8_t *span2_data_copy = (uint8_t*)malloc(span2_data_size);
            memcpy(span2_data_copy, span2_data, span2_data_size);
            Span *span2 = reinterpret_cast<Span*>(span2_data_copy);
            if (!span2) {
                free(span2_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }
            bool ret = right_span_span(span1, span2);
            free(span1);
            free(span2);
            return ret;
        }
    );
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// ---OPERATOR: value &< span ---
void SpanFunctions::Overleft_value_span(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[1];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span_vec.GetType().GetAlias());    
    
    switch (span_type){
        case T_INTSPAN: { // integer &< intspan
            BinaryExecutor::Execute<int32_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](int32_t value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = overleft_value_span(Datum(value), span);
                    free(span_data_copy);
                    return ret;
            }
        );
            break;
        }
        case T_BIGINTSPAN: { // bigint &< bigintspan
            BinaryExecutor::Execute<int64_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](int64_t value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);       
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = overleft_value_span(Datum(value), span);
                    free(span_data_copy);
                    return ret;
                }
            ); 
            break;
        }
        case T_FLOATSPAN: { // double &< floatspan
            BinaryExecutor::Execute<double, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](double value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);      
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid floatspan data: null pointer");
                    }
                    bool ret = overleft_value_span(Float8GetDatum(value), span);
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_DATESPAN: { // date &< datespan
            BinaryExecutor::Execute<int32_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](int32_t value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid datespan data: null pointer");
                    }
                    bool ret = overleft_value_span(Datum(value), span);
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_TSTZSPAN: { // timestamptz &< tstzspan
            BinaryExecutor::Execute<timestamp_tz_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](timestamp_tz_t ts_duckdb, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    bool ret = overleft_value_span(Datum(ts_meos.value), span);
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        default:
            throw NotImplementedException("value &< SPAN: unsupported span type");
    }
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}
// ---OPERATOR: span &< value ---
void SpanFunctions::Overleft_span_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[0];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span_vec.GetType().GetAlias());
    switch (span_type){
        case T_INTSPAN: { // intspan &< integer
            BinaryExecutor::Execute<string_t, int32_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int32_t value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = overleft_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
            }
        );
            break;
        }
        case T_BIGINTSPAN: { // bigintspan &< bigint
            BinaryExecutor::Execute<string_t, int64_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int64_t value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);       
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = overleft_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
                }
            ); 
            break;
        }
        case T_FLOATSPAN: { // floatspan &< double
            BinaryExecutor::Execute<string_t, double, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, double value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);      
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {    
                        free(span_data_copy);
                        throw InvalidInputException("Invalid floatspan data: null pointer");
                    }
                    bool ret = overleft_span_value(span, Float8GetDatum(value));
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_DATESPAN: { // datespan &< date
            BinaryExecutor::Execute<string_t, int32_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int32_t value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid datespan data: null pointer");
                    }
                    bool ret = overleft_span_value(span, Datum(value)); 
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_TSTZSPAN: { // tstzspan &< timestamptz
            BinaryExecutor::Execute<string_t, timestamp_tz_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, timestamp_tz_t ts_duckdb) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    bool ret = overleft_span_value(span, Datum(ts_meos.value));
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        default:
            throw NotImplementedException("SPAN &< value: unsupported span type");
    }
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}   

// ---OPERATOR: span &< span ---
void SpanFunctions::Overleft_span_span(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t span1_blob, string_t span2_blob) -> bool {
            const uint8_t *span1_data = reinterpret_cast<const uint8_t*>(span1_blob.GetData());
            size_t span1_data_size = span1_blob.GetSize();
            uint8_t *span1_data_copy = (uint8_t*)malloc(span1_data_size);
            memcpy(span1_data_copy, span1_data, span1_data_size);
            Span *span1 = reinterpret_cast<Span*>(span1_data_copy);
            if (!span1) {
                free(span1_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }   
            const uint8_t *span2_data = reinterpret_cast<const uint8_t*>(span2_blob.GetData());
            size_t span2_data_size = span2_blob.GetSize();
            uint8_t *span2_data_copy = (uint8_t*)malloc(span2_data_size);
            memcpy(span2_data_copy, span2_data, span2_data_size);
            Span *span2 = reinterpret_cast<Span*>(span2_data_copy);
            if (!span2) {
                free(span2_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }
            bool ret = overleft_span_span(span1, span2);
            free(span1);
            free(span2);
            return ret;
        }
    );
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// --- OPERATOR: value &> span ---
void SpanFunctions::Overright_value_span(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[1];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span_vec.GetType().GetAlias());
    switch (span_type){
        case T_INTSPAN: { // integer &> intspan
            BinaryExecutor::Execute<int32_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](int32_t value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = overright_value_span(Datum(value), span);
                    free(span_data_copy);
                    return ret;
            }
        );
            break;
        }
        case T_BIGINTSPAN: { // bigint &> bigintspan
            BinaryExecutor::Execute<int64_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](int64_t value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);       
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = overright_value_span(Datum(value), span);
                    free(span_data_copy);
                    return ret;
                }
            ); 
            break;
        }
        case T_FLOATSPAN: { // double &> floatspan
            BinaryExecutor::Execute<double, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](double value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);      
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid floatspan data: null pointer");
                    }
                    bool ret = overright_value_span(Float8GetDatum(value), span);
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_DATESPAN: { // date &> datespan
            BinaryExecutor::Execute<int32_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](int32_t value, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);     
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid datespan data: null pointer");
                    }
                    bool ret = overright_value_span(Datum(value), span);
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_TSTZSPAN: { // timestamptz &> tstzspan
            BinaryExecutor::Execute<timestamp_tz_t, string_t, bool>(
                args.data[0], span_vec, result, args.size(),
                [&](timestamp_tz_t ts_duckdb, string_t span_blob) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    bool ret = overright_value_span(Datum(ts_meos.value), span);
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        default:
            throw NotImplementedException("value &> SPAN: unsupported span type");
    }
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}   

// --- OPERATOR: span &> value ---
void SpanFunctions::Overright_span_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[0];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span_vec.GetType().GetAlias());
    switch (span_type){
        case T_INTSPAN: { // intspan &> integer
            BinaryExecutor::Execute<string_t, int32_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int32_t value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    bool ret = overright_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
            }
        );
            break;
        }
        case T_BIGINTSPAN: { // bigintspan &> bigint
            BinaryExecutor::Execute<string_t, int64_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int64_t value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);    
                        throw InvalidInputException("Invalid SPAN data: null pointer");     
                    }
                    bool ret = overright_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
                }
            ); 
            break;
        }
        case T_FLOATSPAN: { // floatspan &> double
            BinaryExecutor::Execute<string_t, double, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, double value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);      
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid floatspan data: null pointer");
                    }
                    bool ret = overright_span_value(span, Float8GetDatum(value));
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_DATESPAN: { // datespan &> date
            BinaryExecutor::Execute<string_t, int32_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int32_t value) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid datespan data: null pointer");
                    }
                    bool ret = overright_span_value(span, Datum(value));
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        case T_TSTZSPAN: { // tstzspan &> timestamptz
            BinaryExecutor::Execute<string_t, timestamp_tz_t, bool>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, timestamp_tz_t ts_duckdb) -> bool {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    bool ret = overright_span_value(span, Datum(ts_meos.value));
                    free(span_data_copy);
                    return ret;
                });
            break;
        }
        default:
            throw NotImplementedException("SPAN &> value: unsupported span type");
    }
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}   

// --- OPERATOR: span &> span ---
void SpanFunctions::Overright_span_span(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t span1_blob, string_t span2_blob) -> bool {
            const uint8_t *span1_data = reinterpret_cast<const uint8_t*>(span1_blob.GetData());
            size_t span1_data_size = span1_blob.GetSize();
            uint8_t *span1_data_copy = (uint8_t*)malloc(span1_data_size);
            memcpy(span1_data_copy, span1_data, span1_data_size);
            Span *span1 = reinterpret_cast<Span*>(span1_data_copy);
            if (!span1) {
                free(span1_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }   
            const uint8_t *span2_data = reinterpret_cast<const uint8_t*>(span2_blob.GetData());
            size_t span2_data_size = span2_blob.GetSize();
            uint8_t *span2_data_copy = (uint8_t*)malloc(span2_data_size);
            memcpy(span2_data_copy, span2_data, span2_data_size);
            Span *span2 = reinterpret_cast<Span*>(span2_data_copy);
            if (!span2) {
                free(span2_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }
            bool ret = overright_span_span(span1, span2);
            free(span1);
            free(span2);
            return ret;
        }
    );
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// --- SET OPERATOR ---
void SpanFunctions::Union_value_span(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[1];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span_vec.GetType().GetAlias());
    switch (span_type){
        case T_INTSPAN: { // integer + intspan
            BinaryExecutor::Execute<int32_t, string_t, string_t>(
                args.data[0], span_vec, result, args.size(),
                [&](int32_t value, string_t span_blob) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);     
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    SpanSet *ret = union_value_span(Datum(value), span);
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);
                    free(ret);
                    return stored_data;
            }
        );
            break;
        case T_BIGINTSPAN: { // bigint + bigintspan
            BinaryExecutor::Execute<int64_t, string_t, string_t>(
                args.data[0], span_vec, result, args.size(),
                [&](int64_t value, string_t span_blob) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);       
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    SpanSet *ret = union_value_span(Datum(value), span);
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);
                    free(ret);
                    return stored_data;
                }
            ); 
            break;
        }
        case T_FLOATSPAN: { // double + floatspan
            BinaryExecutor::Execute<double, string_t, string_t>(
                args.data[0], span_vec, result, args.size(),
                [&](double value, string_t span_blob) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);      
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid floatspan data: null pointer");
                    }
                    SpanSet *ret = union_value_span(Float8GetDatum(value), span);
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);
                    free(ret);
                    return stored_data;
                });
            break;
        }
        case T_DATESPAN: { // date + datespan
            BinaryExecutor::Execute<int32_t, string_t, string_t>(       
                args.data[0], span_vec, result, args.size(),
                [&](int32_t value, string_t span_blob) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    SpanSet *ret = union_value_span(Datum(value), span);
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);
                    free(ret);
                    return stored_data;
                });
            break;
        }
        case T_TSTZSPAN: { // timestamptz + tstzspan
            BinaryExecutor::Execute<timestamp_tz_t, string_t, string_t>(
                args.data[0], span_vec, result, args.size(),
                [&](timestamp_tz_t ts_duckdb, string_t span_blob) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);  
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    SpanSet *ret = union_value_span(Datum(ts_meos.value), span);
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);
                    memcpy(span_buffer, ret, span_size);    
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);
                    free(ret);
                    return stored_data;
                });
            break;
        }
        default:
            throw NotImplementedException("value + SPAN: unsupported span type");
    }
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}
}

void SpanFunctions::Union_span_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[0];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span_vec.GetType().GetAlias());
    switch (span_type){
        case T_INTSPAN: { // intspan + integer
            BinaryExecutor::Execute<string_t, int32_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int32_t value) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    SpanSet *ret = union_span_value(span, Datum(value));
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);
                    free(ret);
                    return stored_data;
            }
        );
            break;
        case T_BIGINTSPAN: { // bigintspan + bigint
            BinaryExecutor::Execute<string_t, int64_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int64_t value) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);       
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    SpanSet *ret = union_span_value(span, Datum(value));
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);
                    free(ret);
                    return stored_data;
                }
            ); 
            break;
        }
        case T_FLOATSPAN: { // floatspan + double
            BinaryExecutor::Execute<string_t, double, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, double value) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);      
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid floatspan data: null pointer");
                    }
                    SpanSet *ret = union_span_value(span, Float8GetDatum(value));
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);
                    free(ret);
                    return stored_data;
                });
            break;
        }
        case T_DATESPAN: { // datespan + date
            BinaryExecutor::Execute<string_t, int32_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int32_t value) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    SpanSet *ret = union_span_value(span, Datum(value));
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);    
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);   
                    free(ret);
                    return stored_data;
                });
            break;
        }
        case T_TSTZSPAN: { // tstzspan + timestamptz
            BinaryExecutor::Execute<string_t, timestamp_tz_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, timestamp_tz_t ts_duckdb) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);  
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    SpanSet *ret = union_span_value(span, Datum(ts_meos.value));
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);    
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);   
                    free(ret);
                    return stored_data;
                });
            break;
        }
        default:
            throw NotImplementedException("SPAN + value: unsupported span type");
    }
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}
}

void SpanFunctions::Union_span_span(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t span1_blob, string_t span2_blob) -> string_t {
            const uint8_t *span1_data = reinterpret_cast<const uint8_t*>(span1_blob.GetData());
            size_t span1_data_size = span1_blob.GetSize();
            uint8_t *span1_data_copy = (uint8_t*)malloc(span1_data_size);
            memcpy(span1_data_copy, span1_data, span1_data_size);
            Span *span1 = reinterpret_cast<Span*>(span1_data_copy);
            if (!span1) {
                free(span1_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }   
            const uint8_t *span2_data = reinterpret_cast<const uint8_t*>(span2_blob.GetData());
            size_t span2_data_size = span2_blob.GetSize();
            uint8_t *span2_data_copy = (uint8_t*)malloc(span2_data_size);
            memcpy(span2_data_copy, span2_data, span2_data_size);
            Span *span2 = reinterpret_cast<Span*>(span2_data_copy);
            if (!span2) {
                free(span2_data_copy);
                throw InvalidInputException("Invalid SPAN data: null pointer");
            }
            SpanSet *ret = union_span_span(span1, span2);
            size_t out_size = spanset_mem_size(ret);
            string_t stored_data = StringVector::AddStringOrBlob(result, (const char *) ret, out_size);
            free(span1);
            free(span2);
            free(ret);
            return stored_data;
        }
    );
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}
// --- OPERATOR: INTERSECTION ---
void SpanFunctions::Intersection_value_span(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[1];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span_vec.GetType().GetAlias());
    switch (span_type){
        case T_INTSPAN: { // integer * intspan
            BinaryExecutor::Execute<int32_t, string_t, string_t>(
                args.data[0], span_vec, result, args.size(),
                [&](int32_t value, string_t span_blob) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);     
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    Span *ret = intersection_value_span(Datum(value), span);
                    if (!ret) {
                        free(span_data_copy);
                        return string_t();
                    }
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);
                    free(ret);
                    return stored_data;
            }
        );
            break;
        }
        case T_BIGINTSPAN: { // bigint * bigintspan
            BinaryExecutor::Execute<int64_t, string_t, string_t>(
                args.data[0], span_vec, result, args.size(),
                [&](int64_t value, string_t span_blob) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);       
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    Span *ret = intersection_value_span(Datum(value), span);
                    if (!ret) {
                        free(span_data_copy);
                        return string_t();
                    }
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);
                    free(ret);
                    return stored_data;
                }
            ); 
            break;
        }
        case T_FLOATSPAN: { // double * floatspan
            BinaryExecutor::Execute<double, string_t, string_t>(
                args.data[0], span_vec, result, args.size(),
                [&](double value, string_t span_blob) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);      
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid floatspan data: null pointer");
                    }
                    Span *ret = intersection_value_span(Float8GetDatum(value), span);
                    if (!ret) {
                        free(span_data_copy);
                        return string_t();
                    }
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);
                    free(ret);
                    return stored_data;
                });
            break;
        }
        case T_DATESPAN: { // date * datespan
            BinaryExecutor::Execute<int32_t, string_t, string_t>(
                args.data[0], span_vec, result, args.size(),
                [&](int32_t value, string_t span_blob) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    Span *ret = intersection_value_span(Datum(value), span);
                    if (!ret) {
                        free(span_data_copy);
                        return string_t();
                    }
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);    
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);   
                    free(ret);
                    return stored_data;
                });
            break;
        }
        case T_TSTZSPAN: { // timestamptz * tstzspan
            BinaryExecutor::Execute<timestamp_tz_t, string_t, string_t>(
                args.data[0], span_vec, result, args.size(),
                [&](timestamp_tz_t ts_duckdb, string_t span_blob) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);  
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    Span *ret = intersection_value_span(Datum(ts_meos.value), span);
                    if (!ret) {
                        free(span_data_copy);
                        return string_t();
                    }
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);    
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);   
                    free(ret);
                    return stored_data;
                });
            break;
        }
        default:
            throw NotImplementedException("value * SPAN: unsupported span type");
    }
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void SpanFunctions::Intersection_span_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[0];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span_vec.GetType().GetAlias());
    switch (span_type){
        case T_INTSPAN: { // intspan * integer
            BinaryExecutor::Execute<string_t, int32_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int32_t value) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    Span *ret = intersection_span_value(span, Datum(value));
                    if (!ret) {
                        free(span_data_copy);
                        return string_t();
                    }
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);
                    free(ret);
                    return stored_data;
            }
        );
            break;
        }
        case T_BIGINTSPAN: { // bigintspan * bigint
            BinaryExecutor::Execute<string_t, int64_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int64_t value) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);       
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }       
                    Span *ret = intersection_span_value(span, Datum(value));
                    if (!ret) {
                        free(span_data_copy);
                        return string_t();
                    }
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);
                    free(ret);
                    return stored_data;
                }
            );
            break;
        }
        case T_FLOATSPAN: { // floatspan * double
            BinaryExecutor::Execute<string_t, double, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, double value) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);      
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid floatspan data: null pointer");
                    }
                    Span *ret = intersection_span_value(span, Float8GetDatum(value));
                    if (!ret) {
                        free(span_data_copy);
                        return string_t();
                    }
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);
                    memcpy(span_buffer, ret, span_size);        
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);
                    free(ret);
                    return stored_data;
                });
            break;
        }
        case T_DATESPAN: { // datespan * date
            BinaryExecutor::Execute<string_t, int32_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int32_t value) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    Span *ret = intersection_span_value(span, Datum(value));
                    if (!ret) {
                        free(span_data_copy);
                        return string_t();
                    }
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);    
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);   
                    free(ret);
                    return stored_data;
                });
            break;
        }
        case T_TSTZSPAN: { // tstzspan * timestamptz
            BinaryExecutor::Execute<string_t, timestamp_tz_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, timestamp_tz_t ts_duckdb) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);  
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    Span *ret = intersection_span_value(span, Datum(ts_meos.value));
                    if (!ret) {
                        free(span_data_copy);
                        return string_t();
                    }
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);    
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);   
                    free(ret);
                    return stored_data;
                });
            break;
        }
        default:
            throw NotImplementedException("SPAN * value: unsupported span type");
    }
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void SpanFunctions::Intersection_span_span(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t span1_blob, string_t span2_blob, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *span1_data = reinterpret_cast<const uint8_t*>(span1_blob.GetData());
            size_t span1_data_size = span1_blob.GetSize();
            uint8_t *span1_data_copy = (uint8_t*)malloc(span1_data_size);
            memcpy(span1_data_copy, span1_data, span1_data_size);
            Span *span1 = reinterpret_cast<Span*>(span1_data_copy);
            if (!span1) {
                free(span1_data_copy);
                throw InvalidInputException("Invalid tstzspan data: null pointer");
            }

            const uint8_t *span2_data = reinterpret_cast<const uint8_t*>(span2_blob.GetData());
            size_t span2_data_size = span2_blob.GetSize();
            uint8_t *span2_data_copy = (uint8_t*)malloc(span2_data_size);
            memcpy(span2_data_copy, span2_data, span2_data_size);
            Span *span2 = reinterpret_cast<Span*>(span2_data_copy);
            if (!span2) {
                free(span2_data_copy);
                throw InvalidInputException("Invalid tstzspan data: null pointer");
            }

            Span *ret = intersection_span_span(span1, span2);
            if (!ret) {
                free(span1);
                free(span2);
                mask.SetInvalid(idx);
                return string_t();
            }

            size_t span_size = sizeof(*ret);
            uint8_t *span_buffer = (uint8_t*) malloc(span_size);
            memcpy(span_buffer, ret, span_size);
            string_t span_string_t((char *) span_buffer, span_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
            free(span_buffer);
            free(ret);
            free(span1);
            free(span2);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// --- OPERATOR: MINUS ---
void SpanFunctions::Minus_value_span(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[1];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span_vec.GetType().GetAlias());
    switch (span_type){
        case T_INTSPAN: { // integer - intspan
            BinaryExecutor::Execute<int32_t, string_t, string_t>(
                args.data[0], span_vec, result, args.size(),
                [&](int32_t value, string_t span_blob) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);     
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    SpanSet *ret = minus_value_span(Datum(value), span);
                    if (!ret) {
                        free(span_data_copy);
                        return string_t();
                    }
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);
                    free(ret);
                    return stored_data;
            }
        );
            break;
        }
        case T_BIGINTSPAN: { // bigint - bigintspan
            BinaryExecutor::Execute<int64_t, string_t, string_t>(
                args.data[0], span_vec, result, args.size(),
                [&](int64_t value, string_t span_blob) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);       
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }       
                    SpanSet *ret = minus_value_span(Datum(value), span);
                    if (!ret) {
                        free(span_data_copy);
                        return string_t();
                    }
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);
                    free(ret);
                    return stored_data;
                }
            );
            break;
        }
        case T_FLOATSPAN: { // float - floatspan
            BinaryExecutor::Execute<double, string_t, string_t>(
                args.data[0], span_vec, result, args.size(),
                [&](double value, string_t span_blob) -> string_t { 
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);      
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid floatspan data: null pointer");
                    }
                    SpanSet *ret = minus_value_span(Float8GetDatum(value), span);
                    if (!ret) {
                        free(span_data_copy);
                        return string_t();
                    }
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);
                    memcpy(span_buffer, ret, span_size);        
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);
                    free(ret);
                    return stored_data;
                });
            break;
        }
        case T_DATESPAN: { // date - datespan
            BinaryExecutor::Execute<int32_t, string_t, string_t>(
                args.data[0], span_vec, result, args.size(),
                [&](int32_t value, string_t span_blob) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    SpanSet *ret = minus_value_span(Datum(value), span);
                    if (!ret) {
                        free(span_data_copy);
                        return string_t();
                    }
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);    
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);   
                    free(ret);
                    return stored_data;
                });
            break;
        }
        case T_TSTZSPAN: { // timestamptz - tstzspan
            BinaryExecutor::Execute<timestamp_tz_t, string_t, string_t>(
                args.data[0], span_vec, result, args.size(),
                [&](timestamp_tz_t ts_duckdb, string_t span_blob) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);  
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    SpanSet *ret = minus_value_span(Datum(ts_meos.value), span);
                    if (!ret) {
                        free(span_data_copy);
                        return string_t();
                    }
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);    
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);   
                    free(ret);
                    return stored_data;
                });
            break;
        }   
        default:
            throw NotImplementedException("value - SPAN: unsupported span type");
    }
     if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void SpanFunctions::Minus_span_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[0];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span_vec.GetType().GetAlias());
    switch (span_type){
        case T_INTSPAN: { // intspan - integer
            BinaryExecutor::Execute<string_t, int32_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int32_t value) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    SpanSet *ret = minus_span_value(span, Datum(value));
                    if (!ret) {
                        free(span_data_copy);
                        return string_t();
                    }
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);
                    free(ret);
                    return stored_data;
            }
        );
            break;
        }
        case T_BIGINTSPAN: { // bigintspan - bigint
            BinaryExecutor::Execute<string_t, int64_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int64_t value) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);       
                        throw InvalidInputException("Invalid SPAN data: null pointer"); 
                    }
                    SpanSet *ret = minus_span_value(span, Datum(value));
                    if (!ret) {
                        free(span_data_copy);
                        return string_t();
                    }
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);
                    free(ret);
                    return stored_data; 
                }
            );
            break;
        }
        case T_FLOATSPAN: { // floatspan - double
            BinaryExecutor::Execute<string_t, double, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, double value) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);      
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid floatspan data: null pointer");
                    }
                    SpanSet *ret = minus_span_value(span, Float8GetDatum(value));
                    if (!ret) {
                        free(span_data_copy);
                        return string_t();
                    }
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);
                    memcpy(span_buffer, ret, span_size);        
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);
                    free(ret);
                    return stored_data;
                });
            break;
        } 
        case T_DATESPAN: { // datespan - date
            BinaryExecutor::Execute<string_t, int32_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int32_t value) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    SpanSet *ret = minus_span_value(span, Datum(value));
                    if (!ret) {
                        free(span_data_copy);
                        return string_t();
                    }
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);    
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);   
                    free(ret);
                    return stored_data;
                });
            break;
        }
        case T_TSTZSPAN: { // tstzspan - timestamptz
            BinaryExecutor::Execute<string_t, timestamp_tz_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, timestamp_tz_t ts_duckdb) -> string_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);  
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer"); 
                    }
                    SpanSet *ret = minus_span_value(span, Datum(ts_meos.value));
                    if (!ret) {
                        free(span_data_copy);
                        return string_t();
                    }
                    size_t span_size = sizeof(*ret);
                    uint8_t *span_buffer = (uint8_t*) malloc(span_size);    
                    memcpy(span_buffer, ret, span_size);
                    string_t span_string_t((char *) span_buffer, span_size);
                    string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
                    free(span_buffer);
                    free(span_data_copy);   
                    free(ret);
                    return stored_data;
                });
            break;      
        default:
            throw NotImplementedException("SPAN - value: unsupported span type");   
        }
        if (args.size() == 1) {
            result.SetVectorType(VectorType::CONSTANT_VECTOR);
        }
    }
}

void SpanFunctions::Minus_span_span(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t span1_blob, string_t span2_blob, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *span1_data = reinterpret_cast<const uint8_t*>(span1_blob.GetData());
            size_t span1_data_size = span1_blob.GetSize();
            uint8_t *span1_data_copy = (uint8_t*)malloc(span1_data_size);
            memcpy(span1_data_copy, span1_data, span1_data_size);
            Span *span1 = reinterpret_cast<Span*>(span1_data_copy);
            if (!span1) {
                free(span1_data_copy);
                throw InvalidInputException("Invalid tstzspan data: null pointer");
            }

            const uint8_t *span2_data = reinterpret_cast<const uint8_t*>(span2_blob.GetData());
            size_t span2_data_size = span2_blob.GetSize();
            uint8_t *span2_data_copy = (uint8_t*)malloc(span2_data_size);
            memcpy(span2_data_copy, span2_data, span2_data_size);
            Span *span2 = reinterpret_cast<Span*>(span2_data_copy);
            if (!span2) {
                free(span2_data_copy);
                throw InvalidInputException("Invalid tstzspan data: null pointer");
            }

            SpanSet *ret = minus_span_span(span1, span2);
            if (!ret) {
                free(span1);
                free(span2);
                mask.SetInvalid(idx);
                return string_t();
            }

            size_t span_size = sizeof(*ret);
            uint8_t *span_buffer = (uint8_t*) malloc(span_size);
            memcpy(span_buffer, ret, span_size);
            string_t span_string_t((char *) span_buffer, span_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
            free(span_buffer);
            free(ret);
            free(span1);
            free(span2);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

//--- DISTANCE FUNCTIONS ---
void SpanFunctions::Distance_span_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[0];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span_vec.GetType().GetAlias());
    switch (span_type){
        case T_INTSPAN: { // distance between intspan and integer
            BinaryExecutor::Execute<string_t, int32_t, double>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int32_t value) -> double {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    int32_t distance = distance_span_value(span, Datum(value));
                    free(span_data_copy);
                    return distance;
            }
        );
            break;
        }
        case T_BIGINTSPAN: { // distance between bigintspan and bigint
            BinaryExecutor::Execute<string_t, int64_t, double>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int64_t value) -> double {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);       
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }       
                    int64_t distance = distance_span_value(span, Datum(value));
                    free(span_data_copy);
                    return distance;
                }
            );
            break;
        }
        case T_FLOATSPAN: { // distance between floatspan and double
            BinaryExecutor::Execute<string_t, double, double>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, double value) -> double {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);      
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid floatspan data: null pointer");    
                    }
                    double distance = distance_span_value(span, Float8GetDatum(value));
                    free(span_data_copy);
                    return distance;
                });
            break;  
        }
        case T_DATESPAN: { // distance between datespan and date
            BinaryExecutor::Execute<string_t, int32_t, double>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, int32_t value) -> double {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    int32_t distance = distance_span_value(span, Datum(value));
                    free(span_data_copy);
                    return distance;
                });
            break;  
        }
        case T_TSTZSPAN: { // distance between tstzspan and timestamptz → INTERVAL
            BinaryExecutor::Execute<string_t, timestamp_tz_t, interval_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t span_blob, timestamp_tz_t ts_duckdb) -> interval_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    double secs = distance_span_timestamptz(span, (TimestampTz)ts_meos.value);
                    free(span_data_copy);
                    return SecondsToInterval(secs);
                });
            break;
        }
        default:
            throw NotImplementedException("distance between SPAN and value: unsupported span type");    

    }
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void SpanFunctions::Distance_value_span(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span_vec = args.data[1];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span_vec.GetType().GetAlias());
    switch (span_type){
        case T_INTSPAN: { // distance between integer and intspan
            BinaryExecutor::Execute<int32_t, string_t, double>(
                args.data[0], span_vec, result, args.size(),
                [&](int32_t value, string_t span_blob) -> double {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    int32_t distance = distance_span_value(span, Datum(value));
                    free(span_data_copy);
                    return distance;
            }
        );
            break;
        }
        case T_BIGINTSPAN: { // distance between bigint and bigintspan
            BinaryExecutor::Execute<int64_t, string_t, double>(
                args.data[0], span_vec, result, args.size(),
                [&](int64_t value, string_t span_blob) -> double {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);       
                        throw InvalidInputException("Invalid SPAN data: null pointer"); 
                    }
                    int64_t distance = distance_span_value(span, Datum(value));
                    free(span_data_copy);
                    return distance;
                }
            );
            break;
        }
        case T_FLOATSPAN: { // distance between double and floatspan
            BinaryExecutor::Execute<double, string_t, double>(
                args.data[0], span_vec, result, args.size(),
                [&](double value, string_t span_blob) -> double {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);      
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid floatspan data: null pointer");    
                    }
                    double distance = distance_span_value(span, Float8GetDatum(value));
                    free(span_data_copy);
                    return distance;
                });
            break;
        }
        case T_DATESPAN: { // distance between date and datespan
            BinaryExecutor::Execute<int32_t, string_t, double>(
                args.data[0], span_vec, result, args.size(),
                [&](int32_t value, string_t span_blob) -> double {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid SPAN data: null pointer");
                    }
                    int32_t distance = distance_span_value(span, Datum(value));
                    free(span_data_copy);
                    return distance;
                });
            break;
        }
        case T_TSTZSPAN: { // distance between timestamptz and tstzspan → INTERVAL
            BinaryExecutor::Execute<timestamp_tz_t, string_t, interval_t>(
                args.data[0], span_vec, result, args.size(),
                [&](timestamp_tz_t ts_duckdb, string_t span_blob) -> interval_t {
                    const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
                    size_t span_data_size = span_blob.GetSize();
                    uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
                    memcpy(span_data_copy, span_data, span_data_size);
                    timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                    Span *span = reinterpret_cast<Span*>(span_data_copy);
                    if (!span) {
                        free(span_data_copy);
                        throw InvalidInputException("Invalid tstzspan data: null pointer");
                    }
                    double secs = distance_span_timestamptz(span, (TimestampTz)ts_meos.value);
                    free(span_data_copy);
                    return SecondsToInterval(secs);
                });
            break;
        }
        default:
            throw NotImplementedException("distance between value and SPAN: unsupported span type");
    }
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void SpanFunctions::Distance_span_span(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &span1_vec = args.data[0];
    MeosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(span1_vec.GetType().GetAlias());

    if (span_type == T_TSTZSPAN) {
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, interval_t>(
            args.data[0], args.data[1], result, args.size(),
            [&](string_t s1_blob, string_t s2_blob, ValidityMask &mask, idx_t idx) -> interval_t {
                Span *s1 = (Span *)malloc(s1_blob.GetSize());
                memcpy(s1, s1_blob.GetData(), s1_blob.GetSize());
                Span *s2 = (Span *)malloc(s2_blob.GetSize());
                memcpy(s2, s2_blob.GetData(), s2_blob.GetSize());
                double secs = distance_tstzspan_tstzspan(s1, s2);
                free(s1);
                free(s2);
                return SecondsToInterval(secs);
            }
        );
    } else {
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, double>(
            args.data[0], args.data[1], result, args.size(),
            [&](string_t span1_blob, string_t span2_blob, ValidityMask &mask, idx_t idx) -> double {
                const uint8_t *span1_data = reinterpret_cast<const uint8_t*>(span1_blob.GetData());
                size_t span1_data_size = span1_blob.GetSize();
                uint8_t *span1_data_copy = (uint8_t*)malloc(span1_data_size);
                memcpy(span1_data_copy, span1_data, span1_data_size);
                Span *span1 = reinterpret_cast<Span*>(span1_data_copy);
                if (!span1) {
                    free(span1_data_copy);
                    throw InvalidInputException("Invalid SPAN data: null pointer");
                }

                const uint8_t *span2_data = reinterpret_cast<const uint8_t*>(span2_blob.GetData());
                size_t span2_data_size = span2_blob.GetSize();
                uint8_t *span2_data_copy = (uint8_t*)malloc(span2_data_size);
                memcpy(span2_data_copy, span2_data, span2_data_size);
                Span *span2 = reinterpret_cast<Span*>(span2_data_copy);
                if (!span2) {
                    free(span2_data_copy);
                    throw InvalidInputException("Invalid SPAN data: null pointer");
                }

                double distance = distance_span_span(span1, span2);
                free(span1);
                free(span2);
                return distance;
            }
        );
    }
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

}
