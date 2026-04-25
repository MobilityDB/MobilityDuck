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

#ifndef WKB_EXTENDED
#define WKB_EXTENDED ((uint8_t)0x04)
#endif

namespace duckdb {

namespace {

static Set *CopySet(string_t blob) {
    Set *s = (Set *)malloc(blob.GetSize());
    memcpy(s, blob.GetData(), blob.GetSize());
    return s;
}

static string_t WriteSetBlob(Vector &result, Set *r) {
    if (!r) {
        return string_t();
    }
    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
    free(r);
    return out;
}

static text *TextFromString(string_t s) {
    text *txt = (text *)malloc(VARHDRSZ + s.GetSize());
    SET_VARSIZE(txt, VARHDRSZ + s.GetSize());
    memcpy(VARDATA(txt), s.GetData(), s.GetSize());
    return txt;
}

} 

// --- AsText ---
void SetFunctions::Set_as_text(DataChunk &args, ExpressionState &state, Vector &result) {
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

        Set *s = (Set *)malloc(size);
        memcpy(s, data, size);

        char *cstr = set_out(s, digits);
        auto str = StringVector::AddString(result, cstr);
        FlatVector::GetData<string_t>(result)[i] = str;

            free(s);
            free(cstr);
    }
}

// --- asBinary (WKB) ---
void SetFunctions::Set_as_binary(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t *>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            if (data_size < sizeof(Set)) {
                throw InvalidInputException("asBinary: invalid set value (size too small)");
            }
            uint8_t *data_copy = (uint8_t *)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Set *s = reinterpret_cast<Set *>(data_copy);
            size_t wkb_size = 0;
            uint8_t *wkb = set_as_wkb(s, WKB_EXTENDED, &wkb_size);
            free(data_copy);
            if (!wkb || wkb_size == 0) {
                if (wkb) {
                    free(wkb);
                }
                throw InternalException("asBinary: set_as_wkb failed");
            }
            string_t ret(reinterpret_cast<const char *>(wkb), wkb_size);
            string_t stored = StringVector::AddStringOrBlob(result, ret);
            free(wkb);
            return stored;
        }
    );
}

// --- asHexWKB ---
void SetFunctions::Set_as_hexwkb(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t *>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            if (data_size < sizeof(Set)) {
                throw InvalidInputException("asHexWKB: invalid set value (size too small)");
            }
            uint8_t *data_copy = (uint8_t *)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Set *s = reinterpret_cast<Set *>(data_copy);
            size_t hex_size = 0;
            char *hex = set_as_hexwkb(s, WKB_EXTENDED, &hex_size);
            (void)hex_size;
            free(data_copy);
            if (!hex) {
                throw InternalException("asHexWKB: set_as_hexwkb failed");
            }
            string_t stored = StringVector::AddString(result, hex);
            free(hex);
            return stored;
        }
    );
}


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
    meosType set_type = SetTypeMapping::GetMeosTypeFromAlias(target_type.GetAlias());

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

// --- Set constructor from list ---
void SetFunctions::Set_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_input = args.data[0];
    auto meos_type = SetTypeMapping::GetMeosTypeFromAlias(result.GetType().ToString());

    UnaryExecutor::Execute<list_entry_t, string_t>(
        list_input, result, args.size(),
        [&](list_entry_t list_entry) -> string_t {
            auto &child = ListVector::GetEntry(list_input);
            child.Flatten(args.size());  

            idx_t offset = list_entry.offset;
            idx_t length = list_entry.length;

            if (length == 0) {
                throw InvalidInputException("The input array cannot be empty");
            }

            Datum *values = (Datum *)malloc(sizeof(Datum) * length);

            for (idx_t i = 0; i < length; ++i) {
                idx_t idx = offset + i;

                switch (meos_type) {
                    case T_INTSET: {
                        int32_t v = FlatVector::GetData<int32_t>(child)[idx];
                        values[i] = Datum(v);
                        break;
                    }
                    case T_BIGINTSET: {
                        int64_t v = FlatVector::GetData<int64_t>(child)[idx];
                        values[i] = Datum(v);
                        break;
                    }
                    case T_FLOATSET: {
                        double v = FlatVector::GetData<double>(child)[idx];
                        values[i] = Float8GetDatum(v);
                        break;
                    }
                    case T_TEXTSET: {
                        string_t str = FlatVector::GetData<string_t>(child)[idx];
                        size_t len = str.GetSize();
                        const char *cstr = str.GetData();

                        text *txt = (text *)malloc(VARHDRSZ + len);
                        SET_VARSIZE(txt, VARHDRSZ + len);
                        memcpy(VARDATA(txt), cstr, len);

                        values[i] = (Datum)txt;                        
                        break;
                    }
                    case T_DATESET: {
                        date_t d = FlatVector::GetData<date_t>(child)[idx];
                        values[i] = Datum((int32_t)ToMeosDate(d));
                        break;
                    }
                    case T_TSTZSET: {
                        timestamp_t ts = FlatVector::GetData<timestamp_t>(child)[idx];
                        values[i] = Datum(ToMeosTimestamp(ts));
                        break;
                    }
                    default:
                        free(values);
                        throw InvalidInputException("Unsupported type in Set Constructor");
                }
            }

            meosType base_type = settype_basetype(meos_type);            
            Set *s = set_make_free(values, (int)length, base_type, true);
                        
            size_t size = set_mem_size(s);            
            string_t blob = StringVector::AddStringOrBlob(result, (const char*)s, size);
            
            free(s);            
            return blob;
        }
    );
}

// Conversion function: base type -> set 
static inline void Write_set(Vector &result, idx_t row, Set *s) {
    auto out = FlatVector::GetData<string_t>(result);
    out[row] = StringVector::AddStringOrBlob(result, (const char *)s, set_mem_size(s));
    free(s);
}

static inline void Value_to_set_core(Vector &source, Vector &result, idx_t count, meosType base_type) {
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);
    
    auto handle_null = [&](idx_t row) {
        FlatVector::SetNull(result, row, true);
    };

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
    meosType set_type  = SetTypeMapping::GetMeosTypeFromAlias(target_type.GetAlias());
    meosType base_type = settype_basetype(set_type);

    Value_to_set_core(source, result, count, base_type);
    return true;
}

// --- SCALAR function (base -> set) ----
void SetFunctions::Value_to_set(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &source = args.data[0];
    auto out_type = result.GetType();
    meosType set_type  = SetTypeMapping::GetMeosTypeFromAlias(out_type.GetAlias());
    meosType base_type = settype_basetype(set_type);

    Value_to_set_core(source, result, args.size(), base_type);
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


void SetFunctions::Intset_to_floatset(DataChunk &args, ExpressionState &state, Vector &result) {
    Intset_to_floatset_common(args.data[0], result, args.size());
}

void SetFunctions::Floatset_to_intset(DataChunk &args, ExpressionState &state, Vector &result) {
    Floatset_to_intset_common(args.data[0], result, args.size());
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

// --- SCALAR: dateset -> tstzset ---
void SetFunctions::Dateset_to_tstzset(DataChunk &args, ExpressionState &state, Vector &result) {
    Dateset_to_tstzset_common(args.data[0], result, args.size());
}

// --- SCALAR: tstzset -> dateset ---
void SetFunctions::Tstzset_to_dateset(DataChunk &args, ExpressionState &state, Vector &result) {
    Tstzset_to_dateset_common(args.data[0], result, args.size());
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

// --- memSize ---
void SetFunctions::Set_mem_size(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];

    UnaryExecutor::Execute<string_t, int32_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> int32_t {                                 
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            Set *s = (Set*)malloc(size);
            memcpy(s, data, size);            
            int mem_size = set_mem_size(s);  
            free(s);
            return mem_size;
        });
}

// --- set_hash / set_hash_extended ---
void SetFunctions::Set_hash(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    UnaryExecutor::Execute<string_t, uint32_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> uint32_t {
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            Set *s = (Set*)malloc(size);
            memcpy(s, data, size);
            uint32_t h = set_hash(s);
            free(s);
            return h;
        });
}

void SetFunctions::Set_hash_extended(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    auto &seed_vec = args.data[1];
    BinaryExecutor::Execute<string_t, int64_t, uint64_t>(
        input, seed_vec, result, args.size(),
        [&](string_t input_blob, int64_t seed) -> uint64_t {
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            Set *s = (Set*)malloc(size);
            memcpy(s, data, size);
            uint64_t h = set_hash_extended(s, (uint64_t)seed);
            free(s);
            return h;
        });
}

// --- numValue ---
void SetFunctions::Set_num_values(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];

    UnaryExecutor::Execute<string_t, int32_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> int32_t {
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            Set *s = (Set*)malloc(size);
            memcpy(s, data, size);
            int count = set_num_values(s);
            free(s);
            return count;
        });
}

