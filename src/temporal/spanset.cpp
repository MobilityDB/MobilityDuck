#include "temporal/spanset.hpp"
#include "temporal/span.hpp"
#include "temporal/set.hpp"
#include "duckdb/main/extension_util.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_util.hpp"

#include "time_util.hpp"


extern "C" {     
    #include "meos.h"    
    #include "meos_internal.h"   
    #include "meos_geo.h"
}

namespace duckdb {

#define DEFINE_SPAN_SET_TYPE(NAME)                                        \
    LogicalType SpansetTypes::NAME() {                                   \
        auto type = LogicalType(LogicalTypeId::BLOB);             \
        type.SetAlias(#NAME);                                        \
        return type;                                                 \
    }

DEFINE_SPAN_SET_TYPE(intspanset)
DEFINE_SPAN_SET_TYPE(bigintspanset)
DEFINE_SPAN_SET_TYPE(floatspanset)
DEFINE_SPAN_SET_TYPE(datespanset)
DEFINE_SPAN_SET_TYPE(tstzspanset)

#undef DEFINE_SET_TYPE

void SpansetTypes::RegisterTypes(DatabaseInstance &db) {
    ExtensionUtil::RegisterType(db, "intspanset", intspanset());
    ExtensionUtil::RegisterType(db, "bigintspanset", bigintspanset());
    ExtensionUtil::RegisterType(db, "floatspanset", floatspanset());    
    ExtensionUtil::RegisterType(db, "datespanset", datespanset());
    ExtensionUtil::RegisterType(db, "tstzspanset", tstzspanset());    
}

const std::vector<LogicalType> &SpansetTypes::AllTypes() {
    static std::vector<LogicalType> types = {
        intspanset(),
        bigintspanset(),
        floatspanset(),        
        datespanset(),
        tstzspanset()
    };
    return types;
}

meosType SpansetTypeMapping::GetMeosTypeFromAlias(const std::string &alias) {
    static const std::unordered_map<std::string, meosType> alias_to_type = {
        {"intspanset", T_INTSPANSET},
        {"bigintspanset", T_BIGINTSPANSET},
        {"floatspanset", T_FLOATSPANSET},        
        {"datespanset", T_DATESPANSET},
        {"tstzspanset", T_TSTZSPANSET}                
    };

    auto it = alias_to_type.find(alias);
    if (it != alias_to_type.end()) {
        return it->second;
    } else {
        return T_UNKNOWN;
    }
}

LogicalType SpansetTypeMapping::GetChildType(const LogicalType &type) {
    auto alias = type.ToString();        
    if (alias == "intspanset") return SpanTypes::INTSPAN();
    if (alias == "bigintspanset") return SpanTypes::BIGINTSPAN();
    if (alias == "floatspanset") return SpanTypes::FLOATSPAN();    
    if (alias == "datespanset") return SpanTypes::DATESPAN();
    if (alias == "tstzspanset") return SpanTypes::TSTZSPAN();   
    throw NotImplementedException("GetChildType: unsupported alias: " + alias); 
}

LogicalType SpansetTypeMapping::GetSetType(const LogicalType &type) {
    auto alias = type.ToString();        
    if (alias == "intspanset") return SetTypes::intset();
    if (alias == "bigintspanset") return SetTypes::bigintset();
    if (alias == "floatspanset") return SetTypes::floatset();    
    if (alias == "datespanset") return SetTypes::dateset();
    if (alias == "tstzspanset") return SetTypes::tstzset();
    throw NotImplementedException("GetChildType: unsupported alias: " + alias);
}

LogicalType SpansetTypeMapping::GetBaseType(const LogicalType &type) {
    auto alias = type.ToString();
    if (alias == "intspanset") return LogicalType::INTEGER;
    if (alias == "bigintspanset") return LogicalType::BIGINT;
    if (alias == "floatspanset") return LogicalType::DOUBLE;    
    if (alias == "datespanset") return LogicalType::DATE;
    if (alias == "tstzspanset") return LogicalType::TIMESTAMP_TZ; 
    throw NotImplementedException("GetChildType: unsupported alias: " + alias);
}

// --- Register Cast ---
void SpansetTypes::RegisterCastFunctions(DatabaseInstance &instance) {
    for (const auto &spanset_type : SpansetTypes::AllTypes()) {
        ExtensionUtil::RegisterCastFunction(
            instance,
            spanset_type,                      
            LogicalType::VARCHAR,   
            SpansetFunctions::Spanset_to_text   
        ); // Blob to text
        ExtensionUtil::RegisterCastFunction(
            instance,
            LogicalType::VARCHAR, 
            spanset_type,                                    
            SpansetFunctions::Text_to_spanset   
        ); // text to blob
        
        auto base_type = SpansetTypeMapping::GetBaseType(spanset_type);
        ExtensionUtil::RegisterCastFunction(
            instance,
            base_type,
            spanset_type,
            SpansetFunctions::Value_to_spanset_cast
        );

        auto set_type = SpansetTypeMapping::GetSetType(spanset_type);        
        ExtensionUtil::RegisterCastFunction(
            instance,
            set_type,
            spanset_type,
            SpansetFunctions::Set_to_spanset_cast
        );
        auto child_type = SpansetTypeMapping::GetChildType(spanset_type); // span
        ExtensionUtil::RegisterCastFunction(
            instance,
            child_type,
            spanset_type,
            SpansetFunctions::Span_to_spanset_cast
        );

        ExtensionUtil::RegisterCastFunction(
            instance,
            spanset_type,
            child_type,
            SpansetFunctions::Spanset_to_span_cast
        );

        ExtensionUtil::RegisterCastFunction(
            instance,
            SpansetTypes::intspanset(),
            SpansetTypes::floatspanset(),
            SpansetFunctions::Intspanset_to_floatspanset_cast
        );

        ExtensionUtil::RegisterCastFunction(
            instance,
            SpansetTypes::floatspanset(),
            SpansetTypes::intspanset(),
            SpansetFunctions::Floatspanset_to_intspanset_cast
        );

        ExtensionUtil::RegisterCastFunction(
            instance,
            SpansetTypes::datespanset(),
            SpansetTypes::tstzspanset(),
            SpansetFunctions::Datespanset_to_tstzspanset_cast
        );

        ExtensionUtil::RegisterCastFunction(
            instance,
            SpansetTypes::tstzspanset(),
            SpansetTypes::datespanset(),
            SpansetFunctions::Tstzspanset_to_datespanset_cast
        );
    }
}

// --- Register Scalar Functions ---
void SpansetTypes::RegisterScalarFunctions(DatabaseInstance &db) {    
    for (const auto &spanset_type : SpansetTypes::AllTypes()) {
        auto child_type = SpansetTypeMapping::GetChildType(spanset_type);    // span     
        auto base_type = SpansetTypeMapping::GetBaseType(spanset_type); 
        auto set_type = SpansetTypeMapping::GetSetType(spanset_type);       // set
        // Register: asText
        if (spanset_type == SpansetTypes::floatspanset()) {            
            ExtensionUtil::RegisterFunction( // asText(floatset)
                db, ScalarFunction("asText", {spanset_type}, LogicalType::VARCHAR, SpansetFunctions::Spanset_as_text)
            );
            
            ExtensionUtil::RegisterFunction( // asText(floatset, int)
                db, ScalarFunction("asText", {spanset_type, LogicalType::INTEGER}, LogicalType::VARCHAR, SpansetFunctions::Spanset_as_text)
            );
        } else {            
            ExtensionUtil::RegisterFunction( // All other set types
                db, ScalarFunction("asText", {spanset_type}, LogicalType::VARCHAR, SpansetFunctions::Spanset_as_text)
            );
        }

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("spanset", {LogicalType::LIST(child_type)}, spanset_type, SpansetFunctions::Spanset_constructor)                 
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("spanset", {base_type}, spanset_type, SpansetFunctions::Value_to_spanset)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("spanset", {SpansetTypeMapping::GetSetType(spanset_type)}, spanset_type, SpansetFunctions::Set_to_spanset)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("spanset", {child_type}, spanset_type, SpansetFunctions::Span_to_spanset)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("span", {spanset_type}, child_type, SpansetFunctions::Spanset_to_span)
        );
        
        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("intspanset", {SpansetTypes::floatspanset()}, SpansetTypes::intspanset(), SpansetFunctions::Floatspanset_to_intspanset)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("floatspanset", {SpansetTypes::intspanset()}, SpansetTypes::floatspanset(), SpansetFunctions::Intspanset_to_floatspanset)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("datespanset", {SpansetTypes::tstzspanset()}, SpansetTypes::datespanset(), SpansetFunctions::Tstzspanset_to_datespanset)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("tstzspanset", {SpansetTypes::datespanset()}, SpansetTypes::tstzspanset(), SpansetFunctions::Datespanset_to_tstzspanset)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("memSize", {spanset_type}, LogicalType::INTEGER, SpansetFunctions::Spanset_mem_size)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("lower", {spanset_type}, base_type, SpansetFunctions::Spanset_lower)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("upper", {spanset_type}, base_type, SpansetFunctions::Spanset_upper)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("lowerInc", {spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_lower_inc)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("upperInc", {spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_upper_inc)
        );

        if (spanset_type == SpansetTypes::intspanset() || spanset_type == SpansetTypes::floatspanset() || spanset_type == SpansetTypes::bigintspanset()) {
            ExtensionUtil::RegisterFunction(
                db,
                ScalarFunction("width", {spanset_type}, base_type, SpansetFunctions::Numspanset_width)
            );

            ExtensionUtil::RegisterFunction(
                db,
                ScalarFunction("width", {spanset_type, LogicalType::BOOLEAN}, base_type, SpansetFunctions::Numspanset_width)
            );

            ExtensionUtil::RegisterFunction(
                db,
                ScalarFunction("duration", {SpansetTypes::datespanset()}, LogicalType::INTERVAL, SpansetFunctions::Datespanset_duration)
            );

            ExtensionUtil::RegisterFunction(
                db,
                ScalarFunction("duration", {SpansetTypes::tstzspanset()}, LogicalType::INTERVAL, SpansetFunctions::Tstzspanset_duration)
            );

            ExtensionUtil::RegisterFunction(
                db,
                ScalarFunction("duration", {SpansetTypes::datespanset(), LogicalType::BOOLEAN}, LogicalType::INTERVAL, SpansetFunctions::Datespanset_duration)
            );

            ExtensionUtil::RegisterFunction(
                db,
                ScalarFunction("duration", {SpansetTypes::tstzspanset(), LogicalType::BOOLEAN}, LogicalType::INTERVAL, SpansetFunctions::Tstzspanset_duration)
            );

        }

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("numSpans", {spanset_type}, LogicalType::INTEGER, SpansetFunctions::Spanset_num_spans)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("startSpan", {spanset_type}, child_type, SpansetFunctions::Spanset_start_span)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("endSpan", {spanset_type}, child_type, SpansetFunctions::Spanset_end_span)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("spanN", {spanset_type, LogicalType::INTEGER}, child_type, SpansetFunctions::Spanset_span_n)
        );
    }
}


// --- AsText ---
void SpansetFunctions::Spanset_as_text(DataChunk &args, ExpressionState &state, Vector &result) {
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

        SpanSet *s = (SpanSet *)malloc(size);
        memcpy(s, data, size);

        char *cstr = spanset_out(s, digits);
        auto str = StringVector::AddString(result, cstr);
        FlatVector::GetData<string_t>(result)[i] = str;

        free(s);
        free(cstr);
    }
}


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
    meosType spanset_type = SpansetTypeMapping::GetMeosTypeFromAlias(target_type.GetAlias());

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

