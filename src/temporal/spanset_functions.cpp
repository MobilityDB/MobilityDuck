#include "temporal/spanset.hpp"
#include "temporal/spanset_functions.hpp"
#include "temporal/span.hpp"
#include "temporal/set.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "time_util.hpp"


extern "C" {     
    #include "meos.h"    
    #include "meos_internal.h"   
    #include "meos_geo.h"
}

namespace duckdb {


// --- Cast to text ---
bool SpansetFunctions::Spanset_to_text(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
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

        SpanSet *s = (SpanSet*)malloc(size);
        memcpy(s, data, size);        

        char *cstr = spanset_out(s, 15);  
        result_data[i] = StringVector::AddString(result, cstr);

        free(cstr);
        free(s);
    }

    result.SetVectorType(VectorType::FLAT_VECTOR);
    return true;
}

bool SpansetFunctions::Text_to_spanset(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);

    auto target_type = result.GetType();
    MeosType spanset_type = SpansetTypeMapping::GetMeosTypeFromAlias(target_type.GetAlias());

    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_str) -> string_t {
            std::string str = input_str.GetString();

            SpanSet *s = spanset_in(str.c_str(), spanset_type);            

            string_t result_blob = StringVector::AddStringOrBlob(result, (const char *)s, spanset_mem_size(s));
            free(s);
            return result_blob;
        }
    );

    return true;
}


// --- Constructor ---
void SpansetFunctions::Spanset_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_input = args.data[0];
    auto meos_type = SpansetTypeMapping::GetMeosTypeFromAlias(result.GetType().ToString());
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

            Span *spans = (Span *)malloc(sizeof(Span) * length);
            if (!spans) throw std::bad_alloc();

            for (idx_t i = 0; i < length; ++i) {
                idx_t idx = offset + i;
                auto blob_val = child.GetValue(idx);
                auto blob_str = blob_val.GetValueUnsafe<string_t>(); 
                auto *bytes = (const uint8_t *)blob_str.GetData();
                auto size = blob_str.GetSize();                
                memcpy(&spans[i], bytes, size);
            }

            SpanSet *sset = spanset_make_free(spans, (int)length, true, false);
            size_t size = spanset_mem_size(sset);
            string_t blob = StringVector::AddStringOrBlob(result, (const char *)sset, size);

            free(sset);
            return blob;
        }
    );
}

// --- Conversion: base type -> spanset ---

static inline void Write_spanset(Vector &result, idx_t row, SpanSet *s) {
    auto out = FlatVector::GetData<string_t>(result);
    out[row] = StringVector::AddStringOrBlob(result, (const char *)s, spanset_mem_size(s));
    free(s);
}

static inline void Value_to_spanset_core(Vector &source, Vector &result, idx_t count, MeosType base_type) {
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);

    auto handle_null = [&](idx_t row) { FlatVector::SetNull(result, row, true); };

    switch (base_type) {
        case T_INT4: {
            auto in = FlatVector::GetData<int32_t>(source);
            for (idx_t i = 0; i < count; ++i) {
                if (FlatVector::IsNull(source, i)) { handle_null(i); continue; }
                SpanSet *s = value_spanset(Datum(in[i]), T_INT4);
                Write_spanset(result, i, s);
            }
            break;
        }
        case T_INT8: {
            auto in = FlatVector::GetData<int64_t>(source);
            for (idx_t i = 0; i < count; ++i) {
                if (FlatVector::IsNull(source, i)) { handle_null(i); continue; }
                SpanSet *s = value_spanset(Datum(in[i]), T_INT8);
                Write_spanset(result, i, s);
            }
            break;
        }
        case T_FLOAT8: {
            auto in = FlatVector::GetData<double>(source);
            for (idx_t i = 0; i < count; ++i) {
                if (FlatVector::IsNull(source, i)) { handle_null(i); continue; }
                SpanSet *s = value_spanset(Float8GetDatum(in[i]), T_FLOAT8);
                Write_spanset(result, i, s);
            }
            break;
        }
        case T_DATE: {
            auto in = FlatVector::GetData<date_t>(source);
            for (idx_t i = 0; i < count; ++i) {
                if (FlatVector::IsNull(source, i)) { handle_null(i); continue; }
                int32_t days = (int32_t)ToMeosDate(in[i]);
                SpanSet *s = value_spanset(Datum(days), T_DATE);
                Write_spanset(result, i, s);
            }
            break;
        }
        case T_TIMESTAMPTZ: {
            auto in = FlatVector::GetData<timestamp_t>(source);
            for (idx_t i = 0; i < count; ++i) {
                if (FlatVector::IsNull(source, i)) { handle_null(i); continue; }
                auto meos_ts = ToMeosTimestamp(in[i]);
                SpanSet *s = value_spanset(Datum(meos_ts), T_TIMESTAMPTZ);
                Write_spanset(result, i, s);
            }
            break;
        }
        default:
            throw NotImplementedException("SpansetConversion: unsupported base type for conversion to spanset");
    }
}