// --- startValue ---
void SetFunctions::Set_start_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    auto set_type = SetTypeMapping::GetMeosTypeFromAlias(input.GetType().ToString());
    auto base_type = settype_basetype(set_type);

    switch (base_type) {
        case T_INT4:
            UnaryExecutor::Execute<string_t, int32_t>(
                input, result, args.size(),
                [&](string_t input_blob) -> int32_t {
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    Set *s = (Set*)malloc(size);
                    memcpy(s, data, size);
                    Datum d = set_start_value(s);
                    free(s);
                    return int32(d);
                });
            break;

        case T_INT8:
            UnaryExecutor::Execute<string_t, int64_t>(
                input, result, args.size(),
                [&](string_t input_blob) -> int64_t {
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    Set *s = (Set*)malloc(size);
                    memcpy(s, data, size);
                    Datum d = set_start_value(s);
                    free(s);
                    return int64(d);
                });
            break;

        case T_FLOAT8:
            UnaryExecutor::Execute<string_t, double>(
                input, result, args.size(),
                [&](string_t input_blob) -> double {
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    Set *s = (Set*)malloc(size);
                    memcpy(s, data, size);                    
                    Datum d = set_start_value(s);
                    free(s);
                    return DatumGetFloat8(d);
                });
            break;

        case T_TEXT:
            UnaryExecutor::Execute<string_t, string_t>(
                input, result, args.size(),
                [&](string_t input_blob) -> string_t {
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    Set *s = (Set*)malloc(size);
                    memcpy(s, data, size);                    
                    Datum d = set_start_value(s);
                    free(s);                    
                    text *txt = (text *)DatumGetPointer(d);
                    int len = VARSIZE(txt) - VARHDRSZ;
                    string str(VARDATA(txt), len);
                    return StringVector::AddString(result, str); 
                });
            break;

        case T_DATE:
            UnaryExecutor::Execute<string_t, date_t>(
                input, result, args.size(),
                [&](string_t input_blob) -> date_t {
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    Set *s = (Set*)malloc(size);
                    memcpy(s, data, size);                    
                    Datum d = set_start_value(s);
                    free(s);
                    return date_t(int32(FromMeosDate(d)));
                });
            break;

        case T_TIMESTAMPTZ:
            UnaryExecutor::Execute<string_t, timestamp_t>(
                input, result, args.size(),
                [&](string_t input_blob) -> timestamp_t {
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    Set *s = (Set*)malloc(size);
                    memcpy(s, data, size);                    
                    Datum d = set_start_value(s);
                    free(s);
                    int64_t tmp = int64(d);
                    return FromMeosTimestamp(tmp);
                });
            break;

        default:
            throw NotImplementedException("startValue: Unsupported set base type.");
    }
}


// --- endValue ---
void SetFunctions::Set_end_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    auto set_type = SetTypeMapping::GetMeosTypeFromAlias(input.GetType().ToString());
    auto base_type = settype_basetype(set_type);

    switch (base_type) {
        case T_INT4:
            UnaryExecutor::Execute<string_t, int32_t>(
                input, result, args.size(),
                [&](string_t input_blob) -> int32_t {
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    Set *s = (Set*)malloc(size);
                    memcpy(s, data, size);                    
                    Datum d = set_end_value(s);
                    free(s);
                    return int32(d);
                });
            break;

        case T_INT8:
            UnaryExecutor::Execute<string_t, int64_t>(
                input, result, args.size(),
                [&](string_t input_blob) -> int64_t {
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    Set *s = (Set*)malloc(size);
                    memcpy(s, data, size);                    
                    Datum d = set_end_value(s);
                    free(s);
                    return int64(d);
                });
            break;

        case T_FLOAT8:
            UnaryExecutor::Execute<string_t, double>(
                input, result, args.size(),
                [&](string_t input_blob) -> double {
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    Set *s = (Set*)malloc(size);
                    memcpy(s, data, size);                    
                    Datum d = set_end_value(s);
                    free(s);
                    return DatumGetFloat8(d);
                });
            break;

        case T_TEXT:
            UnaryExecutor::Execute<string_t, string_t>(
                input, result, args.size(),
                [&](string_t input_blob) -> string_t {
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    Set *s = (Set*)malloc(size);
                    memcpy(s, data, size);                    
                    Datum d = set_end_value(s);                    
                    free(s);
                    text *txt = (text *)DatumGetPointer(d);
                    int len = VARSIZE(txt) - VARHDRSZ;
                    string str(VARDATA(txt), len);
                    return StringVector::AddString(result, str);
                });
            break;

        case T_DATE:
            UnaryExecutor::Execute<string_t, date_t>(
                input, result, args.size(),
                [&](string_t input_blob) -> date_t {
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    Set *s = (Set*)malloc(size);
                    memcpy(s, data, size);                    
                    Datum d = set_end_value(s);
                    free(s);
                    return date_t(int32(FromMeosDate(d)));
                });
            break;

        case T_TIMESTAMPTZ:
            UnaryExecutor::Execute<string_t, timestamp_t>(
                input, result, args.size(),
                [&](string_t input_blob) -> timestamp_t {
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    Set *s = (Set*)malloc(size);
                    memcpy(s, data, size);                    
                    Datum d = set_end_value(s);
                    free(s);
                    int64_t tmp = int64(d);
                    return FromMeosTimestamp(tmp);
                });
            break;

        default:
            throw NotImplementedException("startValue: Unsupported set base type.");
    }
}

// --- valueN ---
void SetFunctions::Set_value_n(DataChunk &args, ExpressionState &state, Vector &result_vec) {
    auto &set_vec = args.data[0];
    auto &index_vec = args.data[1];

    const auto set_type = SetTypeMapping::GetMeosTypeFromAlias(set_vec.GetType().ToString());
    const auto base_type = settype_basetype(set_type);

    result_vec.SetVectorType(VectorType::FLAT_VECTOR);
    auto &validity = FlatVector::Validity(result_vec);

    for (idx_t i = 0; i < args.size(); i++) {
        validity.SetInvalid(i);

        if (set_vec.GetValue(i).IsNull() || index_vec.GetValue(i).IsNull())
            continue;
        
        int32_t index = FlatVector::GetData<int32_t>(index_vec)[i];
        auto blob = FlatVector::GetData<string_t>(set_vec)[i];
        const uint8_t *data = (const uint8_t *)blob.GetData();
        size_t size = blob.GetSize();        
        Set *s = (Set*)malloc(size);
        memcpy(s, data, size);

        if (!s) continue;

        Datum d;
        bool found = set_value_n(s, index, &d);
        free(s);

        if (!found) continue;
        
        switch (base_type) {
            case T_INT4:
                FlatVector::GetData<int32_t>(result_vec)[i] = int32(d);
                break;
            case T_INT8:
                FlatVector::GetData<int64_t>(result_vec)[i] = int64(d);
                break;
            case T_FLOAT8:
                FlatVector::GetData<double>(result_vec)[i] = DatumGetFloat8(d);
                break;
            case T_TEXT: {
                text *txt = (text *)DatumGetPointer(d);
                int len = VARSIZE(txt) - VARHDRSZ;
                string str(VARDATA(txt), len);
                FlatVector::GetData<string_t>(result_vec)[i] = StringVector::AddString(result_vec, str);
                break;
            }
            case T_DATE: {
                int32_t raw = int32(d);
                FlatVector::GetData<date_t>(result_vec)[i] = date_t(FromMeosDate(raw));
                break;
            }
            case T_TIMESTAMPTZ: {
                int64_t raw = int64(d);
                FlatVector::GetData<timestamp_t>(result_vec)[i] = timestamp_t(FromMeosTimestamp(raw));
                break;
            }
            default:
                throw NotImplementedException("valueN: unsupported set base type");
        }

        validity.SetValid(i);
    }
}

// --- getValues ---

void SetFunctions::Set_values(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    idx_t row_count = args.size();
    
    auto set_type_alias = SetTypeMapping::GetMeosTypeFromAlias(input.GetType().ToString());
    auto base_type = settype_basetype(set_type_alias); // MEOS base type enum
    auto child_type = SetTypeMapping::GetChildType(input.GetType()); // DuckDB LogicalType
    
    auto &result_validity = FlatVector::Validity(result);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &child_vector = ListVector::GetEntry(result);
    
    child_vector.SetVectorType(VectorType::FLAT_VECTOR);
    ListVector::Reserve(result, row_count); 
    idx_t total_offset = 0;

    for (idx_t i = 0; i < row_count; ++i) {
        if (input.GetValue(i).IsNull()) {
            result_validity.SetInvalid(i);
            continue;
        }

        string_t blob = FlatVector::GetData<string_t>(input)[i];
        const uint8_t *data = (const uint8_t *)(blob.GetData());
        size_t size = blob.GetSize();
        Set *s = (Set*)malloc(size);
        memcpy(s, data, size);
        if (!s) {
            result_validity.SetInvalid(i);
            continue;
        }

        uint64_t count = s->count;
        Datum *values = set_vals(s);
        
        ListVector::SetListSize(result, total_offset + count);
        list_entries[i] = list_entry_t{total_offset, count};
        
        switch (base_type) {
            case T_INT4: {
                auto data = FlatVector::GetData<int32_t>(child_vector);
                for (int j = 0; j < count; ++j) {
                    data[total_offset + j] = int32(values[j]);
                }
                break;
            }
            case T_INT8: {
                auto data = FlatVector::GetData<int64_t>(child_vector);
                for (int j = 0; j < count; ++j) {
                    data[total_offset + j] = int64(values[j]);
                }
                break;
            }
            case T_FLOAT8: {
                auto data = FlatVector::GetData<double>(child_vector);
                for (int j = 0; j < count; ++j) {
                    data[total_offset + j] = DatumGetFloat8(values[j]);
                }
                break;
            }
            case T_TEXT: {
                auto data = FlatVector::GetData<string_t>(child_vector);
                for (int j = 0; j < count; ++j) {
                    text *txt = (text *)DatumGetPointer(values[j]);
                    string str(VARDATA(txt), VARSIZE(txt) - VARHDRSZ);
                    data[total_offset + j] = StringVector::AddString(child_vector, str);
                }
                break;
            }
            case T_DATE: {
                auto data = FlatVector::GetData<date_t>(child_vector);
                for (int j = 0; j < count; ++j) {
                    data[total_offset + j] = date_t(FromMeosDate(int32(values[j])));
                }
                break;
            }
            case T_TIMESTAMPTZ: {
                auto data = FlatVector::GetData<timestamp_t>(child_vector);
                for (int j = 0; j < count; ++j) {
                    data[total_offset + j] = timestamp_t(FromMeosTimestamp(int64(values[j])));
                }
                break;
            }
            default:
                free(values);
                free(s);
                throw NotImplementedException("Unsupported base type in getValues");
        }

        total_offset += count;
        result_validity.SetValid(i);
        free(values);
        free(s);
    }
}

