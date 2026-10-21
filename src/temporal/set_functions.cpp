#include "temporal/set.hpp"
#include "temporal/set_functions.hpp"
#include "time_util.hpp"
#include "duckdb/common/vector_operations/unary_executor.hpp"
#include "duckdb/common/vector_operations/binary_executor.hpp"
#include "duckdb/common/vector_operations/ternary_executor.hpp"

extern "C" {
#include "meos.h"
#include "meos_internal.h"
#include "meos_geo.h"
}

namespace duckdb {

// --- Cast From String ---
bool SetFunctions::Set_to_text(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    source.Flatten(count);
    auto result_data = FlatVector::GetData<string_t>(result); 

    for (idx_t i = 0; i < count; ++i) {
        if (FlatVector::IsNull(source, i)) {
            FlatVector::SetNull(result, i, true);
            continue;
        }

        Value val = source.GetValue(i);
        const string_t &blob = StringValue::Get(val);
        const uint8_t *data = (const uint8_t *)(blob.GetData());
        size_t size = blob.GetSize();

        Set *s = (Set*)malloc(size);                

        memcpy(s, data, size);             
        char *cstr = set_out(s, 15);  
        result_data[i] = StringVector::AddString(result, cstr);

        free(cstr);
        free(s);
    }

    result.SetVectorType(VectorType::FLAT_VECTOR);
    return true;
}

bool SetFunctions::Text_to_set(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);

    auto target_type = result.GetType();
    MeosType set_type = SetTypeMapping::GetMeosTypeFromAlias(target_type.GetAlias());

    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input) -> string_t {
            std::string input_str(input.GetDataUnsafe(), input.GetSize());
            Set *s = nullptr;

            if (set_type == T_TEXTSET && !input_str.empty() && input_str.front() != '{') {                
                text *txt = (text *)malloc(VARHDRSZ + input_str.size());
                SET_VARSIZE(txt, VARHDRSZ + input_str.size());
                memcpy(VARDATA(txt), input_str.c_str(), input_str.size());

                s = value_set(PointerGetDatum(txt), T_TEXT);                
            } else {
                s = set_in(input_str.c_str(), set_type);                              
            }            

            string_t blob = StringVector::AddStringOrBlob(result, (const char *)s, set_mem_size(s));
            free(s);
            return blob;
        }
    );

    return true;
}

// Conversion function: base type -> set 
static inline void Write_set(Vector &result, idx_t row, Set *s) {
    auto out = FlatVector::GetData<string_t>(result);
    out[row] = StringVector::AddStringOrBlob(result, (const char *)s, set_mem_size(s));
    free(s);
}

