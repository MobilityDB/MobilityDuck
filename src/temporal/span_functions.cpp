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


// --- POSITION OPERATORS ---

// --- OPERATOR: span &> span ---

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

            size_t span_size = spanset_mem_size(ret);
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

}