// --- shift ---
void SetFunctions::Numset_shift(DataChunk &args, ExpressionState &state, Vector &result) {    
    auto &set_vec  = args.data[0];
    auto out_type  = result.GetType();    
    meosType set_type  = SetTypeMapping::GetMeosTypeFromAlias(out_type.GetAlias());    

    switch (set_type) {
        case T_INTSET: { // shift(intset, integer) -> intset
            BinaryExecutor::Execute<string_t, int32_t, string_t>(
                set_vec, args.data[1], result, args.size(),
                [&](string_t blob, int32_t shift) -> string_t {
                    const Set *s = (const Set *)blob.GetDataUnsafe();
                    Set *r = numset_shift_scale(s, Datum(shift), 0, /*do_shift=*/true, /*do_scale=*/false);
                    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
                    free(r);
                    return out;                    
                });
            break;
        }
        case T_BIGINTSET: { // shift(bigintset, bigint) -> bigintset
            BinaryExecutor::Execute<string_t, int64_t, string_t>(
                set_vec, args.data[1], result, args.size(),
                [&](string_t blob, int64_t shift) -> string_t {
                    const Set *s = (const Set *)blob.GetDataUnsafe();
                    Set *r = numset_shift_scale(s, Datum(shift), 0, /*do_shift=*/true, /*do_scale=*/false);
                    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
                    free(r);
                    return out;
                });
            break;
        }
        case T_FLOATSET: { // shift(floatset, float) -> floatset
            BinaryExecutor::Execute<string_t, double_t, string_t>(
                set_vec, args.data[1], result, args.size(),
                [&](string_t blob, double shift) -> string_t {                    
                    const Set *s = (const Set *)blob.GetDataUnsafe();
                    Set *r = numset_shift_scale(s, Float8GetDatum(shift), 0, /*do_shift=*/true, /*do_scale=*/false);
                    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
                    free(r);
                    return out;
                });
            break;
        }
        case T_DATESET: { // shift(dateset, integer) -> dateset
            BinaryExecutor::Execute<string_t, int32_t, string_t>(
                set_vec, args.data[1], result, args.size(),
                [&](string_t blob, int32_t shift) -> string_t {
                    const Set *s = (const Set *)blob.GetDataUnsafe();
                    Set *r = numset_shift_scale(s, Datum(shift), 0, /*do_shift=*/true, /*do_scale=*/false);
                    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
                    free(r);
                    return out;
                });
            break;
        }
        default:
            throw NotImplementedException("shift(<set>): unsupported base type");
    }
}

// --- tstzset shift ---
void SetFunctions::Tstzset_shift(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &set_vec = args.data[0];
    auto &iv_vec  = args.data[1];

    BinaryExecutor::Execute<string_t, interval_t, string_t>(
        set_vec, iv_vec, result, args.size(),
        [&](string_t blob, const interval_t &iv) -> string_t {
            const Set *s = (const Set *)blob.GetDataUnsafe();            
            MeosInterval meos_iv = IntervaltToInterval(iv);
            Set *r = tstzset_shift_scale(s, &meos_iv, NULL);
            return StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));             
        }
    );
}
// --- Scale ---
void SetFunctions::Numset_scale(DataChunk &args, ExpressionState &state, Vector &result){
    auto &set_vec  = args.data[0];
    auto out_type  = result.GetType();    
    meosType set_type  = SetTypeMapping::GetMeosTypeFromAlias(out_type.GetAlias());    

    switch (set_type) {
        case T_INTSET: { // scale(intset, integer) -> intset
            BinaryExecutor::Execute<string_t, int32_t, string_t>(
                set_vec, args.data[1], result, args.size(),
                [&](string_t blob, int32_t width) -> string_t {
                    const Set *s = (const Set *)blob.GetDataUnsafe();
                    Set *r = numset_shift_scale(s, 0, Datum(width), false, true);
                    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
                    free(r);
                    return out;                    
                });
            break;
        }
        case T_BIGINTSET: { // scale(bigintset, integer) -> bigintset
            BinaryExecutor::Execute<string_t, int64_t, string_t>(
                set_vec, args.data[1], result, args.size(),
                [&](string_t blob, int64_t width) -> string_t {
                    const Set *s = (const Set *)blob.GetDataUnsafe();
                    Set *r = numset_shift_scale(s, 0, Datum(width), false, true);
                    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
                    free(r);
                    return out;
                });
            break;
        }
        case T_FLOATSET: { // scale(floatset, integer) -> floatset
            BinaryExecutor::Execute<string_t, int32_t, string_t>(
                set_vec, args.data[1], result, args.size(),
                [&](string_t blob, double width) -> string_t {                    
                    const Set *s = (const Set *)blob.GetDataUnsafe();
                    Set *r = numset_shift_scale(s, 0, Float8GetDatum(width), false, true);
                    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
                    free(r);
                    return out;
                });
            break;
        }
        case T_DATESET: { // scale(dateset, integer) -> dateset
            BinaryExecutor::Execute<string_t, int32_t, string_t>(
                set_vec, args.data[1], result, args.size(),
                [&](string_t blob, int32_t width) -> string_t {
                    const Set *s = (const Set *)blob.GetDataUnsafe();
                    Set *r = numset_shift_scale(s, 0, Datum(width), false, true);
                    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
                    free(r);
                    return out;
                });
            break;
        }
        default:
            throw NotImplementedException("scale(<set>): unsupported base type");
    }
}

// --- tstzset scale ---
void SetFunctions::Tstzset_scale(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &set_vec = args.data[0];
    auto &iv_vec  = args.data[1];

    BinaryExecutor::Execute<string_t, interval_t, string_t>(
        set_vec, iv_vec, result, args.size(),
        [&](string_t blob, const interval_t &iv) -> string_t {
            const Set *s = (const Set *)blob.GetDataUnsafe();            
            MeosInterval meos_iv = IntervaltToInterval(iv);
            Set *r = tstzset_shift_scale(s, NULL, &meos_iv);
            return StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));             
        }
    );
}
// --- Shift Scale ---
void SetFunctions::Numset_shift_scale(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &set_vec = args.data[0];
    auto &sh_vec  = args.data[1];
    auto &wd_vec  = args.data[2];

    auto out_type  = result.GetType();
    meosType set_type = SetTypeMapping::GetMeosTypeFromAlias(out_type.GetAlias());

    switch (set_type) {
        case T_INTSET: { // shift_scale(intset, integer, integer) -> intset
            TernaryExecutor::Execute<string_t, int32_t, int32_t, string_t>(
                set_vec, sh_vec, wd_vec, result, args.size(),
                [&](string_t blob, int32_t shift, int32_t width) -> string_t {
                    const Set *s = (const Set *)blob.GetDataUnsafe();
                    Set *r = numset_shift_scale(s, Datum(shift), Datum(width), true, true);
                    auto out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
                    free(r);
                    return out;
                });
            break;
        }

        case T_BIGINTSET: { // shift_scale(bigintset, bigint, bigint) -> bigintset
            TernaryExecutor::Execute<string_t, int64_t, int64_t, string_t>(
                set_vec, sh_vec, wd_vec, result, args.size(),
                [&](string_t blob, int64_t shift, int64_t width) -> string_t {
                    const Set *s = (const Set *)blob.GetDataUnsafe();
                    Set *r = numset_shift_scale(s, Datum(shift),Datum(width), true, true);
                    auto out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
                    free(r);
                    return out;
                });
            break;
        }

        case T_FLOATSET: { // shift_scale(floatset, double, double) -> floatset
            TernaryExecutor::Execute<string_t, double, double, string_t>(
                set_vec, sh_vec, wd_vec, result, args.size(),
                [&](string_t blob, double shift, double width) -> string_t {
                    const Set *s = (const Set *)blob.GetDataUnsafe();
                    Set *r = numset_shift_scale(s, Float8GetDatum(shift), Float8GetDatum(width), true, true);
                    auto out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
                    free(r);
                    return out;
                });
            break;
        }

        case T_DATESET: { // shift_scale(dateset, integer(days), integer(days)) -> dateset
            TernaryExecutor::Execute<string_t, int32_t, int32_t, string_t>(
                set_vec, sh_vec, wd_vec, result, args.size(),
                [&](string_t blob, int32_t shift_days, int32_t width_days) -> string_t {
                    const Set *s = (const Set *)blob.GetDataUnsafe();
                    Set *r = numset_shift_scale(s,Datum(shift_days), Datum(width_days), true, true);
                    auto out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
                    free(r);
                    return out;
                });
            break;
        }

        default:
            throw NotImplementedException("shift_scale(<numset>): unsupported base type");
    }
}
void SetFunctions::Tstzset_shift_scale(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &set_vec = args.data[0];
    auto &shift_vec = args.data[1];
    auto &duration_vec = args.data[2];

    TernaryExecutor::Execute<string_t, interval_t, interval_t, string_t>(
        set_vec, shift_vec, duration_vec, result, args.size(),
        [&](string_t blob, const interval_t &shift, const interval_t &duration) -> string_t {
            const Set *s = (const Set *)blob.GetDataUnsafe();            
            MeosInterval meos_shift = IntervaltToInterval(shift);
            MeosInterval meos_duration = IntervaltToInterval(duration);
            Set *r = tstzset_shift_scale(s, &meos_shift, &meos_duration);
            return StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));             
        }
    );
}