static inline void Value_to_set_core(Vector &source, Vector &result, idx_t count, MeosType base_type) {
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);
    
    auto handle_null = [&](idx_t row) {
        FlatVector::SetNull(result, row, true);
    };

    // The base types the binding registers itself, carried as the catalog states: a pointer to
    // the value, or the eight bytes of the Datum for a by-value base. value_set copies what it
    // is given, so a pointer may address the source vector directly.
    if (! basetype_byvalue(base_type) && base_type != T_TEXT) {
        auto in = FlatVector::GetData<string_t>(source);
        for (idx_t i = 0; i < count; ++i) {
            if (FlatVector::IsNull(source, i)) { handle_null(i); continue; }
            Write_set(result, i, value_set(PointerGetDatum(in[i].GetDataUnsafe()), base_type));
        }
        return;
    }

    switch (base_type) {
        case T_INT4: {
            auto in = FlatVector::GetData<int32_t>(source);
            for (idx_t i = 0; i < count; ++i) {
                if (FlatVector::IsNull(source, i)) { handle_null(i); continue; }
                Datum d = Datum(in[i]);               
                Set *s = value_set(d, T_INT4);
                Write_set(result, i, s);
            }
            break;
        }
        case T_INT8: {
            auto in = FlatVector::GetData<int64_t>(source);
            for (idx_t i = 0; i < count; ++i) {
                if (FlatVector::IsNull(source, i)) { handle_null(i); continue; }
                Datum d = Datum(in[i]);               
                Set *s = value_set(d, T_INT8);
                Write_set(result, i, s);
            }
            break;
        }
        case T_FLOAT8: {
            auto in = FlatVector::GetData<double>(source);
            for (idx_t i = 0; i < count; ++i) {
                if (FlatVector::IsNull(source, i)) { handle_null(i); continue; }
                Datum d = Float8GetDatum(in[i]);
                Set *s = value_set(d, T_FLOAT8);
                Write_set(result, i, s);
            }
            break;
        }
        case T_TEXT: {
            auto in = FlatVector::GetData<string_t>(source);
            for (idx_t i = 0; i < count; ++i) {
                if (FlatVector::IsNull(source, i)) {
                    handle_null(i);
                    continue;
                }                
                const char *data_ptr = in[i].GetDataUnsafe();
                size_t len = in[i].GetSize();                
                text *txt = (text *)malloc(VARHDRSZ + len);
                SET_VARSIZE(txt, VARHDRSZ + len);
                memcpy(VARDATA(txt), data_ptr, len);
                Set *s = value_set(PointerGetDatum(txt), T_TEXT);
                Write_set(result, i, s);
                free(txt); 
            }
            break;
        }
            
        case T_DATE: {
            auto in = FlatVector::GetData<date_t>(source);
            for (idx_t i = 0; i < count; ++i) {
                if (FlatVector::IsNull(source, i)) { handle_null(i); continue; }
                int32_t days = (int32_t)ToMeosDate(in[i]);
                Datum d = Datum(days);
                Set *s = value_set(d, T_DATE);
                Write_set(result, i, s);
            }
            break;
        }
        case T_TIMESTAMPTZ: {
            auto in = FlatVector::GetData<timestamp_t>(source);
            for (idx_t i = 0; i < count; ++i) {
                if (FlatVector::IsNull(source, i)) { handle_null(i); continue; }
                auto meos_ts = ToMeosTimestamp(in[i]);        
                Datum d = Datum(meos_ts);
                Set *s = value_set(d, T_TIMESTAMPTZ);
                Write_set(result, i, s);
            }
            break;
        }
        default:
            throw NotImplementedException("SetConversion: unsupported base type for conversion to set");
    }
}

// --- CAST (conversion: base -> set) ----

bool SetFunctions::Value_to_set_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    auto target_type = result.GetType();
    MeosType set_type  = SetTypeMapping::GetMeosTypeFromAlias(target_type.GetAlias());
    MeosType base_type = settype_basetype(set_type);

    Value_to_set_core(source, result, count, base_type);
    return true;
}

// --- Conversion: intset <-> floatset ---
static void Intset_to_floatset_common(Vector &source, Vector &result, idx_t count) {
            UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const Set *src_set = (const Set *)blob.GetDataUnsafe();            
            Set *dst_set = intset_to_floatset(src_set);
            string_t out = StringVector::AddStringOrBlob(result, (const char *)dst_set, set_mem_size(dst_set));
            free(dst_set);
            return out;
        }
    );
}

static void Floatset_to_intset_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const Set *src_set = (const Set *)blob.GetDataUnsafe();
            Set *dst_set = floatset_to_intset(src_set);
            string_t out = StringVector::AddStringOrBlob(result, (const char *)dst_set, set_mem_size(dst_set));
            free(dst_set);
            return out;
        }
    );
}


bool SetFunctions::Intset_to_floatset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Intset_to_floatset_common(source, result, count);
    return true;    
}

bool SetFunctions::Floatset_to_intset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Floatset_to_intset_common(source, result, count);
    return true;
}

// --- Conversion: tstzset <-> dateset ---

// dateset -> tstzset
static void Dateset_to_tstzset_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const Set *src = (const Set *)blob.GetDataUnsafe();            
            Set *dst = dateset_to_tstzset(src);
            string_t out = StringVector::AddStringOrBlob(result, (const char *)dst, set_mem_size(dst));
            free(dst);
            return out;
        }
    );
}

// tstzset -> dateset
static void Tstzset_to_dateset_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const Set *src = (const Set *)blob.GetDataUnsafe();                        
            Set *dst = tstzset_to_dateset(src);
            string_t out = StringVector::AddStringOrBlob(result, (const char *)dst, set_mem_size(dst));
            free(dst);
            return out;
        }
    );
}

// --- CAST: dateset -> tstzset ---
bool SetFunctions::Dateset_to_tstzset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Dateset_to_tstzset_common(source, result, count);
    return true;
}

// --- CAST: tstzset -> dateset ---
bool SetFunctions::Tstzset_to_dateset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Tstzset_to_dateset_common(source, result, count);
    return true;
}

// floor/ceil/round/degrees/radians on floatset are generated from the catalog
// (generated_temporal_udfs.cpp); no hand bodies remain.

} // namespace duckdb
