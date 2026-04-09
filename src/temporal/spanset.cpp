#include "temporal/spanset.hpp"
#include "temporal/spanset_functions.hpp"
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

        if (spanset_type == SpansetTypes::intspanset() ||spanset_type == SpansetTypes::datespanset()){

            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("shift", {spanset_type, LogicalType::INTEGER}, spanset_type, SpansetFunctions::Numspanset_shift)
            ); 
            
            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("scale", {spanset_type, LogicalType::INTERVAL}, spanset_type, SpansetFunctions::Numspanset_scale)
            );

            ExtensionUtil::RegisterFunction(
                db,
                ScalarFunction("shiftScale", {spanset_type, LogicalType::INTEGER, LogicalType::INTEGER}, spanset_type,
                               SpansetFunctions::Numspanset_shift_scale));

        }
        else if( spanset_type == SpansetTypes::bigintspanset() ){
            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("shift", {spanset_type, LogicalType::BIGINT}, spanset_type, SpansetFunctions::Numspanset_shift)
            ); 

            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("scale", {spanset_type, LogicalType::INTERVAL}, spanset_type, SpansetFunctions::Numspanset_scale)
            );
            ExtensionUtil::RegisterFunction(
                db,
                ScalarFunction("shiftScale", {spanset_type, LogicalType::BIGINT, LogicalType::BIGINT}, spanset_type, SpansetFunctions::Numspanset_shift_scale)
            );    
        }
        else if( spanset_type == SpansetTypes::floatspanset() ){
            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("shift", {spanset_type, LogicalType::DOUBLE}, spanset_type, SpansetFunctions::Numspanset_shift)
            ); 
            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("scale", {spanset_type, LogicalType::INTERVAL}, spanset_type, SpansetFunctions::Numspanset_scale)
            );
            ExtensionUtil::RegisterFunction(
                db,
                ScalarFunction("shiftScale", {spanset_type, LogicalType::DOUBLE, LogicalType::DOUBLE}, spanset_type, SpansetFunctions::Numspanset_shift_scale)
            );

            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("floor", {spanset_type}, spanset_type, SpansetFunctions::Floatspanset_floor)
            );
            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("ceil", {spanset_type}, spanset_type, SpansetFunctions::Floatspanset_ceil)
            );
            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("round", {spanset_type}, spanset_type, SpansetFunctions::Floatspanset_round)
            );

            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("round", {spanset_type, LogicalType::INTEGER}, spanset_type, SpansetFunctions::Floatspanset_round)
            );
            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("degrees", {spanset_type}, spanset_type, SpansetFunctions::Floatspanset_degrees)
            );
            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("radians", {spanset_type}, spanset_type, SpansetFunctions::Floatspanset_radians)
            );

        }
        else if( spanset_type == SpansetTypes::tstzspanset() ){
            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("shift", {spanset_type, LogicalType::INTERVAL}, spanset_type, SpansetFunctions::Tstzspanset_shift)
            ); 

            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("scale", {spanset_type, LogicalType::INTERVAL}, spanset_type, SpansetFunctions::Tstzspanset_scale)
            );
            ExtensionUtil::RegisterFunction(
                db,
                ScalarFunction("shiftScale", {spanset_type, LogicalType::INTERVAL, LogicalType::INTERVAL}, spanset_type, SpansetFunctions::Tstzspanset_shift_scale)
            );

        }      
    }
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

    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("numDates", {SpansetTypes::datespanset()}, LogicalType::INTEGER, SpansetFunctions::Datespanset_num_dates)
    );

    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("startDate", {SpansetTypes::datespanset()}, LogicalType::DATE, SpansetFunctions::Datespanset_start_date)
    );

    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("endDate", {SpansetTypes::datespanset()}, LogicalType::DATE, SpansetFunctions::Datespanset_end_date)
    );

    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("dateN", {SpansetTypes::datespanset(), LogicalType::INTEGER}, LogicalType::DATE, SpansetFunctions::Datespanset_date_n)
    );

    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("dates", {SpansetTypes::datespanset()}, SetTypes::dateset(), SpansetFunctions::Datespanset_dates)
    );

    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("numTimestamps", {SpansetTypes::tstzspanset()}, LogicalType::INTEGER, SpansetFunctions::Tstzspanset_num_timestamps)
    );

    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("startTimestamp", {SpansetTypes::tstzspanset()}, LogicalType::TIMESTAMP_TZ, SpansetFunctions::Tstzspanset_start_timestamptz)
    );

    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("endTimestamp", {SpansetTypes::tstzspanset()}, LogicalType::TIMESTAMP_TZ, SpansetFunctions::Tstzspanset_end_timestamptz)
    );

    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("timestampN", {SpansetTypes::tstzspanset(), LogicalType::INTEGER}, LogicalType::TIMESTAMP_TZ, SpansetFunctions::Tstzspanset_timestamptz_n)
    );

    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("timestamps", {SpansetTypes::tstzspanset()}, SetTypes::tstzset(), SpansetFunctions::Tstzspanset_timestamps)
    );


}

} // namespace duckdb   