// --- Floor (floatset) ---
void SetFunctions::Floatset_floor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            Set *s = (Set*)malloc(size);
            memcpy(s, data, size);
            Set *r = floatset_floor(s);
            free(s);
            string_t out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
            free(r);
            return out;            
        });
}

// --- Ceil (floatset) ---
void SetFunctions::Floatset_ceil(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            Set *s = (Set*)malloc(size);
            memcpy(s, data, size);
            Set *r = floatset_ceil(s);
            free(s);
            string_t out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
            free(r);
            return out;            
        });
}

// --- Round (floatset) ---
void SetFunctions::Floatset_round(DataChunk &args, ExpressionState &state, Vector &result) {
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
        int digits = has_digits ? FlatVector::GetData<int32_t>(*digits_vec_ptr)[i] : 0;

        const uint8_t *data = (const uint8_t *)blob.GetData();
        size_t size = blob.GetSize();

        Set *s = (Set *)malloc(size);
        memcpy(s, data, size);
        Set *r = set_round(s, digits);
        free(s);
        string_t str = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
        FlatVector::GetData<string_t>(result)[i] = str;
        free(r);
    }
}

// --- Degrees (floatset) ---
void SetFunctions::Floatset_degrees(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input_vec = args.data[0];
    input_vec.Flatten(args.size());

    bool has_bools = args.ColumnCount() > 1;
    Vector *bools_vec_ptr = has_bools ? &args.data[1] : nullptr;
    if (has_bools) bools_vec_ptr->Flatten(args.size());

    for (idx_t i = 0; i < args.size(); i++) {
        if (FlatVector::IsNull(input_vec, i) || (has_bools && FlatVector::IsNull(*bools_vec_ptr, i))) {
            FlatVector::SetNull(result, i, true);
            continue;
        }

        auto blob = FlatVector::GetData<string_t>(input_vec)[i];
        // Registered in set.cpp as a BOOLEAN argument; DuckDB 1.4 asserts the
        // Vector's template type matches the declared type, so reading this as
        // `int32_t` triggered `Expected INT32, found BOOL` (set.test:227).
        bool bools = has_bools ? FlatVector::GetData<bool>(*bools_vec_ptr)[i] : false;

        const uint8_t *data = (const uint8_t *)blob.GetData();
        size_t size = blob.GetSize();

        Set *s = (Set *)malloc(size);
        memcpy(s, data, size);
        Set *r = floatset_degrees(s, bools);
        free(s);
        string_t str = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
        FlatVector::GetData<string_t>(result)[i] = str;
        free(r);
    }
}

// --- Radians (floatset) ---
void SetFunctions::Floatset_radians(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input_vec = args.data[0];
    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            Set *s = (Set*)malloc(size);
            memcpy(s, data, size);
            Set *r = floatset_radians(s);
            free(s);
            string_t out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
            free(r);
            return out;            
        });
    }

// --- Textset lower ---
void SetFunctions::Textset_lower(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            Set *s = (Set*)malloc(size);
            memcpy(s, data, size);
            Set *r = textset_lower(s);
            free(s);
            string_t out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
            free(r);
            return out;            
        });
}

// --- Textset upper ---
void SetFunctions::Textset_upper(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            Set *s = (Set*)malloc(size);
            memcpy(s, data, size);
            Set *r = textset_upper(s);
            free(s);
            string_t out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
            free(r);
            return out;            
        });
}

// --- Textset initcap ---
void SetFunctions::Textset_initcap(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            Set *s = (Set*)malloc(size);
            memcpy(s, data, size);
            Set *r = textset_initcap(s);
            free(s);
            string_t out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
            free(r);
            return out;            
        });
}

// --- textset_cat: text || textset (MEOS textcat_text_textset) ---
void SetFunctions::Textcat_text_textset(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t txt_str, string_t set_blob, ValidityMask &mask, idx_t idx) -> string_t {
            text *txt = (text *)malloc(VARHDRSZ + txt_str.GetSize());
            SET_VARSIZE(txt, VARHDRSZ + txt_str.GetSize());
            memcpy(VARDATA(txt), txt_str.GetData(), txt_str.GetSize());

            Set *s = (Set *)malloc(set_blob.GetSize());
            memcpy(s, set_blob.GetData(), set_blob.GetSize());

            Set *r = textcat_text_textset(txt, s);
            free(txt);
            free(s);
            if (!r) {
                mask.SetInvalid(idx);
                return string_t();
            }
            string_t out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
            free(r);
            return out;
        }
    );
}

// --- textset_cat: textset || text (MEOS textcat_textset_text) ---
void SetFunctions::Textcat_textset_text(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t set_blob, string_t txt_str, ValidityMask &mask, idx_t idx) -> string_t {
            Set *s = (Set *)malloc(set_blob.GetSize());
            memcpy(s, set_blob.GetData(), set_blob.GetSize());

            text *txt = (text *)malloc(VARHDRSZ + txt_str.GetSize());
            SET_VARSIZE(txt, VARHDRSZ + txt_str.GetSize());
            memcpy(VARDATA(txt), txt_str.GetData(), txt_str.GetSize());

            Set *r = textcat_textset_text(s, txt);
            free(txt);
            free(s);
            if (!r) {
                mask.SetInvalid(idx);
                return string_t();
            }
            string_t out = StringVector::AddStringOrBlob(result, (const char *)r, set_mem_size(r));
            free(r);
            return out;
        }
    );
}

// --- set_contains (set @> value) ---
void SetFunctions::Contains_set_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &set_vec = args.data[0];
    auto &val_vec = args.data[1];
    idx_t count = args.size();
    set_vec.Flatten(count);
    val_vec.Flatten(count);

    switch (val_vec.GetType().id()) {
    case LogicalTypeId::INTEGER:
        BinaryExecutor::ExecuteWithNulls<string_t, int32_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t set_blob, int32_t v, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(set_blob);
                bool r = contains_set_int(s, v);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::BIGINT:
        BinaryExecutor::ExecuteWithNulls<string_t, int64_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t set_blob, int64_t v, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(set_blob);
                bool r = contains_set_bigint(s, v);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::DOUBLE:
        BinaryExecutor::ExecuteWithNulls<string_t, double, bool>(
            set_vec, val_vec, result, count,
            [&](string_t set_blob, double v, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(set_blob);
                bool r = contains_set_float(s, v);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::VARCHAR:
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t set_blob, string_t val_str, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(set_blob);
                text *txt = TextFromString(val_str);
                bool r = contains_set_text(s, txt);
                free(s);
                free(txt);
                return r;
            });
        break;
    case LogicalTypeId::DATE:
        BinaryExecutor::ExecuteWithNulls<string_t, date_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t set_blob, date_t d, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(set_blob);
                bool r = contains_set_date(s, ToMeosDate(d));
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::TIMESTAMP_TZ:
        BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t set_blob, timestamp_tz_t ts, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(set_blob);
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts);
                bool r = contains_set_timestamptz(s, (TimestampTz)ts_meos.value);
                free(s);
                return r;
            });
        break;
    default:
        throw InternalException("Contains_set_value: unsupported value type");
    }
}

void SetFunctions::Contains_set_set(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> bool {
            Set *s1 = CopySet(a);
            Set *s2 = CopySet(b);
            bool r = contains_set_set(s1, s2);
            free(s1);
            free(s2);
            return r;
        });
}