// --- CAST (conversion: base -> spanset) ----
bool SpansetFunctions::Value_to_spanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    auto target_type   = result.GetType();
    MeosType spanset_t = SpansetTypeMapping::GetMeosTypeFromAlias(target_type.GetAlias());
    MeosType span_t    = spansettype_spantype(spanset_t);
    MeosType base_t    = spantype_basetype(span_t);

    Value_to_spanset_core(source, result, count, base_t);
    return true;
}

// --- SCALAR function (conversion: base -> spanset) ----
void SpansetFunctions::Value_to_spanset(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &source      = args.data[0];
    auto out_type     = result.GetType();
    MeosType spanset_t= SpansetTypeMapping::GetMeosTypeFromAlias(out_type.GetAlias());
    MeosType span_t   = spansettype_spantype(spanset_t);
    MeosType base_t   = spantype_basetype(span_t);

    Value_to_spanset_core(source, result, args.size(), base_t);
}

// --- Conversion: set -> spanset ---
static void Set_to_spanset_common(Vector &source, Vector &result, idx_t count) {
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto handle_null = [&](idx_t row) { FlatVector::SetNull(result, row, true); };
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const uint8_t *bytes = (const uint8_t *)blob.GetData();
            size_t size = blob.GetSize();            
            Set *s = (Set *)malloc(size);
            memcpy(s, bytes, size);            

            SpanSet *spanset = set_spanset(s);
            free(s);

            size_t result_size = spanset_mem_size(spanset);
            string_t result_blob = StringVector::AddStringOrBlob(result, (const char *)spanset, result_size);
            free(spanset);

            return result_blob;
        }
    );
}

// --- CAST (conversion: set -> spanset) ----
bool SpansetFunctions::Set_to_spanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {    
    Set_to_spanset_common(source, result, count);
    return true;
}


// --- Conversion: span -> spanset ---
static inline void Span_to_spanset_common (Vector &source, Vector &result, idx_t count){
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto handle_null = [&](idx_t row) { FlatVector::SetNull(result, row, true); };
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const uint8_t *bytes = (const uint8_t *)blob.GetData();
            size_t size = blob.GetSize();            
            Span *s = (Span *)malloc(size);
            memcpy(s, bytes, size);            

            SpanSet *spanset = span_to_spanset(s);
            free(s);

            size_t result_size = spanset_mem_size(spanset);
            string_t result_blob = StringVector::AddStringOrBlob(result, (const char *)spanset, result_size);
            free(spanset);

            return result_blob;
        }
    );
}

// --- CAST (conversion: span -> spanset) ----
bool SpansetFunctions::Span_to_spanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {    
    Span_to_spanset_common(source, result, count);
    return true;
}

// --- Conversion: spanset -> span ---

static inline void Spanset_to_span_common(Vector &source, Vector &result, idx_t count) {
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto handle_null = [&](idx_t row) { FlatVector::SetNull(result, row, true); };
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const uint8_t *bytes = (const uint8_t *)blob.GetData();
            size_t size = blob.GetSize();            
            SpanSet *spanset = (SpanSet *)malloc(size);
            memcpy(spanset, bytes, size);            

            Span *s = spanset_span(spanset);
            free(spanset);

            size_t result_size = sizeof(Span);
            string_t result_blob = StringVector::AddStringOrBlob(result, (const char *)s, result_size);
            free(s);

            return result_blob;
        }
    );
}

