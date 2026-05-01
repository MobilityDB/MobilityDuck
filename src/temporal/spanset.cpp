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

void SpansetTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType( "intspanset", intspanset());
    loader.RegisterType( "bigintspanset", bigintspanset());
    loader.RegisterType( "floatspanset", floatspanset());    
    loader.RegisterType( "datespanset", datespanset());
    loader.RegisterType( "tstzspanset", tstzspanset());    
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
void SpansetTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    for (const auto &spanset_type : SpansetTypes::AllTypes()) {
        loader.RegisterCastFunction(
            spanset_type,                      
            LogicalType::VARCHAR,   
            SpansetFunctions::Spanset_to_text   
        ); // Blob to text
        loader.RegisterCastFunction(
            LogicalType::VARCHAR, 
            spanset_type,                                    
            SpansetFunctions::Text_to_spanset   
        ); // text to blob
        
        auto base_type = SpansetTypeMapping::GetBaseType(spanset_type);
        loader.RegisterCastFunction(
            base_type,
            spanset_type,
            SpansetFunctions::Value_to_spanset_cast
        );

        auto set_type = SpansetTypeMapping::GetSetType(spanset_type);        
        loader.RegisterCastFunction(
            set_type,
            spanset_type,
            SpansetFunctions::Set_to_spanset_cast
        );
        auto child_type = SpansetTypeMapping::GetChildType(spanset_type); // span
        loader.RegisterCastFunction(
            child_type,
            spanset_type,
            SpansetFunctions::Span_to_spanset_cast
        );

        loader.RegisterCastFunction(
            spanset_type,
            child_type,
            SpansetFunctions::Spanset_to_span_cast
        );

        loader.RegisterCastFunction(
            SpansetTypes::intspanset(),
            SpansetTypes::floatspanset(),
            SpansetFunctions::Intspanset_to_floatspanset_cast
        );

        loader.RegisterCastFunction(
            SpansetTypes::floatspanset(),
            SpansetTypes::intspanset(),
            SpansetFunctions::Floatspanset_to_intspanset_cast
        );

        loader.RegisterCastFunction(
            SpansetTypes::datespanset(),
            SpansetTypes::tstzspanset(),
            SpansetFunctions::Datespanset_to_tstzspanset_cast
        );

        loader.RegisterCastFunction(
            SpansetTypes::tstzspanset(),
            SpansetTypes::datespanset(),
            SpansetFunctions::Tstzspanset_to_datespanset_cast
        );
    }
}