void SetFunctions::Contained_value_set(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &val_vec = args.data[0];
    auto &set_vec = args.data[1];
    idx_t count = args.size();
    val_vec.Flatten(count);
    set_vec.Flatten(count);

    switch (val_vec.GetType().id()) {
    case LogicalTypeId::INTEGER:
        BinaryExecutor::ExecuteWithNulls<int32_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](int32_t v, string_t set_blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(set_blob);
                bool r = contained_int_set(v, s);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::BIGINT:
        BinaryExecutor::ExecuteWithNulls<int64_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](int64_t v, string_t set_blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(set_blob);
                bool r = contained_bigint_set(v, s);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::DOUBLE:
        BinaryExecutor::ExecuteWithNulls<double, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](double v, string_t set_blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(set_blob);
                bool r = contained_float_set(v, s);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::VARCHAR:
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](string_t val_str, string_t set_blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(set_blob);
                text *txt = TextFromString(val_str);
                bool r = contained_text_set(txt, s);
                free(s);
                free(txt);
                return r;
            });
        break;
    case LogicalTypeId::DATE:
        BinaryExecutor::ExecuteWithNulls<date_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](date_t d, string_t set_blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(set_blob);
                bool r = contained_date_set(ToMeosDate(d), s);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::TIMESTAMP_TZ:
        BinaryExecutor::ExecuteWithNulls<timestamp_tz_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](timestamp_tz_t ts, string_t set_blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(set_blob);
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts);
                bool r = contained_timestamptz_set((TimestampTz)ts_meos.value, s);
                free(s);
                return r;
            });
        break;
    default:
        throw InternalException("Contained_value_set: unsupported value type");
    }
}

void SetFunctions::Contained_set_set(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> bool {
            Set *s1 = CopySet(a);
            Set *s2 = CopySet(b);
            bool r = contained_set_set(s1, s2);
            free(s1);
            free(s2);
            return r;
        });
}

void SetFunctions::Overlaps_set_set(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> bool {
            Set *s1 = CopySet(a);
            Set *s2 = CopySet(b);
            bool r = overlaps_set_set(s1, s2);
            free(s1);
            free(s2);
            return r;
        });
}

// --- Union: set + set ---
void SetFunctions::Union_set_set(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t set1_str, string_t set2_str, ValidityMask &mask, idx_t idx) -> string_t {
            Set *set1 = CopySet(set1_str);
            Set *set2 = CopySet(set2_str);
            Set *ret = union_set_set(set1, set2);
            free(set1);
            free(set2);
            if (!ret) {
                mask.SetInvalid(idx);
                return string_t();
            }
            string_t out = StringVector::AddStringOrBlob(result, (const char *)ret, set_mem_size(ret));
            free(ret);
            return out;
        });
}


// ---------- Left (<<): value left of set ----------
void SetFunctions::Left_value_set(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &val_vec = args.data[0];
    auto &set_vec = args.data[1];
    idx_t count = args.size();
    val_vec.Flatten(count);
    set_vec.Flatten(count);
    switch (val_vec.GetType().id()) {
    case LogicalTypeId::INTEGER:
        BinaryExecutor::ExecuteWithNulls<int32_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](int32_t v, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = left_int_set(v, s);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::BIGINT:
        BinaryExecutor::ExecuteWithNulls<int64_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](int64_t v, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = left_bigint_set(v, s);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::DOUBLE:
        BinaryExecutor::ExecuteWithNulls<double, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](double v, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = left_float_set(v, s);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::VARCHAR:
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](string_t val_str, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                text *txt = TextFromString(val_str);
                bool r = left_text_set(txt, s);
                free(s);
                free(txt);
                return r;
            });
        break;
    case LogicalTypeId::DATE:
        BinaryExecutor::ExecuteWithNulls<date_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](date_t d, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = before_date_set(ToMeosDate(d), s);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::TIMESTAMP_TZ:
        BinaryExecutor::ExecuteWithNulls<timestamp_tz_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](timestamp_tz_t ts, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts);
                bool r = before_timestamptz_set((TimestampTz)ts_meos.value, s);
                free(s);
                return r;
            });
        break;
    default:
        throw InternalException("Left_value_set: unsupported value type");
    }
}

void SetFunctions::Left_set_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &set_vec = args.data[0];
    auto &val_vec = args.data[1];
    idx_t count = args.size();
    set_vec.Flatten(count);
    val_vec.Flatten(count);
    switch (val_vec.GetType().id()) {
    case LogicalTypeId::INTEGER:
        BinaryExecutor::ExecuteWithNulls<string_t, int32_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, int32_t v, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = left_set_int(s, v);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::BIGINT:
        BinaryExecutor::ExecuteWithNulls<string_t, int64_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, int64_t v, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = left_set_bigint(s, v);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::DOUBLE:
        BinaryExecutor::ExecuteWithNulls<string_t, double, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, double v, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = left_set_float(s, v);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::VARCHAR:
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, string_t val_str, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                text *txt = TextFromString(val_str);
                bool r = left_set_text(s, txt);
                free(s);
                free(txt);
                return r;
            });
        break;
    case LogicalTypeId::DATE:
        BinaryExecutor::ExecuteWithNulls<string_t, date_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, date_t d, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = before_set_date(s, ToMeosDate(d));
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::TIMESTAMP_TZ:
        BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, timestamp_tz_t ts, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts);
                bool r = before_set_timestamptz(s, (TimestampTz)ts_meos.value);
                free(s);
                return r;
            });
        break;
    default:
        throw InternalException("Left_set_value: unsupported value type");
    }
}

void SetFunctions::Left_set_set(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> bool {
            Set *s1 = CopySet(a);
            Set *s2 = CopySet(b);
            bool r = left_set_set(s1, s2);
            free(s1);
            free(s2);
            return r;
        });
}

// ---------- Right (>>) ----------
void SetFunctions::Right_value_set(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &val_vec = args.data[0];
    auto &set_vec = args.data[1];
    idx_t count = args.size();
    val_vec.Flatten(count);
    set_vec.Flatten(count);
    switch (val_vec.GetType().id()) {
    case LogicalTypeId::INTEGER:
        BinaryExecutor::ExecuteWithNulls<int32_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](int32_t v, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = right_int_set(v, s);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::BIGINT:
        BinaryExecutor::ExecuteWithNulls<int64_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](int64_t v, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = right_bigint_set(v, s);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::DOUBLE:
        BinaryExecutor::ExecuteWithNulls<double, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](double v, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = right_float_set(v, s);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::VARCHAR:
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](string_t val_str, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                text *txt = TextFromString(val_str);
                bool r = right_text_set(txt, s);
                free(s);
                free(txt);
                return r;
            });
        break;
    case LogicalTypeId::DATE:
        BinaryExecutor::ExecuteWithNulls<date_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](date_t d, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = after_date_set(ToMeosDate(d), s);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::TIMESTAMP_TZ:
        BinaryExecutor::ExecuteWithNulls<timestamp_tz_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](timestamp_tz_t ts, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts);
                bool r = after_timestamptz_set((TimestampTz)ts_meos.value, s);
                free(s);
                return r;
            });
        break;
    default:
        throw InternalException("Right_value_set: unsupported value type");
    }
}

void SetFunctions::Right_set_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &set_vec = args.data[0];
    auto &val_vec = args.data[1];
    idx_t count = args.size();
    set_vec.Flatten(count);
    val_vec.Flatten(count);
    switch (val_vec.GetType().id()) {
    case LogicalTypeId::INTEGER:
        BinaryExecutor::ExecuteWithNulls<string_t, int32_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, int32_t v, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = right_set_int(s, v);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::BIGINT:
        BinaryExecutor::ExecuteWithNulls<string_t, int64_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, int64_t v, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = right_set_bigint(s, v);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::DOUBLE:
        BinaryExecutor::ExecuteWithNulls<string_t, double, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, double v, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = right_set_float(s, v);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::VARCHAR:
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, string_t val_str, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                text *txt = TextFromString(val_str);
                bool r = right_set_text(s, txt);
                free(s);
                free(txt);
                return r;
            });
        break;
    case LogicalTypeId::DATE:
        BinaryExecutor::ExecuteWithNulls<string_t, date_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, date_t d, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = after_set_date(s, ToMeosDate(d));
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::TIMESTAMP_TZ:
        BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, timestamp_tz_t ts, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts);
                bool r = after_set_timestamptz(s, (TimestampTz)ts_meos.value);
                free(s);
                return r;
            });
        break;
    default:
        throw InternalException("Right_set_value: unsupported value type");
    }
}

void SetFunctions::Right_set_set(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> bool {
            Set *s1 = CopySet(a);
            Set *s2 = CopySet(b);
            bool r = right_set_set(s1, s2);
            free(s1);
            free(s2);
            return r;
        });
}