// --- CAST (conversion: spanset -> span) ----
bool SpansetFunctions::Spanset_to_span_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Spanset_to_span_common(source, result, count);
    return true;
}


// --- Conversion: intspanset <-> floatspanset ---
static inline void Intspanset_to_floatspanset_common(Vector &source, Vector &result, idx_t count) {
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto handle_null = [&](idx_t row) { FlatVector::SetNull(result, row, true); };
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const uint8_t *bytes = (const uint8_t *)blob.GetData();
            size_t size = blob.GetSize();            
            SpanSet *intspanset = (SpanSet *)malloc(size);
            memcpy(intspanset, bytes, size);            

            SpanSet *floatspanset = intspanset_to_floatspanset(intspanset);
            free(intspanset);

            size_t result_size = spanset_mem_size(floatspanset);
            string_t result_blob = StringVector::AddStringOrBlob(result, (const char *)floatspanset, result_size);
            free(floatspanset);

            return result_blob;
        }
    );
}

static inline void Floatspanset_to_intspanset_common(Vector &source, Vector &result, idx_t count) {
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto handle_null = [&](idx_t row) { FlatVector::SetNull(result, row, true); };
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const uint8_t *bytes = (const uint8_t *)blob.GetData();
            size_t size = blob.GetSize();            
            SpanSet *floatspanset = (SpanSet *)malloc(size);
            memcpy(floatspanset, bytes, size);            

            SpanSet *intspanset = floatspanset_to_intspanset(floatspanset);
            free(floatspanset);

            size_t result_size = spanset_mem_size(intspanset);
            string_t result_blob = StringVector::AddStringOrBlob(result, (const char *)intspanset, result_size);
            free(intspanset);

            return result_blob;
        }
    );
}

// --- CAST (conversion: intspanset -> floatspanset) ----
bool SpansetFunctions::Intspanset_to_floatspanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Intspanset_to_floatspanset_common(source, result, count);
    return true;
}

// --- SCALAR function (intspanset -> floatspanset) ----
void SpansetFunctions::Intspanset_to_floatspanset(DataChunk &args, ExpressionState &state, Vector &result) {
    Intspanset_to_floatspanset_common(args.data[0], result, args.size());
}

// --- CAST (conversion: floatspanset -> intspanset) ----
bool SpansetFunctions::Floatspanset_to_intspanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Floatspanset_to_intspanset_common(source, result, count);
    return true;
}
// --- SCALAR function (floatspanset -> intspanset) ----
void SpansetFunctions::Floatspanset_to_intspanset(DataChunk &args, ExpressionState &state, Vector &result) {
    Floatspanset_to_intspanset_common(args.data[0], result, args.size());
}

// --- Conversion: datespanset <-> tstzspanset ----
static inline void Datespanset_to_tstzspanset_common(Vector &source, Vector &result, idx_t count) {
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto handle_null = [&](idx_t row) { FlatVector::SetNull(result, row, true); };
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const uint8_t *bytes = (const uint8_t *)blob.GetData();
            size_t size = blob.GetSize();            
            SpanSet *datespanset = (SpanSet *)malloc(size);
            memcpy(datespanset, bytes, size);            

            SpanSet *tstzspanset = datespanset_to_tstzspanset(datespanset);
            free(datespanset);

            size_t result_size = spanset_mem_size(tstzspanset);
            string_t result_blob = StringVector::AddStringOrBlob(result, (const char *)tstzspanset, result_size);
            free(tstzspanset);

            return result_blob;
        }
    );
}

static inline void Tstzspanset_to_datespanset_common(Vector &source, Vector &result, idx_t count) {
    source.Flatten(count);
    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto handle_null = [&](idx_t row) { FlatVector::SetNull(result, row, true); };
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            const uint8_t *bytes = (const uint8_t *)blob.GetData();
            size_t size = blob.GetSize();            
            SpanSet *tstzspanset = (SpanSet *)malloc(size);
            memcpy(tstzspanset, bytes, size);            

            SpanSet *datespanset = tstzspanset_to_datespanset(tstzspanset);
            free(tstzspanset);

            size_t result_size = spanset_mem_size(datespanset);
            string_t result_blob = StringVector::AddStringOrBlob(result, (const char *)datespanset, result_size);
            free(datespanset);

            return result_blob;
        }
    );
}

