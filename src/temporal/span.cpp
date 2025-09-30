#define MOBILITYDUCK_EXTENSION_TYPES

#include "temporal/span.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_util.hpp"

#include "time_util.hpp"

#include <regex>
#include <string>

extern "C" {
    #include <meos.h>
    #include <meos_geo.h>
    #include <meos_internal.h>
}

namespace duckdb {

#define DEFINE_SPAN_TYPE(NAME)                                        \
    LogicalType SpanTypes::NAME() {                                   \
        auto type = LogicalType(LogicalTypeId::BLOB);                \
        type.SetAlias(#NAME);                                        \
        return type;                                                 \
    }

DEFINE_SPAN_TYPE(INTSPAN)
DEFINE_SPAN_TYPE(BIGINTSPAN)
DEFINE_SPAN_TYPE(FLOATSPAN)
DEFINE_SPAN_TYPE(DATESPAN)
DEFINE_SPAN_TYPE(TSTZSPAN)

#undef DEFINE_SPAN_TYPE

void SpanTypes::RegisterTypes(DatabaseInstance &db) {
    ExtensionUtil::RegisterType(db, "INTSPAN", INTSPAN());
    ExtensionUtil::RegisterType(db, "BIGINTSPAN", BIGINTSPAN());
    ExtensionUtil::RegisterType(db, "FLOATSPAN", FLOATSPAN());
    ExtensionUtil::RegisterType(db, "DATESPAN", DATESPAN());
    ExtensionUtil::RegisterType(db, "TSTZSPAN", TSTZSPAN());    
}

const std::vector<LogicalType> &SpanTypes::AllTypes() {
    static std::vector<LogicalType> types = {
        INTSPAN(),
        BIGINTSPAN(),
        FLOATSPAN(),
        DATESPAN(),
        TSTZSPAN()
    };
    return types;
}

meosType SpanTypeMapping::GetMeosTypeFromAlias(const std::string &alias) {
    static const std::unordered_map<std::string, meosType> alias_to_type = {
        {"INTSPAN", T_INTSPAN},
        {"BIGINTSPAN", T_BIGINTSPAN},
        {"FLOATSPAN", T_FLOATSPAN},
        {"DATESPAN", T_DATESPAN},
        {"TSTZSPAN", T_TSTZSPAN}        
    };

    auto it = alias_to_type.find(alias);
    if (it != alias_to_type.end()) {
        return it->second;
    } else {
        return T_UNKNOWN;
    }
}

LogicalType SpanTypeMapping::GetChildType(const LogicalType &type) {
    auto alias = type.ToString();
    if (alias == "INTSPAN") return LogicalType::INTEGER;
    if (alias == "BIGINTSPAN") return LogicalType::BIGINT;
    if (alias == "FLOATSPAN") return LogicalType::DOUBLE;
    if (alias == "DATESPAN") return LogicalType::DATE;
    if (alias == "TSTZSPAN") return LogicalType::TIMESTAMP_TZ;    
    throw NotImplementedException("GetChildType: unsupported alias: " + alias);
}

// Register all cast functions 
void SpanTypes::RegisterCastFunctions(DatabaseInstance &instance) {
    for (const auto &span_type : SpanTypes::AllTypes()) {
        ExtensionUtil::RegisterCastFunction(
            instance,
            span_type,                      
            LogicalType::VARCHAR,   
            SpanFunctions::Span_to_text   
        ); // Blob to text
        ExtensionUtil::RegisterCastFunction(
            instance,
            LogicalType::VARCHAR, 
            span_type,                                    
            SpanFunctions::Text_to_span   
        ); // text to blob
        
        ExtensionUtil::RegisterCastFunction(
            instance,
            SpanTypes::INTSPAN(),
            SpanTypes::FLOATSPAN(),
            SpanFunctions::Intspan_to_floatspan_cast // intspan -> floatspan 
        );

        ExtensionUtil::RegisterCastFunction(
            instance,
            SpanTypes::FLOATSPAN(),
            SpanTypes::INTSPAN(),
            SpanFunctions::Floatspan_to_intspan_cast // floatspan -> intspan
        );
        
        ExtensionUtil::RegisterCastFunction(
            instance,
            SpanTypes::DATESPAN(),
            SpanTypes::TSTZSPAN(),
            SpanFunctions::Datespan_to_tstzspan_cast // datespan -> tstzspan
        );
        
        ExtensionUtil::RegisterCastFunction(
            instance,
            SpanTypes::TSTZSPAN(),
            SpanTypes::DATESPAN(),
            SpanFunctions::Tstzspan_to_datespan_cast // tstzspan -> datespan 
        );
    }
}

void SpanTypes::RegisterScalarFunctions(DatabaseInstance &db) {    
    for (const auto &span_type : SpanTypes::AllTypes()) {
        auto base_type = SpanTypeMapping::GetChildType(span_type);         

        // Register: asText
        if (span_type == SpanTypes::FLOATSPAN()) {            
            ExtensionUtil::RegisterFunction( // asText(floatspan)
                db, ScalarFunction("asText", {span_type}, LogicalType::VARCHAR, SpanFunctions::Span_as_text)
            );
            
            ExtensionUtil::RegisterFunction( // asText(floatspan, int)
                db, ScalarFunction("asText", {span_type, LogicalType::INTEGER}, LogicalType::VARCHAR, SpanFunctions::Span_as_text)
            );
        } else {            
            ExtensionUtil::RegisterFunction( // All other span types
                db, ScalarFunction("asText", {span_type}, LogicalType::VARCHAR, SpanFunctions::Span_as_text)
            );
        }

        // Register span constructor functions
        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction(span_type.ToString(), {LogicalType::VARCHAR}, span_type, SpanFunctions::Span_constructor)                 
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("span", {base_type, base_type}, span_type, SpanFunctions::Span_binary_constructor)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("span", {base_type}, span_type, SpanFunctions::Value_to_span)                 
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("intspan", {SpanTypes::FLOATSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Floatspan_to_intspan)                 
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("floatspan", {SpanTypes::INTSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Intspan_to_floatspan)                 
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("datespan", {SpanTypes::TSTZSPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Tstzspan_to_datespan)                 
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("tstzspan", {SpanTypes::DATESPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Datespan_to_tstzspan)                 
        );

        if (span_type == SpanTypes::INTSPAN() ||span_type == SpanTypes::DATESPAN()){

            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("shift", {span_type, LogicalType::INTEGER}, span_type, SpanFunctions::Numspan_shift)
            ); 
        }
        else if( span_type == SpanTypes::BIGINTSPAN() ){
             ExtensionUtil::RegisterFunction(
                db, ScalarFunction("shift", {span_type, LogicalType::BIGINT}, span_type, SpanFunctions::Numspan_shift)
            ); 
        }
        else if( span_type == SpanTypes::FLOATSPAN() ){
             ExtensionUtil::RegisterFunction(
                db, ScalarFunction("shift", {span_type, LogicalType::FLOAT}, span_type, SpanFunctions::Numspan_shift)
            ); 
        }
        else if( span_type == SpanTypes::TSTZSPAN() ){
             ExtensionUtil::RegisterFunction(
                db, ScalarFunction("shift", {span_type, LogicalType::INTERVAL}, span_type, SpanFunctions::Numspan_shift)
            ); 
        }

        if (span_type == SpanTypes::TSTZSPAN()) {
            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("@>", {span_type, LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SpanFunctions::Contains_tstzspan_timestamptz)
            );
        }


    }
}

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
    meosType target_meos_type = SpanTypeMapping::GetMeosTypeFromAlias(type_alias);
    
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

// --- Span constructor from string ---
void SpanFunctions::Span_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input_vec = args.data[0];
    auto &result_type = result.GetType();
    std::string type_alias = result_type.GetAlias();
    
    meosType target_meos_type = SpanTypeMapping::GetMeosTypeFromAlias(type_alias);
    
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
static void Span_binary_constructor_tstz(Vector &args0, Vector &args1, Vector &result, idx_t count) {
    auto &result_type = result.GetType();
    std::string type_alias = result_type.GetAlias();
    meosType spantype = SpanTypeMapping::GetMeosTypeFromAlias(type_alias);
    if (spantype == T_UNKNOWN) {
        throw InvalidInputException("Unknown span type: " + type_alias);
    }

    BinaryExecutor::Execute<timestamp_tz_t, timestamp_tz_t, string_t>(
        args0, args1, result, count,
        [&](timestamp_tz_t lower_duckdb, timestamp_tz_t upper_duckdb) {
            TimestampTz lower = ToMeosTimestamp(lower_duckdb);
            TimestampTz upper = ToMeosTimestamp(upper_duckdb);
            Datum lower_dat = (Datum)lower;
            Datum upper_dat = (Datum)upper;
            
            // Default values for now
            bool lower_inc = true;
            bool upper_inc = false;

            meosType basetype = spantype_basetype(spantype);
            Span *span = span_make(lower, upper, lower_inc, upper_inc, basetype);
            if (span == NULL) {
                throw InvalidInputException("Failed to create span from timestamps");
            }
            size_t span_size = sizeof(Span);
            char *span_data = (char*)malloc(span_size);
            memcpy(span_data, span, span_size);
            free(span);
            return string_t(span_data, span_size);
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}



void SpanFunctions::Span_binary_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &args0 = args.data[0];
    auto &args1 = args.data[1];

    auto out_type = result.GetType();
    meosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(out_type.GetAlias());

    switch (span_type) {
        case T_TSTZSPAN:
            Span_binary_constructor_tstz(args0, args1, result, args.size());
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

static void Value_to_span_core(Vector &source, Vector &result, idx_t count, meosType base_type){
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
    }
}

void SpanFunctions::Value_to_span(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &source = args.data[0];
    auto out_type = result.GetType();
    meosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(out_type.GetAlias());
    meosType base_type = spantype_basetype(span_type);

    Value_to_span_core(source, result, args.size(), base_type);

}





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



static inline string_t Numspan_shift_common(const string_t &blob, Datum shift_datum,
                                        meosType validate_span_type, Vector &result) {
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
    meosType span_type = SpanTypeMapping::GetMeosTypeFromAlias(out_type.GetAlias());    
    
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
        case T_TSTZSPAN: { // shift(tstzspan, interval) -> tstzspan
            BinaryExecutor::Execute<string_t, interval_t, string_t>(
                span_vec, args.data[1], result, args.size(),
                [&](string_t blob, interval_t shift_interval) -> string_t {
                    return Tstzspan_shift_common(blob, shift_interval, result);
                });
            break;
        }
        default:
            throw NotImplementedException("shift(<span>): unsupported span type");
    }
}

// --- OPERATOR: tstzspan @> timestamptz ---
void SpanFunctions::Contains_tstzspan_timestamptz(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, timestamp_tz_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t span_blob, timestamp_tz_t ts_duckdb) -> bool {
            const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
            size_t span_data_size = span_blob.GetSize();
            uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
            memcpy(span_data_copy, span_data, span_data_size);
            Span *span = reinterpret_cast<Span*>(span_data_copy);
            if (!span) {
                free(span_data_copy);
                throw InvalidInputException("Invalid TSTZSPAN data: null pointer");
            }
            timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
            bool ret = contains_span_value(span, Datum(ts_meos.value));
            free(span_data_copy);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

} // namespace duckdb

#ifndef MOBILITYDUCK_EXTENSION_TYPES
#endif