// --- Register Scalar Functions ---
void SpansetTypes::RegisterScalarFunctions(ExtensionLoader &loader) {    
    for (const auto &spanset_type : SpansetTypes::AllTypes()) {
        auto child_type = SpansetTypeMapping::GetChildType(spanset_type);    // span     
        auto base_type = SpansetTypeMapping::GetBaseType(spanset_type); 
        auto set_type = SpansetTypeMapping::GetSetType(spanset_type);       // set
        // Register: asText
        if (spanset_type == SpansetTypes::floatspanset()) {            
            loader.RegisterFunction( // asText(floatset)
                ScalarFunction("asText", {spanset_type}, LogicalType::VARCHAR, SpansetFunctions::Spanset_as_text)
            );
            
            loader.RegisterFunction( // asText(floatset, int)
                ScalarFunction("asText", {spanset_type, LogicalType::INTEGER}, LogicalType::VARCHAR, SpansetFunctions::Spanset_as_text)
            );
        } else {            
            loader.RegisterFunction( // All other set types
                ScalarFunction("asText", {spanset_type}, LogicalType::VARCHAR, SpansetFunctions::Spanset_as_text)
            );
        }

        loader.RegisterFunction(
            ScalarFunction("spanset", {LogicalType::LIST(child_type)}, spanset_type, SpansetFunctions::Spanset_constructor)                 
        );

        loader.RegisterFunction(
            ScalarFunction("spanset", {base_type}, spanset_type, SpansetFunctions::Value_to_spanset)
        );

        loader.RegisterFunction(
            ScalarFunction("spanset", {SpansetTypeMapping::GetSetType(spanset_type)}, spanset_type, SpansetFunctions::Set_to_spanset)
        );

        loader.RegisterFunction(
            ScalarFunction("spanset", {child_type}, spanset_type, SpansetFunctions::Span_to_spanset)
        );

        loader.RegisterFunction(
            ScalarFunction("span", {spanset_type}, child_type, SpansetFunctions::Spanset_to_span)
        );
        
        loader.RegisterFunction(
            ScalarFunction("intspanset", {SpansetTypes::floatspanset()}, SpansetTypes::intspanset(), SpansetFunctions::Floatspanset_to_intspanset)
        );

        loader.RegisterFunction(
            ScalarFunction("floatspanset", {SpansetTypes::intspanset()}, SpansetTypes::floatspanset(), SpansetFunctions::Intspanset_to_floatspanset)
        );

        loader.RegisterFunction(
            ScalarFunction("datespanset", {SpansetTypes::tstzspanset()}, SpansetTypes::datespanset(), SpansetFunctions::Tstzspanset_to_datespanset)
        );

        loader.RegisterFunction(
            ScalarFunction("tstzspanset", {SpansetTypes::datespanset()}, SpansetTypes::tstzspanset(), SpansetFunctions::Datespanset_to_tstzspanset)
        );

        loader.RegisterFunction(
            ScalarFunction("memSize", {spanset_type}, LogicalType::INTEGER, SpansetFunctions::Spanset_mem_size)
        );

        loader.RegisterFunction(
            ScalarFunction("lower", {spanset_type}, base_type, SpansetFunctions::Spanset_lower)
        );

        loader.RegisterFunction(
            ScalarFunction("upper", {spanset_type}, base_type, SpansetFunctions::Spanset_upper)
        );

        loader.RegisterFunction(
            ScalarFunction("lowerInc", {spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_lower_inc)
        );

        loader.RegisterFunction(
            ScalarFunction("upperInc", {spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_upper_inc)
        );

        if (spanset_type == SpansetTypes::intspanset() || spanset_type == SpansetTypes::floatspanset() || spanset_type == SpansetTypes::bigintspanset()) {
            loader.RegisterFunction(
                ScalarFunction("width", {spanset_type}, base_type, SpansetFunctions::Numspanset_width)
            );

            loader.RegisterFunction(
                ScalarFunction("width", {spanset_type, LogicalType::BOOLEAN}, base_type, SpansetFunctions::Numspanset_width)
            );
        }

        loader.RegisterFunction(
            ScalarFunction("numSpans", {spanset_type}, LogicalType::INTEGER, SpansetFunctions::Spanset_num_spans)
        );

        loader.RegisterFunction(
            ScalarFunction("startSpan", {spanset_type}, child_type, SpansetFunctions::Spanset_start_span)
        );

        loader.RegisterFunction(
            ScalarFunction("endSpan", {spanset_type}, child_type, SpansetFunctions::Spanset_end_span)
        );

        loader.RegisterFunction(
            ScalarFunction("spanN", {spanset_type, LogicalType::INTEGER}, child_type, SpansetFunctions::Spanset_span_n)
        );

        if (spanset_type == SpansetTypes::intspanset() ||spanset_type == SpansetTypes::datespanset()){

            loader.RegisterFunction( ScalarFunction("shift", {spanset_type, LogicalType::INTEGER}, spanset_type, SpansetFunctions::Numspanset_shift)
            ); 
            
            loader.RegisterFunction( ScalarFunction("scale", {spanset_type, LogicalType::INTERVAL}, spanset_type, SpansetFunctions::Numspanset_scale)
            );

            loader.RegisterFunction(
                ScalarFunction("shiftScale", {spanset_type, LogicalType::INTEGER, LogicalType::INTEGER}, spanset_type,
                               SpansetFunctions::Numspanset_shift_scale));

        }
        else if( spanset_type == SpansetTypes::bigintspanset() ){
            loader.RegisterFunction( ScalarFunction("shift", {spanset_type, LogicalType::BIGINT}, spanset_type, SpansetFunctions::Numspanset_shift)
            ); 

            loader.RegisterFunction( ScalarFunction("scale", {spanset_type, LogicalType::INTERVAL}, spanset_type, SpansetFunctions::Numspanset_scale)
            );
            loader.RegisterFunction(
                ScalarFunction("shiftScale", {spanset_type, LogicalType::BIGINT, LogicalType::BIGINT}, spanset_type, SpansetFunctions::Numspanset_shift_scale)
            );    
        }
        else if( spanset_type == SpansetTypes::floatspanset() ){
            loader.RegisterFunction( ScalarFunction("shift", {spanset_type, LogicalType::DOUBLE}, spanset_type, SpansetFunctions::Numspanset_shift)
            ); 
            loader.RegisterFunction( ScalarFunction("scale", {spanset_type, LogicalType::INTERVAL}, spanset_type, SpansetFunctions::Numspanset_scale)
            );
            loader.RegisterFunction(
                ScalarFunction("shiftScale", {spanset_type, LogicalType::DOUBLE, LogicalType::DOUBLE}, spanset_type, SpansetFunctions::Numspanset_shift_scale)
            );

            loader.RegisterFunction( ScalarFunction("floor", {spanset_type}, spanset_type, SpansetFunctions::Floatspanset_floor)
            );
            loader.RegisterFunction( ScalarFunction("ceil", {spanset_type}, spanset_type, SpansetFunctions::Floatspanset_ceil)
            );
            loader.RegisterFunction( ScalarFunction("round", {spanset_type}, spanset_type, SpansetFunctions::Floatspanset_round)
            );

            loader.RegisterFunction( ScalarFunction("round", {spanset_type, LogicalType::INTEGER}, spanset_type, SpansetFunctions::Floatspanset_round)
            );
            loader.RegisterFunction( ScalarFunction("degrees", {spanset_type}, spanset_type, SpansetFunctions::Floatspanset_degrees)
            );
            loader.RegisterFunction( ScalarFunction("radians", {spanset_type}, spanset_type, SpansetFunctions::Floatspanset_radians)
            );

        }
        else if( spanset_type == SpansetTypes::tstzspanset() ){
            loader.RegisterFunction( ScalarFunction("shift", {spanset_type, LogicalType::INTERVAL}, spanset_type, SpansetFunctions::Tstzspanset_shift)
            ); 

            loader.RegisterFunction( ScalarFunction("scale", {spanset_type, LogicalType::INTERVAL}, spanset_type, SpansetFunctions::Tstzspanset_scale)
            );
            loader.RegisterFunction(
                ScalarFunction("shiftScale", {spanset_type, LogicalType::INTERVAL, LogicalType::INTERVAL}, spanset_type, SpansetFunctions::Tstzspanset_shift_scale)
            );

        } 
        loader.RegisterFunction(
            ScalarFunction("spans", {spanset_type}, LogicalType::LIST(child_type), SpansetFunctions::Spanset_spans)
        );
        loader.RegisterFunction(
            ScalarFunction("splitNSpans", {spanset_type, LogicalType::INTEGER}, LogicalType::LIST(child_type), SpansetFunctions::Spanset_split_n_spans)
        );
        loader.RegisterFunction(
            ScalarFunction("splitEachNSpans", {spanset_type, LogicalType::INTEGER}, LogicalType::LIST(child_type), SpansetFunctions::Spanset_split_each_n_spans)
        );

        // Lowercase aliases matching MobilityDB's SQL surface
        loader.RegisterFunction(
            ScalarFunction("splitNspans", {spanset_type, LogicalType::INTEGER}, LogicalType::LIST(child_type), SpansetFunctions::Spanset_split_n_spans)
        );
        loader.RegisterFunction(
            ScalarFunction("splitEachNspans", {spanset_type, LogicalType::INTEGER}, LogicalType::LIST(child_type), SpansetFunctions::Spanset_split_each_n_spans)
        );

        // Hash
        loader.RegisterFunction(
            ScalarFunction("spanset_hash", {spanset_type}, LogicalType::UINTEGER, SpansetFunctions::Spanset_hash)
        );
        loader.RegisterFunction(
            ScalarFunction("spanset_hash_extended", {spanset_type, LogicalType::BIGINT}, LogicalType::UBIGINT, SpansetFunctions::Spanset_hash_extended)
        );

        // comparison operators
        loader.RegisterFunction(
            ScalarFunction("spanset_eq", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_eq)
        );

        loader.RegisterFunction(
            ScalarFunction("=", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_eq)
        );

        loader.RegisterFunction(
            ScalarFunction("spanset_ne", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_ne)
        );
        loader.RegisterFunction(
            ScalarFunction("<>", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_ne)
        );
        loader.RegisterFunction(
            ScalarFunction("spanset_le", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_le)
        );
        loader.RegisterFunction(
            ScalarFunction("<=", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_le)
        );
        loader.RegisterFunction(
            ScalarFunction("spanset_lt", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_lt)
        );
        loader.RegisterFunction(
            ScalarFunction("<", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_lt)
        );
        loader.RegisterFunction(
            ScalarFunction("spanset_ge", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_ge)
        );
        loader.RegisterFunction(
            ScalarFunction(">=", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_ge)
        );
        loader.RegisterFunction(
            ScalarFunction("spanset_gt", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_gt)
        );
        loader.RegisterFunction(
            ScalarFunction(">", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_gt)
        );
        loader.RegisterFunction(
            ScalarFunction("spanset_cmp", {spanset_type, spanset_type}, LogicalType::INTEGER, SpansetFunctions::Spanset_cmp)
        );
    }
    loader.RegisterFunction(
        ScalarFunction("duration", {SpansetTypes::datespanset()}, LogicalType::INTERVAL, SpansetFunctions::Datespanset_duration)
    );

    loader.RegisterFunction(
        ScalarFunction("duration", {SpansetTypes::tstzspanset()}, LogicalType::INTERVAL, SpansetFunctions::Tstzspanset_duration)
    );

    loader.RegisterFunction(
        ScalarFunction("duration", {SpansetTypes::datespanset(), LogicalType::BOOLEAN}, LogicalType::INTERVAL, SpansetFunctions::Datespanset_duration)
    );

    loader.RegisterFunction(
        ScalarFunction("duration", {SpansetTypes::tstzspanset(), LogicalType::BOOLEAN}, LogicalType::INTERVAL, SpansetFunctions::Tstzspanset_duration)
    );

    loader.RegisterFunction(
        ScalarFunction("numDates", {SpansetTypes::datespanset()}, LogicalType::INTEGER, SpansetFunctions::Datespanset_num_dates)
    );

    loader.RegisterFunction(
        ScalarFunction("startDate", {SpansetTypes::datespanset()}, LogicalType::DATE, SpansetFunctions::Datespanset_start_date)
    );

    loader.RegisterFunction(
        ScalarFunction("endDate", {SpansetTypes::datespanset()}, LogicalType::DATE, SpansetFunctions::Datespanset_end_date)
    );

    loader.RegisterFunction(
        ScalarFunction("dateN", {SpansetTypes::datespanset(), LogicalType::INTEGER}, LogicalType::DATE, SpansetFunctions::Datespanset_date_n)
    );

    loader.RegisterFunction(
        ScalarFunction("dates", {SpansetTypes::datespanset()}, SetTypes::dateset(), SpansetFunctions::Datespanset_dates)
    );

    loader.RegisterFunction(
        ScalarFunction("numTimestamps", {SpansetTypes::tstzspanset()}, LogicalType::INTEGER, SpansetFunctions::Tstzspanset_num_timestamps)
    );

    loader.RegisterFunction(
        ScalarFunction("startTimestamp", {SpansetTypes::tstzspanset()}, LogicalType::TIMESTAMP_TZ, SpansetFunctions::Tstzspanset_start_timestamptz)
    );

    loader.RegisterFunction(
        ScalarFunction("endTimestamp", {SpansetTypes::tstzspanset()}, LogicalType::TIMESTAMP_TZ, SpansetFunctions::Tstzspanset_end_timestamptz)
    );

    loader.RegisterFunction(
        ScalarFunction("timestampN", {SpansetTypes::tstzspanset(), LogicalType::INTEGER}, LogicalType::TIMESTAMP_TZ, SpansetFunctions::Tstzspanset_timestamptz_n)
    );

    loader.RegisterFunction(
        ScalarFunction("timestamps", {SpansetTypes::tstzspanset()}, SetTypes::tstzset(), SpansetFunctions::Tstzspanset_timestamps)
    );

    // time_distance — five overloads, all involving at least one
    // tstzspanset. Returns the time-axis distance in seconds.
    loader.RegisterFunction(ScalarFunction("time_distance",
        {LogicalType::TIMESTAMP_TZ, SpansetTypes::tstzspanset()}, LogicalType::DOUBLE,
        SpansetFunctions::Time_distance_ts_spanset));
    loader.RegisterFunction(ScalarFunction("time_distance",
        {SpanTypes::TSTZSPAN(), SpansetTypes::tstzspanset()}, LogicalType::DOUBLE,
        SpansetFunctions::Time_distance_span_spanset));
    loader.RegisterFunction(ScalarFunction("time_distance",
        {SpansetTypes::tstzspanset(), LogicalType::TIMESTAMP_TZ}, LogicalType::DOUBLE,
        SpansetFunctions::Time_distance_spanset_ts));
    loader.RegisterFunction(ScalarFunction("time_distance",
        {SpansetTypes::tstzspanset(), SpanTypes::TSTZSPAN()}, LogicalType::DOUBLE,
        SpansetFunctions::Time_distance_spanset_span));
    loader.RegisterFunction(ScalarFunction("time_distance",
        {SpansetTypes::tstzspanset(), SpansetTypes::tstzspanset()}, LogicalType::DOUBLE,
        SpansetFunctions::Time_distance_spanset_spanset));
}

} // namespace duckdb   