// --- CAST (conversion: datespanset -> tstzspanset) ----
bool SpansetFunctions::Datespanset_to_tstzspanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Datespanset_to_tstzspanset_common(source, result, count);
    return true;
}
// --- SCALAR function (datespanset -> tstzspanset) ----
void SpansetFunctions::Datespanset_to_tstzspanset(DataChunk &args, ExpressionState &state, Vector &result) {
    Datespanset_to_tstzspanset_common(args.data[0], result, args.size());
}
// --- CAST (conversion: tstzspanset -> datespanset) ----
bool SpansetFunctions::Tstzspanset_to_datespanset_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Tstzspanset_to_datespanset_common(source, result, count);
    return true;
}
// --- SCALAR function (tstzspanset -> datespanset) ----
void SpansetFunctions::Tstzspanset_to_datespanset(DataChunk &args, ExpressionState &state, Vector &result) {
    Tstzspanset_to_datespanset_common(args.data[0], result, args.size());
}

// -- memsize ---
void SpansetFunctions::Spanset_mem_size(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];

    UnaryExecutor::Execute<string_t, int32_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> int32_t {                                 
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            SpanSet *s = (SpanSet*)malloc(size);
            memcpy(s, data, size);            
            int mem_size = spanset_mem_size(s);  
            free(s);
            return mem_size;
        });
}

// --- lower ---

void SpansetFunctions::Spanset_lower(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    auto spanset_type = SpansetTypeMapping::GetMeosTypeFromAlias(input.GetType().ToString());
    auto span_type = spansettype_spantype(spanset_type);
    auto base_type = spantype_basetype(span_type);

    switch (base_type){
        case T_INT4:{
            UnaryExecutor::Execute<string_t, int32_t>(
                input, result, args.size(),
                [&](string_t input_blob) -> int64_t {                                 
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    SpanSet *s = (SpanSet*)malloc(size);
                    memcpy(s, data, size);            
                    Datum lower = spanset_lower(s);  
                    free(s);
                    return (int32_t)lower;
                });
                break;
        }
        case T_INT8:{
            UnaryExecutor::Execute<string_t, int64_t>(
                input, result, args.size(),
                [&](string_t input_blob) -> int64_t {                                 
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    SpanSet *s = (SpanSet*)malloc(size);
                    memcpy(s, data, size);            
                    Datum lower = spanset_lower(s);  
                    free(s);
                    return (int64_t)lower;
                });
                break;
        }
        case T_FLOAT8: {
            UnaryExecutor::Execute<string_t, double>(
                input, result, args.size(),
                [&](string_t input_blob) -> double {                                 
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    SpanSet *s = (SpanSet*)malloc(size);
                    memcpy(s, data, size);            
                    Datum lower = spanset_lower(s);  
                    free(s);
                    return DatumGetFloat8(lower);
                });
                break;
        }
        case T_DATE: {
            UnaryExecutor::Execute<string_t, date_t>(
                input, result, args.size(),
                [&](string_t input_blob) -> date_t {                                 
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    SpanSet *s = (SpanSet*)malloc(size);
                    memcpy(s, data, size);            
                    Datum lower = spanset_lower(s);  
                    free(s);
                    return date_t(int32(FromMeosDate(lower)));
                });
                break;
        }
        case T_TIMESTAMPTZ: {
            UnaryExecutor::Execute<string_t, timestamp_t>(
                input, result, args.size(),
                [&](string_t input_blob) -> timestamp_t {                                 
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    SpanSet *s = (SpanSet*)malloc(size);
                    memcpy(s, data, size);            
                    Datum lower = spanset_lower(s);  
                    free(s);
                    return timestamp_t((int64_t)FromMeosTimestamp(lower));
                });
                break;
        }
        default: {
            throw NotImplementedException("Spanset lower not implemented for this type");            
        }
    }
}

// --- upper ---

