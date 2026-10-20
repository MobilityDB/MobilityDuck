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

// --- *FromBinary parser (set_from_wkb is subtype-agnostic) ---
void SetFunctions::Set_from_binary(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            if (input.GetSize() == 0)
                throw InvalidInputException("setFromBinary: empty WKB input");
            uint8_t *wkb = (uint8_t *)malloc(input.GetSize());
            if (!wkb) throw InternalException("setFromBinary: malloc failed");
            memcpy(wkb, input.GetData(), input.GetSize());
            Set *s = set_from_wkb(wkb, input.GetSize());
            free(wkb);
            if (!s) throw InvalidInputException(
                "setFromBinary: invalid MEOS-WKB");
            size_t sz = set_mem_size(s);
            string_t stored = StringVector::AddStringOrBlob(
                result, string_t(reinterpret_cast<const char *>(s), sz));
            free(s);
            return stored;
        });
}

void SetFunctions::Set_from_hexwkb(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string hex(input.GetData(), input.GetSize());
            Set *s = set_from_hexwkb(hex.c_str());
            if (!s) throw InvalidInputException(
                "setFromHexWKB: invalid hex-encoded MEOS-WKB");
            size_t sz = set_mem_size(s);
            string_t stored = StringVector::AddStringOrBlob(
                result, string_t(reinterpret_cast<const char *>(s), sz));
            free(s);
            return stored;
        });
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
            MeosType elem_type = settype_basetype(meos_type);

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
                    default: {
                        // The remaining base values are the ones the binding registers as
                        // types of its own, and the catalog states how MEOS carries each:
                        // a by-value base arrives in the eight bytes of its Datum, and every
                        // other one is a pointer to the value. set_make_exp copies each value
                        // into the set and set_make_free releases the array alone, so a
                        // pointer may address the child vector directly.
                        if (basetype_byvalue(elem_type)) {
                            values[i] = Datum(FlatVector::GetData<int64_t>(child)[idx]);
                        } else {
                            values[i] = PointerGetDatum(
                                FlatVector::GetData<string_t>(child)[idx].GetDataUnsafe());
                        }
                        break;
                    }
                }
            }

            MeosType base_type = settype_basetype(meos_type);            
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

// --- SCALAR function (base -> set) ----
void SetFunctions::Value_to_set(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &source = args.data[0];
    auto out_type = result.GetType();
    MeosType set_type  = SetTypeMapping::GetMeosTypeFromAlias(out_type.GetAlias());
    MeosType base_type = settype_basetype(set_type);

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


// --- getValues ---


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
// floor/ceil/round/degrees/radians on floatset are generated from the catalog
// (generated_temporal_udfs.cpp); no hand bodies remain.

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


// ---------- Minus (-) ----------

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
} // namespace duckdb