// ---------- Overleft (&<) ----------
void SetFunctions::Overleft_value_set(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &val_vec = args.data[0];
    auto &set_vec = args.data[1];
    idx_t count = args.size();
    val_vec.Flatten(count);
    set_vec.Flatten(count);
    switch (val_vec.GetType().id()) {
    case LogicalTypeId::INTEGER:
        BinaryExecutor::ExecuteWithNulls<int32_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](int32_t v, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = overleft_int_set(v, s);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::BIGINT:
        BinaryExecutor::ExecuteWithNulls<int64_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](int64_t v, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = overleft_bigint_set(v, s);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::DOUBLE:
        BinaryExecutor::ExecuteWithNulls<double, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](double v, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = overleft_float_set(v, s);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::VARCHAR:
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](string_t val_str, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                text *txt = TextFromString(val_str);
                bool r = overleft_text_set(txt, s);
                free(s);
                free(txt);
                return r;
            });
        break;
    case LogicalTypeId::DATE:
        BinaryExecutor::ExecuteWithNulls<date_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](date_t d, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = overbefore_date_set(ToMeosDate(d), s);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::TIMESTAMP_TZ:
        BinaryExecutor::ExecuteWithNulls<timestamp_tz_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](timestamp_tz_t ts, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts);
                bool r = overbefore_timestamptz_set((TimestampTz)ts_meos.value, s);
                free(s);
                return r;
            });
        break;
    default:
        throw InternalException("Overleft_value_set: unsupported value type");
    }
}

void SetFunctions::Overleft_set_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &set_vec = args.data[0];
    auto &val_vec = args.data[1];
    idx_t count = args.size();
    set_vec.Flatten(count);
    val_vec.Flatten(count);
    switch (val_vec.GetType().id()) {
    case LogicalTypeId::INTEGER:
        BinaryExecutor::ExecuteWithNulls<string_t, int32_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, int32_t v, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = overleft_set_int(s, v);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::BIGINT:
        BinaryExecutor::ExecuteWithNulls<string_t, int64_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, int64_t v, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = overleft_set_bigint(s, v);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::DOUBLE:
        BinaryExecutor::ExecuteWithNulls<string_t, double, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, double v, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = overleft_set_float(s, v);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::VARCHAR:
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, string_t val_str, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                text *txt = TextFromString(val_str);
                bool r = overleft_set_text(s, txt);
                free(s);
                free(txt);
                return r;
            });
        break;
    case LogicalTypeId::DATE:
        BinaryExecutor::ExecuteWithNulls<string_t, date_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, date_t d, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = overbefore_set_date(s, ToMeosDate(d));
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::TIMESTAMP_TZ:
        BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, timestamp_tz_t ts, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts);
                bool r = overbefore_set_timestamptz(s, (TimestampTz)ts_meos.value);
                free(s);
                return r;
            });
        break;
    default:
        throw InternalException("Overleft_set_value: unsupported value type");
    }
}

void SetFunctions::Overleft_set_set(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> bool {
            Set *s1 = CopySet(a);
            Set *s2 = CopySet(b);
            bool r = overleft_set_set(s1, s2);
            free(s1);
            free(s2);
            return r;
        });
}

// ---------- Overright (&>) ----------
void SetFunctions::Overright_value_set(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &val_vec = args.data[0];
    auto &set_vec = args.data[1];
    idx_t count = args.size();
    val_vec.Flatten(count);
    set_vec.Flatten(count);
    switch (val_vec.GetType().id()) {
    case LogicalTypeId::INTEGER:
        BinaryExecutor::ExecuteWithNulls<int32_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](int32_t v, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = overright_int_set(v, s);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::BIGINT:
        BinaryExecutor::ExecuteWithNulls<int64_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](int64_t v, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = overright_bigint_set(v, s);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::DOUBLE:
        BinaryExecutor::ExecuteWithNulls<double, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](double v, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = overright_float_set(v, s);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::VARCHAR:
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](string_t val_str, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                text *txt = TextFromString(val_str);
                bool r = overright_text_set(txt, s);
                free(s);
                free(txt);
                return r;
            });
        break;
    case LogicalTypeId::DATE:
        BinaryExecutor::ExecuteWithNulls<date_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](date_t d, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = overafter_date_set(ToMeosDate(d), s);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::TIMESTAMP_TZ:
        BinaryExecutor::ExecuteWithNulls<timestamp_tz_t, string_t, bool>(
            val_vec, set_vec, result, count,
            [&](timestamp_tz_t ts, string_t blob, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts);
                bool r = overafter_timestamptz_set((TimestampTz)ts_meos.value, s);
                free(s);
                return r;
            });
        break;
    default:
        throw InternalException("Overright_value_set: unsupported value type");
    }
}

void SetFunctions::Overright_set_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &set_vec = args.data[0];
    auto &val_vec = args.data[1];
    idx_t count = args.size();
    set_vec.Flatten(count);
    val_vec.Flatten(count);
    switch (val_vec.GetType().id()) {
    case LogicalTypeId::INTEGER:
        BinaryExecutor::ExecuteWithNulls<string_t, int32_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, int32_t v, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = overright_set_int(s, v);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::BIGINT:
        BinaryExecutor::ExecuteWithNulls<string_t, int64_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, int64_t v, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = overright_set_bigint(s, v);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::DOUBLE:
        BinaryExecutor::ExecuteWithNulls<string_t, double, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, double v, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = overright_set_float(s, v);
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::VARCHAR:
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, string_t val_str, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                text *txt = TextFromString(val_str);
                bool r = overright_set_text(s, txt);
                free(s);
                free(txt);
                return r;
            });
        break;
    case LogicalTypeId::DATE:
        BinaryExecutor::ExecuteWithNulls<string_t, date_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, date_t d, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                bool r = overafter_set_date(s, ToMeosDate(d));
                free(s);
                return r;
            });
        break;
    case LogicalTypeId::TIMESTAMP_TZ:
        BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, bool>(
            set_vec, val_vec, result, count,
            [&](string_t blob, timestamp_tz_t ts, ValidityMask &mask, idx_t idx) -> bool {
                Set *s = CopySet(blob);
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts);
                bool r = overafter_set_timestamptz(s, (TimestampTz)ts_meos.value);
                free(s);
                return r;
            });
        break;
    default:
        throw InternalException("Overright_set_value: unsupported value type");
    }
}

void SetFunctions::Overright_set_set(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> bool {
            Set *s1 = CopySet(a);
            Set *s2 = CopySet(b);
            bool r = overright_set_set(s1, s2);
            free(s1);
            free(s2);
            return r;
        });
}

// ---------- Union (+): set + value ----------
void SetFunctions::Union_set_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &set_vec = args.data[0];
    auto &val_vec = args.data[1];
    idx_t count = args.size();
    set_vec.Flatten(count);
    val_vec.Flatten(count);
    switch (val_vec.GetType().id()) {
    case LogicalTypeId::INTEGER:
        BinaryExecutor::ExecuteWithNulls<string_t, int32_t, string_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, int32_t v, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = union_set_int(s, v);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::BIGINT:
        BinaryExecutor::ExecuteWithNulls<string_t, int64_t, string_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, int64_t v, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = union_set_bigint(s, v);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::DOUBLE:
        BinaryExecutor::ExecuteWithNulls<string_t, double, string_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, double v, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = union_set_float(s, v);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::VARCHAR:
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, string_t val_str, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                text *txt = TextFromString(val_str);
                Set *r = union_set_text(s, txt);
                free(txt);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::DATE:
        BinaryExecutor::ExecuteWithNulls<string_t, date_t, string_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, date_t d, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = union_set_date(s, ToMeosDate(d));
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::TIMESTAMP_TZ:
        BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, string_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, timestamp_tz_t ts, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts);
                Set *r = union_set_timestamptz(s, (TimestampTz)ts_meos.value);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    default:
        throw InternalException("Union_set_value: unsupported value type");
    }
}