void SpansetFunctions::Spanset_upper(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    auto spanset_type = SpansetTypeMapping::GetMeosTypeFromAlias(input.GetType().ToString());
    auto span_type = spansettype_spantype(spanset_type);
    auto base_type = spantype_basetype(span_type);

    switch (base_type){
        case T_INT4:{
            UnaryExecutor::Execute<string_t, int32_t>(
                input, result, args.size(),
                [&](string_t input_blob) -> int64_t {                                 
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    SpanSet *s = (SpanSet*)malloc(size);
                    memcpy(s, data, size);            
                    Datum upper = spanset_upper(s);  
                    free(s);
                    return (int32_t)upper;
                });
                break;
        }
        case T_INT8:{
            UnaryExecutor::Execute<string_t, int64_t>(
                input, result, args.size(),
                [&](string_t input_blob) -> int64_t {                                 
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    SpanSet *s = (SpanSet*)malloc(size);
                    memcpy(s, data, size);            
                    Datum upper = spanset_upper(s);  
                    free(s);
                    return (int64_t)upper;
                });
                break;
        }
        case T_FLOAT8: {
            UnaryExecutor::Execute<string_t, double>(
                input, result, args.size(),
                [&](string_t input_blob) -> double {                                 
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    SpanSet *s = (SpanSet*)malloc(size);
                    memcpy(s, data, size);            
                    Datum upper = spanset_upper(s);  
                    free(s);
                    return DatumGetFloat8(upper);
                });
                break;
        }
        case T_DATE: {
            UnaryExecutor::Execute<string_t, date_t>(
                input, result, args.size(),
                [&](string_t input_blob) -> date_t {                                 
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    SpanSet *s = (SpanSet*)malloc(size);
                    memcpy(s, data, size);
                    Datum upper = spanset_upper(s);
                    free(s);
                    return date_t(int32(FromMeosDate(upper)));
                });
                break;
        }
        case T_TIMESTAMPTZ: {
            UnaryExecutor::Execute<string_t, timestamp_t>(
                input, result, args.size(),
                [&](string_t input_blob) -> timestamp_t {                                 
                    const uint8_t *data = (const uint8_t *)input_blob.GetData();
                    size_t size = input_blob.GetSize();
                    SpanSet *s = (SpanSet*)malloc(size);
                    memcpy(s, data, size);            
                    Datum upper = spanset_upper(s);  
                    free(s);
                    return timestamp_t((int64_t)FromMeosTimestamp(upper));
                });
                break;
        }
        default: {
            throw NotImplementedException("Spanset upper not implemented for this type");            
        }
    }
}    

// --- lower_inc ---

void SpansetFunctions::Spanset_lower_inc(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    UnaryExecutor::Execute<string_t, bool>(
        input, result, args.size(),
        [&](string_t input_blob) -> bool {                                 
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            SpanSet *s = (SpanSet*)malloc(size);
            memcpy(s, data, size);            
            bool lower_inc = spanset_lower_inc(s);  
            free(s);
            return lower_inc;
        });
}

// --- upper_inc ---
void SpansetFunctions::Spanset_upper_inc(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    UnaryExecutor::Execute<string_t, bool>(
        input, result, args.size(),
        [&](string_t input_blob) -> bool {                                 
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            SpanSet *s = (SpanSet*)malloc(size);
            memcpy(s, data, size);            
            bool upper_inc = spanset_upper_inc(s);  
            free(s);
            return upper_inc;
        });
}

// --- width ---
void SpansetFunctions::Numspanset_width(DataChunk &args, ExpressionState &state, Vector &result) {
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
        // Second argument registered as BOOLEAN; read as bool, not int32_t.
        bool bools = has_bools ? FlatVector::GetData<bool>(*bools_vec_ptr)[i] : false;

        const uint8_t *data = (const uint8_t *)blob.GetData();
        size_t size = blob.GetSize();

        SpanSet *s = (SpanSet *)malloc(size);
        memcpy(s, data, size);
        auto r = numspanset_width(s, bools);
        free(s);
        auto result_type = result.GetType().id();
        if (result_type == LogicalType::INTEGER){
            FlatVector::GetData<int32_t>(result)[i] = Datum(r);        
        }        
        if (result_type == LogicalType::BIGINT){
            FlatVector::GetData<int64_t>(result)[i] = Datum(r);        
        }
        if (result_type == LogicalType::DOUBLE){
            FlatVector::GetData<double>(result)[i] = DatumGetFloat8(r);        
        }
    }
}