static inline void Value_to_spanset_core(Vector &source, Vector &result, idx_t count, meosType base_type) {
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
    meosType spanset_t = SpansetTypeMapping::GetMeosTypeFromAlias(target_type.GetAlias());
    meosType span_t    = spansettype_spantype(spanset_t);
    meosType base_t    = spantype_basetype(span_t);

    Value_to_spanset_core(source, result, count, base_t);
    return true;
}

// --- SCALAR function (conversion: base -> spanset) ----
void SpansetFunctions::Value_to_spanset(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &source      = args.data[0];
    auto out_type     = result.GetType();
    meosType spanset_t= SpansetTypeMapping::GetMeosTypeFromAlias(out_type.GetAlias());
    meosType span_t   = spansettype_spantype(spanset_t);
    meosType base_t   = spantype_basetype(span_t);

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

// --- SCALAR function (set -> spanset) ----
void SpansetFunctions::Set_to_spanset(DataChunk &args, ExpressionState &state, Vector &result) {
    Set_to_spanset_common(args.data[0], result, args.size());
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

// --- SCALAR function (span -> spanset) ----
void SpansetFunctions::Span_to_spanset(DataChunk &args, ExpressionState &state, Vector &result) {
    Span_to_spanset_common(args.data[0], result, args.size());
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

// --- SCALAR function (spanset -> span) ----
void SpansetFunctions::Spanset_to_span(DataChunk &args, ExpressionState &state, Vector &result) {
    Spanset_to_span_common(args.data[0], result, args.size());
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
        int bools = has_bools ? FlatVector::GetData<int32_t>(*bools_vec_ptr)[i] : false;

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
        int bools = has_bools ? FlatVector::GetData<int32_t>(*bools_vec_ptr)[i] : false;

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
        int bools = has_bools ? FlatVector::GetData<int32_t>(*bools_vec_ptr)[i] : false;

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
    UnaryExecutor::Execute<string_t, int64_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> int64_t {                                 
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            SpanSet *s = (SpanSet*)malloc(size);
            memcpy(s, data, size);            
            int64_t num_spans = spanset_num_spans(s);  
            free(s);
            return num_spans;
        });
}

// --- startSpan ---
void SpansetFunctions::Spanset_start_span(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    UnaryExecutor::Execute<string_t, string_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> string_t {                                 
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            SpanSet *s = (SpanSet*)malloc(size);
            memcpy(s, data, size);            
            Span *start_span = spanset_start_span(s);  
            free(s);
            string_t out = StringVector::AddStringOrBlob(result, (const char *)start_span, sizeof(Span));
            free(start_span);
            return out;
        });
}

// --- endSpan ---
void SpansetFunctions::Spanset_end_span(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    UnaryExecutor::Execute<string_t, string_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> string_t {                                 
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            SpanSet *s = (SpanSet*)malloc(size);
            memcpy(s, data, size);            
            Span *end_span = spanset_end_span(s);  
            free(s);
            string_t out = StringVector::AddStringOrBlob(result, (const char *)end_span, sizeof(Span));
            free(end_span);
            return out;
        });
}

// --- spanN ---
void SpansetFunctions::Spanset_span_n(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    auto &n = args.data[1];
    BinaryExecutor::Execute<string_t, int32_t, string_t>(
            input, n, result, args.size(),
            [&](string_t blob, int32_t pos) -> string_t {                             
            const uint8_t *data = (const uint8_t *)blob.GetData();
            size_t size = blob.GetSize();
            SpanSet *s = (SpanSet*)malloc(size);
            memcpy(s, data, size);            
            int64_t n_value = FlatVector::GetData<int64_t>(n)[0];
            Span *span_n = spanset_span_n(s, n_value);  
            free(s);
            string_t out = StringVector::AddStringOrBlob(result, (const char *)span_n, sizeof(Span));
            free(span_n);
            return out;
        });
}

} // namespace duckdb   