// ---------- Union: value + set ----------
void SetFunctions::Union_value_set(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &val_vec = args.data[0];
    auto &set_vec = args.data[1];
    idx_t count = args.size();
    val_vec.Flatten(count);
    set_vec.Flatten(count);
    switch (val_vec.GetType().id()) {
    case LogicalTypeId::INTEGER:
        BinaryExecutor::ExecuteWithNulls<int32_t, string_t, string_t>(
            val_vec, set_vec, result, count,
            [&](int32_t v, string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = union_int_set(v, s);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::BIGINT:
        BinaryExecutor::ExecuteWithNulls<int64_t, string_t, string_t>(
            val_vec, set_vec, result, count,
            [&](int64_t v, string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = union_bigint_set(v, s);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::DOUBLE:
        BinaryExecutor::ExecuteWithNulls<double, string_t, string_t>(
            val_vec, set_vec, result, count,
            [&](double v, string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = union_float_set(v, s);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::VARCHAR:
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
            val_vec, set_vec, result, count,
            [&](string_t val_str, string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                text *txt = TextFromString(val_str);
                Set *r = union_text_set(txt, s);
                free(txt);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::DATE:
        BinaryExecutor::ExecuteWithNulls<date_t, string_t, string_t>(
            val_vec, set_vec, result, count,
            [&](date_t d, string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = union_date_set(ToMeosDate(d), s);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::TIMESTAMP_TZ:
        BinaryExecutor::ExecuteWithNulls<timestamp_tz_t, string_t, string_t>(
            val_vec, set_vec, result, count,
            [&](timestamp_tz_t ts, string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts);
                Set *r = union_timestamptz_set((TimestampTz)ts_meos.value, s);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    default:
        throw InternalException("Union_value_set: unsupported value type");
    }
}

// ---------- Minus (-) ----------
void SetFunctions::Minus_set_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &set_vec = args.data[0];
    auto &val_vec = args.data[1];
    idx_t count = args.size();
    set_vec.Flatten(count);
    val_vec.Flatten(count);
    switch (val_vec.GetType().id()) {
    case LogicalTypeId::INTEGER:
        BinaryExecutor::ExecuteWithNulls<string_t, int32_t, string_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, int32_t v, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = minus_set_int(s, v);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::BIGINT:
        BinaryExecutor::ExecuteWithNulls<string_t, int64_t, string_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, int64_t v, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = minus_set_bigint(s, v);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::DOUBLE:
        BinaryExecutor::ExecuteWithNulls<string_t, double, string_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, double v, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = minus_set_float(s, v);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::VARCHAR:
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, string_t val_str, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                text *txt = TextFromString(val_str);
                Set *r = minus_set_text(s, txt);
                free(txt);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::DATE:
        BinaryExecutor::ExecuteWithNulls<string_t, date_t, string_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, date_t d, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = minus_set_date(s, ToMeosDate(d));
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::TIMESTAMP_TZ:
        BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, string_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, timestamp_tz_t ts, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts);
                Set *r = minus_set_timestamptz(s, (TimestampTz)ts_meos.value);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    default:
        throw InternalException("Minus_set_value: unsupported value type");
    }
}

void SetFunctions::Minus_value_set(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &val_vec = args.data[0];
    auto &set_vec = args.data[1];
    idx_t count = args.size();
    val_vec.Flatten(count);
    set_vec.Flatten(count);
    switch (val_vec.GetType().id()) {
    case LogicalTypeId::INTEGER:
        BinaryExecutor::ExecuteWithNulls<int32_t, string_t, string_t>(
            val_vec, set_vec, result, count,
            [&](int32_t v, string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = minus_int_set(v, s);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::BIGINT:
        BinaryExecutor::ExecuteWithNulls<int64_t, string_t, string_t>(
            val_vec, set_vec, result, count,
            [&](int64_t v, string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = minus_bigint_set(v, s);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::DOUBLE:
        BinaryExecutor::ExecuteWithNulls<double, string_t, string_t>(
            val_vec, set_vec, result, count,
            [&](double v, string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = minus_float_set(v, s);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::VARCHAR:
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
            val_vec, set_vec, result, count,
            [&](string_t val_str, string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                text *txt = TextFromString(val_str);
                Set *r = minus_text_set(txt, s);
                free(txt);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::DATE:
        BinaryExecutor::ExecuteWithNulls<date_t, string_t, string_t>(
            val_vec, set_vec, result, count,
            [&](date_t d, string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = minus_date_set(ToMeosDate(d), s);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::TIMESTAMP_TZ:
        BinaryExecutor::ExecuteWithNulls<timestamp_tz_t, string_t, string_t>(
            val_vec, set_vec, result, count,
            [&](timestamp_tz_t ts, string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts);
                Set *r = minus_timestamptz_set((TimestampTz)ts_meos.value, s);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    default:
        throw InternalException("Minus_value_set: unsupported value type");
    }
}

void SetFunctions::Minus_set_set(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> string_t {
            Set *s1 = CopySet(a);
            Set *s2 = CopySet(b);
            Set *r = minus_set_set(s1, s2);
            free(s1);
            free(s2);
            return WriteSetBlob(result, r);
        });
}

// ---------- Intersection (*) ----------
void SetFunctions::Intersect_set_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &set_vec = args.data[0];
    auto &val_vec = args.data[1];
    idx_t count = args.size();
    set_vec.Flatten(count);
    val_vec.Flatten(count);
    switch (val_vec.GetType().id()) {
    case LogicalTypeId::INTEGER:
        BinaryExecutor::ExecuteWithNulls<string_t, int32_t, string_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, int32_t v, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = intersection_set_int(s, v);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::BIGINT:
        BinaryExecutor::ExecuteWithNulls<string_t, int64_t, string_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, int64_t v, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = intersection_set_bigint(s, v);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::DOUBLE:
        BinaryExecutor::ExecuteWithNulls<string_t, double, string_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, double v, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = intersection_set_float(s, v);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::VARCHAR:
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, string_t val_str, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                text *txt = TextFromString(val_str);
                Set *r = intersection_set_text(s, txt);
                free(txt);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::DATE:
        BinaryExecutor::ExecuteWithNulls<string_t, date_t, string_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, date_t d, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = intersection_set_date(s, ToMeosDate(d));
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::TIMESTAMP_TZ:
        BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, string_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, timestamp_tz_t ts, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts);
                Set *r = intersection_set_timestamptz(s, (TimestampTz)ts_meos.value);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    default:
        throw InternalException("Intersect_set_value: unsupported value type");
    }
}

void SetFunctions::Intersect_value_set(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &val_vec = args.data[0];
    auto &set_vec = args.data[1];
    idx_t count = args.size();
    val_vec.Flatten(count);
    set_vec.Flatten(count);
    switch (val_vec.GetType().id()) {
    case LogicalTypeId::INTEGER:
        BinaryExecutor::ExecuteWithNulls<int32_t, string_t, string_t>(
            val_vec, set_vec, result, count,
            [&](int32_t v, string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = intersection_int_set(v, s);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::BIGINT:
        BinaryExecutor::ExecuteWithNulls<int64_t, string_t, string_t>(
            val_vec, set_vec, result, count,
            [&](int64_t v, string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = intersection_bigint_set(v, s);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::DOUBLE:
        BinaryExecutor::ExecuteWithNulls<double, string_t, string_t>(
            val_vec, set_vec, result, count,
            [&](double v, string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = intersection_float_set(v, s);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::VARCHAR:
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
            val_vec, set_vec, result, count,
            [&](string_t val_str, string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                text *txt = TextFromString(val_str);
                Set *r = intersection_text_set(txt, s);
                free(txt);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::DATE:
        BinaryExecutor::ExecuteWithNulls<date_t, string_t, string_t>(
            val_vec, set_vec, result, count,
            [&](date_t d, string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                Set *r = intersection_date_set(ToMeosDate(d), s);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    case LogicalTypeId::TIMESTAMP_TZ:
        BinaryExecutor::ExecuteWithNulls<timestamp_tz_t, string_t, string_t>(
            val_vec, set_vec, result, count,
            [&](timestamp_tz_t ts, string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
                Set *s = CopySet(blob);
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts);
                Set *r = intersection_timestamptz_set((TimestampTz)ts_meos.value, s);
                free(s);
                return WriteSetBlob(result, r);
            });
        break;
    default:
        throw InternalException("Intersect_value_set: unsupported value type");
    }
}

void SetFunctions::Intersect_set_set(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> string_t {
            Set *s1 = CopySet(a);
            Set *s2 = CopySet(b);
            Set *r = intersection_set_set(s1, s2);
            free(s1);
            free(s2);
            return WriteSetBlob(result, r);
        });
}

// ---------- Distance (<->) ----------
void SetFunctions::Distance_set_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &set_vec = args.data[0];
    auto &val_vec = args.data[1];
    idx_t count = args.size();
    set_vec.Flatten(count);
    val_vec.Flatten(count);
    std::string alias = set_vec.GetType().ToString();
    if (alias == "intset") {
        BinaryExecutor::ExecuteWithNulls<string_t, int32_t, int32_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, int32_t v, ValidityMask &mask, idx_t idx) -> int32_t {
                Set *s = CopySet(blob);
                int32_t r = distance_set_int(s, v);
                free(s);
                return r;
            });
        return;
    }
    if (alias == "bigintset") {
        BinaryExecutor::ExecuteWithNulls<string_t, int64_t, int64_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, int64_t v, ValidityMask &mask, idx_t idx) -> int64_t {
                Set *s = CopySet(blob);
                int64_t r = distance_set_bigint(s, v);
                free(s);
                return r;
            });
        return;
    }
    if (alias == "floatset") {
        BinaryExecutor::ExecuteWithNulls<string_t, double, double>(
            set_vec, val_vec, result, count,
            [&](string_t blob, double v, ValidityMask &mask, idx_t idx) -> double {
                Set *s = CopySet(blob);
                double r = distance_set_float(s, v);
                free(s);
                return r;
            });
        return;
    }
    if (alias == "dateset") {
        BinaryExecutor::ExecuteWithNulls<string_t, date_t, int32_t>(
            set_vec, val_vec, result, count,
            [&](string_t blob, date_t dt, ValidityMask &mask, idx_t idx) -> int32_t {
                Set *s = CopySet(blob);
                int r = distance_set_date(s, ToMeosDate(dt));
                free(s);
                return r;
            });
        return;
    }
    if (alias == "tstzset") {
        BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, double>(
            set_vec, val_vec, result, count,
            [&](string_t blob, timestamp_tz_t ts, ValidityMask &mask, idx_t idx) -> double {
                Set *s = CopySet(blob);
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts);
                double r = distance_set_timestamptz(s, (TimestampTz)ts_meos.value);
                free(s);
                return r;
            });
        return;
    }
    throw InternalException("Distance_set_value: unsupported set type");
}

void SetFunctions::Distance_value_set(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &val_vec = args.data[0];
    auto &set_vec = args.data[1];
    idx_t count = args.size();
    val_vec.Flatten(count);
    set_vec.Flatten(count);
    std::string alias = set_vec.GetType().ToString();
    if (alias == "intset") {
        BinaryExecutor::ExecuteWithNulls<int32_t, string_t, int32_t>(
            val_vec, set_vec, result, count,
            [&](int32_t v, string_t blob, ValidityMask &mask, idx_t idx) -> int32_t {
                Set *s = CopySet(blob);
                int32_t r = distance_set_int(s, v);
                free(s);
                return r;
            });
        return;
    }
    if (alias == "bigintset") {
        BinaryExecutor::ExecuteWithNulls<int64_t, string_t, int64_t>(
            val_vec, set_vec, result, count,
            [&](int64_t v, string_t blob, ValidityMask &mask, idx_t idx) -> int64_t {
                Set *s = CopySet(blob);
                int64_t r = distance_set_bigint(s, v);
                free(s);
                return r;
            });
        return;
    }
    if (alias == "floatset") {
        BinaryExecutor::ExecuteWithNulls<double, string_t, double>(
            val_vec, set_vec, result, count,
            [&](double v, string_t blob, ValidityMask &mask, idx_t idx) -> double {
                Set *s = CopySet(blob);
                double r = distance_set_float(s, v);
                free(s);
                return r;
            });
        return;
    }
    if (alias == "dateset") {
        BinaryExecutor::ExecuteWithNulls<date_t, string_t, int32_t>(
            val_vec, set_vec, result, count,
            [&](date_t dt, string_t blob, ValidityMask &mask, idx_t idx) -> int32_t {
                Set *s = CopySet(blob);
                int r = distance_set_date(s, ToMeosDate(dt));
                free(s);
                return r;
            });
        return;
    }
    if (alias == "tstzset") {
        BinaryExecutor::ExecuteWithNulls<timestamp_tz_t, string_t, double>(
            val_vec, set_vec, result, count,
            [&](timestamp_tz_t ts, string_t blob, ValidityMask &mask, idx_t idx) -> double {
                Set *s = CopySet(blob);
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts);
                double r = distance_set_timestamptz(s, (TimestampTz)ts_meos.value);
                free(s);
                return r;
            });
        return;
    }
    throw InternalException("Distance_value_set: unsupported set type");
}

void SetFunctions::Distance_set_set(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &a = args.data[0];
    auto &b = args.data[1];
    std::string alias = a.GetType().ToString();
    if (alias == "intset") {
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, int32_t>(
            a, b, result, args.size(),
            [&](string_t x, string_t y, ValidityMask &mask, idx_t idx) -> int32_t {
                Set *s1 = CopySet(x);
                Set *s2 = CopySet(y);
                int r = distance_intset_intset(s1, s2);
                free(s1);
                free(s2);
                return r;
            });
        return;
    }
    if (alias == "bigintset") {
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, int64_t>(
            a, b, result, args.size(),
            [&](string_t x, string_t y, ValidityMask &mask, idx_t idx) -> int64_t {
                Set *s1 = CopySet(x);
                Set *s2 = CopySet(y);
                int64_t r = distance_bigintset_bigintset(s1, s2);
                free(s1);
                free(s2);
                return r;
            });
        return;
    }
    if (alias == "floatset") {
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, double>(
            a, b, result, args.size(),
            [&](string_t x, string_t y, ValidityMask &mask, idx_t idx) -> double {
                Set *s1 = CopySet(x);
                Set *s2 = CopySet(y);
                double r = distance_floatset_floatset(s1, s2);
                free(s1);
                free(s2);
                return r;
            });
        return;
    }
    if (alias == "dateset") {
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, int32_t>(
            a, b, result, args.size(),
            [&](string_t x, string_t y, ValidityMask &mask, idx_t idx) -> int32_t {
                Set *s1 = CopySet(x);
                Set *s2 = CopySet(y);
                int r = distance_dateset_dateset(s1, s2);
                free(s1);
                free(s2);
                return r;
            });
        return;
    }
    if (alias == "tstzset") {
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, double>(
            a, b, result, args.size(),
            [&](string_t x, string_t y, ValidityMask &mask, idx_t idx) -> double {
                Set *s1 = CopySet(x);
                Set *s2 = CopySet(y);
                double r = distance_tstzset_tstzset(s1, s2);
                free(s1);
                free(s2);
                return r;
            });
        return;
    }
    throw InternalException("Distance_set_set: unsupported set type");
}

void SetFunctions::Distance_value_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &v0 = args.data[0];
    auto &v1 = args.data[1];
    idx_t count = args.size();
    v0.Flatten(count);
    v1.Flatten(count);
    auto id = v0.GetType().id();
    switch (id) {
    case LogicalTypeId::INTEGER:
        BinaryExecutor::ExecuteWithNulls<int32_t, int32_t, int32_t>(
            v0, v1, result, count,
            [&](int32_t a, int32_t b, ValidityMask &mask, idx_t idx) -> int32_t {
                Datum d = distance_value_value(Datum(a), Datum(b), T_INT4);
                return DatumGetInt32(d);
            });
        break;
    case LogicalTypeId::BIGINT:
        BinaryExecutor::ExecuteWithNulls<int64_t, int64_t, int64_t>(
            v0, v1, result, count,
            [&](int64_t a, int64_t b, ValidityMask &mask, idx_t idx) -> int64_t {
                Datum d = distance_value_value(Datum(a), Datum(b), T_INT8);
                return DatumGetInt64(d);
            });
        break;
    case LogicalTypeId::DOUBLE:
        BinaryExecutor::ExecuteWithNulls<double, double, double>(
            v0, v1, result, count,
            [&](double a, double b, ValidityMask &mask, idx_t idx) -> double {
                Datum d = distance_value_value(Float8GetDatum(a), Float8GetDatum(b), T_FLOAT8);
                return DatumGetFloat8(d);
            });
        break;
    case LogicalTypeId::DATE:
        BinaryExecutor::ExecuteWithNulls<date_t, date_t, int32_t>(
            v0, v1, result, count,
            [&](date_t a, date_t b, ValidityMask &mask, idx_t idx) -> int32_t {
                Datum d = distance_value_value(Datum(ToMeosDate(a)), Datum(ToMeosDate(b)), T_DATE);
                return DatumGetInt32(d);
            });
        break;
    case LogicalTypeId::TIMESTAMP_TZ:
        BinaryExecutor::ExecuteWithNulls<timestamp_tz_t, timestamp_tz_t, double>(
            v0, v1, result, count,
            [&](timestamp_tz_t a, timestamp_tz_t b, ValidityMask &mask, idx_t idx) -> double {
                timestamp_tz_t am = DuckDBToMeosTimestamp(a);
                timestamp_tz_t bm = DuckDBToMeosTimestamp(b);
                Datum d = distance_value_value(Datum(am.value), Datum(bm.value), T_TIMESTAMPTZ);
                return DatumGetFloat8(d);
            });
        break;
    default:
        throw InternalException("Distance_value_value: unsupported type");
    }
}

void SetFunctions::Set_eq(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> bool {
            Set *s1 = CopySet(a);
            Set *s2 = CopySet(b);
            bool r = set_eq(s1, s2);
            free(s1);
            free(s2);
            return r;
        });
}

void SetFunctions::Set_ne(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> bool {
            Set *s1 = CopySet(a);
            Set *s2 = CopySet(b);
            bool r = set_ne(s1, s2);
            free(s1);
            free(s2);
            return r;
        });
}

void SetFunctions::Set_lt(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> bool {
            Set *s1 = CopySet(a);
            Set *s2 = CopySet(b);
            bool r = set_lt(s1, s2);
            free(s1);
            free(s2);
            return r;
        });
}

void SetFunctions::Set_le(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> bool {
            Set *s1 = CopySet(a);
            Set *s2 = CopySet(b);
            bool r = set_le(s1, s2);
            free(s1);
            free(s2);
            return r;
        });
}

void SetFunctions::Set_ge(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> bool {
            Set *s1 = CopySet(a);
            Set *s2 = CopySet(b);
            bool r = set_ge(s1, s2);
            free(s1);
            free(s2);
            return r;
        });
}

void SetFunctions::Set_gt(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> bool {
            Set *s1 = CopySet(a);
            Set *s2 = CopySet(b);
            bool r = set_gt(s1, s2);
            free(s1);
            free(s2);
            return r;
        });
}

void SetFunctions::Set_cmp(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, int32_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> int32_t {
            Set *s1 = CopySet(a);
            Set *s2 = CopySet(b);
            int32_t r = set_cmp(s1, s2);
            free(s1);
            free(s2);
            return r;
        });
}
} // namespace duckdb