// --- duration (datespanset) ---

void SpansetFunctions::Datespanset_duration(DataChunk &args, ExpressionState &state, Vector &result) {
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
        bool bools = has_bools ? FlatVector::GetData<bool>(*bools_vec_ptr)[i] : false;

        const uint8_t *data = (const uint8_t *)blob.GetData();
        size_t size = blob.GetSize();

        SpanSet *s = (SpanSet *)malloc(size);
        memcpy(s, data, size);
        auto r = datespanset_duration(s, bools);
        free(s);
        FlatVector::GetData<interval_t>(result)[i] = IntervalToIntervalt(r);                
    }
}

// --- duration (tstzspanset) ---
void SpansetFunctions::Tstzspanset_duration(DataChunk &args, ExpressionState &state, Vector &result) {
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
        bool bools = has_bools ? FlatVector::GetData<bool>(*bools_vec_ptr)[i] : false;

        const uint8_t *data = (const uint8_t *)blob.GetData();
        size_t size = blob.GetSize();

        SpanSet *s = (SpanSet *)malloc(size);
        memcpy(s, data, size);
        auto r = tstzspanset_duration(s, bools);
        free(s);
        FlatVector::GetData<interval_t>(result)[i] = IntervalToIntervalt(r);                
    }
}

// --- numSpans ---
void SpansetFunctions::Spanset_num_spans(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    // numSpans is registered as returning LogicalType::INTEGER.
    UnaryExecutor::Execute<string_t, int32_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> int32_t {
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            SpanSet *s = (SpanSet*)malloc(size);
            memcpy(s, data, size);
            int32_t num_spans = spanset_num_spans(s);
            free(s);
            return num_spans;
        });
}




// --- numDates ---
void SpansetFunctions::Datespanset_num_dates(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    // Registered as returning LogicalType::INTEGER.
    UnaryExecutor::Execute<string_t, int32_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> int32_t {
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            SpanSet *s = (SpanSet*)malloc(size);
            memcpy(s, data, size);
            int32_t num_dates = datespanset_num_dates(s);
            free(s);
            return num_dates;
        });
}

// --- startDate ---
void SpansetFunctions::Datespanset_start_date(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    UnaryExecutor::Execute<string_t, date_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> date_t {                                 
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            SpanSet *s = (SpanSet*)malloc(size);
            memcpy(s, data, size);            
            DateADT start_date = datespanset_start_date(s);  
            free(s);
            return date_t(int32(FromMeosDate(start_date)));
        });
}

// --- endDate ---
void SpansetFunctions::Datespanset_end_date(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    UnaryExecutor::Execute<string_t, date_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> date_t {                                 
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            SpanSet *s = (SpanSet*)malloc(size);
            memcpy(s, data, size);            
            DateADT end_date = datespanset_end_date(s);  
            free(s);
            return date_t(int32(FromMeosDate(end_date)));
        });
}



// --- numTimestamps ---
void SpansetFunctions::Tstzspanset_num_timestamps(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    // Registered as returning LogicalType::INTEGER.
    UnaryExecutor::Execute<string_t, int32_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> int32_t {
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            SpanSet *s = (SpanSet*)malloc(size);
            memcpy(s, data, size);
            int32_t num_timestamps = tstzspanset_num_timestamps(s);
            free(s);
            return num_timestamps;
        });
}

// --- startTimestamp ---
void SpansetFunctions::Tstzspanset_start_timestamptz(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    UnaryExecutor::Execute<string_t, timestamp_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> timestamp_t {                                 
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            SpanSet *s = (SpanSet*)malloc(size);
            memcpy(s, data, size);            
            TimestampTz start_timestamp = tstzspanset_start_timestamptz(s);  
            free(s);
            return timestamp_t((int64_t)FromMeosTimestamp(start_timestamp));
        });
}

// --- endTimestamp ---
void SpansetFunctions::Tstzspanset_end_timestamptz(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    UnaryExecutor::Execute<string_t, timestamp_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> timestamp_t {                                 
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            SpanSet *s = (SpanSet*)malloc(size);
            memcpy(s, data, size);            
            TimestampTz end_timestamp = tstzspanset_end_timestamptz(s);  
            free(s);
            return timestamp_t((int64_t)FromMeosTimestamp(end_timestamp));
        });
}



