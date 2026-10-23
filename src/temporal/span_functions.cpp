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

// --- Span binary constructor ---



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

}