static inline string_t Numspanset_shift_common(const string_t &blob, Datum shift_datum,
                                        MeosType validate_spanset_type, Vector &result) {
    const uint8_t *data = (const uint8_t *)blob.GetData();
    size_t size = blob.GetSize();
    
    SpanSet *s = (SpanSet *)malloc(size);
    memcpy(s, data, size);
    
    switch(validate_spanset_type) {
        case T_INTSPANSET: VALIDATE_INTSPANSET(s, NULL); break;
        case T_FLOATSPANSET: VALIDATE_FLOATSPANSET(s, NULL); break;
        case T_BIGINTSPANSET: VALIDATE_BIGINTSPANSET(s, NULL); break;
        case T_DATESPANSET: VALIDATE_DATESPANSET(s, NULL); break;
        default: break;
    }    
    
    SpanSet *r = numspanset_shift_scale(s, shift_datum, 0, /*do_shift=*/true, /*do_scale=*/false);
    
    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, size);
    
    free(s);
    free(r);
    return out;
}

static inline string_t Tstzspanset_shift_common(const string_t &blob, interval_t duckdb_interval, Vector &result) {
    const uint8_t *data = (const uint8_t *)blob.GetData();
    size_t size = blob.GetSize();
    
    SpanSet *s = (SpanSet *)malloc(size);
    memcpy(s, data, size);
    
    VALIDATE_TSTZSPANSET(s, NULL);
    
    // Convert DuckDB interval_t to MEOS Interval
    MeosInterval meos_interval = IntervaltToInterval(duckdb_interval);
    
    SpanSet *r = tstzspanset_shift_scale(s, &meos_interval, NULL);
    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, size);
    
    free(s);
    free(r);
    return out;
}

static inline string_t Numspanset_scale_common(const string_t &blob, Datum scale_datum,
                                        MeosType validate_spanset_type, Vector &result) {
    const uint8_t *data = (const uint8_t *)blob.GetData();
    size_t size = blob.GetSize();
    
    SpanSet *s = (SpanSet *)malloc(size);
    memcpy(s, data, size);
    
    switch(validate_spanset_type) {
        case T_INTSPANSET: VALIDATE_INTSPANSET(s, NULL); break;
        case T_FLOATSPANSET: VALIDATE_FLOATSPANSET(s, NULL); break;
        case T_BIGINTSPANSET: VALIDATE_BIGINTSPANSET(s, NULL); break;
        case T_DATESPANSET: VALIDATE_DATESPANSET(s, NULL); break;
        default: break;
    }    
    
    SpanSet *r = numspanset_shift_scale(s, 0, scale_datum, /*do_shift=*/false, /*do_scale=*/true);
    
    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, size);
    
    free(s);
    free(r);
    return out;
}

static inline string_t Tstzspanset_scale_common(const string_t &blob, interval_t duckdb_scale, Vector &result) {
    const uint8_t *data = (const uint8_t *)blob.GetData();
    size_t size = blob.GetSize();
    
    SpanSet *s = (SpanSet *)malloc(size);
    memcpy(s, data, size);
    
    VALIDATE_TSTZSPANSET(s, NULL);
    
    // Convert DuckDB interval_t to MEOS Interval
    MeosInterval meos_interval = IntervaltToInterval(duckdb_scale);
    
    SpanSet *r = tstzspanset_shift_scale(s, NULL, &meos_interval);
    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, size);
    
    free(s);
    free(r);
    return out;
}

static inline string_t Tstzspanset_shift_scale_common(const string_t &blob, interval_t shift_iv, interval_t scale_iv,
                                                   Vector &result) {
    const uint8_t *data = (const uint8_t *)blob.GetData();
    size_t size = blob.GetSize();

    SpanSet *s = (SpanSet *)malloc(size);
    memcpy(s, data, size);

    VALIDATE_TSTZSPAN(s, NULL);

    MeosInterval meos_shift = IntervaltToInterval(shift_iv);
    MeosInterval meos_scale = IntervaltToInterval(scale_iv);

    SpanSet *r = tstzspanset_shift_scale(s, &meos_shift, &meos_scale);
    free(s);
    if (!r) {
        throw InvalidInputException("tstzspanset_shift_scale failed");
    }
    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, size);
    free(r);
    return out;
}

static inline string_t Numspanset_shift_scale_common(const string_t &blob, Datum shift_datum, Datum scale_datum,
                                                 MeosType validate_spanset_type, Vector &result) {
    const uint8_t *data = (const uint8_t *)blob.GetData();
    size_t size = blob.GetSize();

    SpanSet *s = (SpanSet *)malloc(size);
    memcpy(s, data, size);

    switch (validate_spanset_type) {
        case T_INTSPANSET:
            VALIDATE_INTSPANSET(s, NULL);
            break;
        case T_FLOATSPANSET:
            VALIDATE_FLOATSPANSET(s, NULL);
            break;
        case T_BIGINTSPANSET:
            VALIDATE_BIGINTSPANSET(s, NULL);
            break;
        case T_DATESPANSET:
            VALIDATE_DATESPANSET(s, NULL);
            break;
        default:
            break;
    }

    SpanSet *r = numspanset_shift_scale(s, shift_datum, scale_datum, /*do_shift=*/true, /*do_scale=*/true);
    free(s);
    if (!r) {
        throw InvalidInputException("numspanset_shift_scale failed");
    }
    string_t out = StringVector::AddStringOrBlob(result, (const char *)r, size);
    free(r);
    return out;
}

// floor/ceil/round/degrees/radians on floatspanset are generated from the catalog
// (generated_temporal_udfs.cpp); no hand bodies remain.

// spans(<spanset_type>) is generated from the catalog (spanset_spans) in generated_temporal_udfs.cpp.
void SpansetFunctions::Spanset_split_n_spans(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &spanset_vec = args.data[0];
    auto &n_vec = args.data[1];
    idx_t row_count = args.size();
    spanset_vec.Flatten(row_count);
    n_vec.Flatten(row_count);

    auto &result_validity = FlatVector::Validity(result);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &child_vector = ListVector::GetEntry(result);
    child_vector.SetVectorType(VectorType::FLAT_VECTOR);
    ListVector::Reserve(result, row_count);

    idx_t total_offset = 0;
    const size_t span_bytes = sizeof(Span);

    for (idx_t i = 0; i < row_count; ++i) {
        if (spanset_vec.GetValue(i).IsNull() || n_vec.GetValue(i).IsNull()) {
            result_validity.SetInvalid(i);
            continue;
        }

        string_t spanset_blob = FlatVector::GetData<string_t>(spanset_vec)[i];
        int32_t n = FlatVector::GetData<int32_t>(n_vec)[i];

        SpanSet *s = (SpanSet *)malloc(spanset_blob.GetSize());
        memcpy(s, spanset_blob.GetData(), spanset_blob.GetSize());

        int out_count = 0;
        Span *spans = spanset_split_n_spans(s, n, &out_count);
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

void SpansetFunctions::Spanset_split_each_n_spans(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &spanset_vec = args.data[0];
    auto &n_vec = args.data[1];
    idx_t row_count = args.size();
    spanset_vec.Flatten(row_count);
    n_vec.Flatten(row_count);

    auto &result_validity = FlatVector::Validity(result);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &child_vector = ListVector::GetEntry(result);
    child_vector.SetVectorType(VectorType::FLAT_VECTOR);
    ListVector::Reserve(result, row_count);

    idx_t total_offset = 0;
    const size_t span_bytes = sizeof(Span);

    for (idx_t i = 0; i < row_count; ++i) {
        if (spanset_vec.GetValue(i).IsNull() || n_vec.GetValue(i).IsNull()) {
            result_validity.SetInvalid(i);
            continue;
        }

        string_t spanset_blob = FlatVector::GetData<string_t>(spanset_vec)[i];
        int32_t n = FlatVector::GetData<int32_t>(n_vec)[i];

        SpanSet *s = (SpanSet *)malloc(spanset_blob.GetSize());
        memcpy(s, spanset_blob.GetData(), spanset_blob.GetSize());

        int out_count = 0;
        Span *spans = spanset_split_each_n_spans(s, n, &out_count);
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

} // namespace duckdb   
