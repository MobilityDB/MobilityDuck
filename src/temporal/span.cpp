#define MOBILITYDUCK_EXTENSION_TYPES

#include "temporal/span.hpp"
#include "temporal/span_functions.hpp"
#include "temporal/set.hpp"
#include "temporal/spanset.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/common/vector_operations/binary_executor.hpp"
#include "duckdb/common/vector_operations/generic_executor.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_util.hpp"

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

        ExtensionUtil::RegisterCastFunction(
            instance,
            SetTypes::intset(),
            SpanTypes::INTSPAN(),
            SpanFunctions::Set_to_span_cast // intset -> intspan
         );
        ExtensionUtil::RegisterCastFunction(
            instance,
            SetTypes::bigintset(),
            SpanTypes::BIGINTSPAN(),
            SpanFunctions::Set_to_span_cast // bigintset -> bigintspan
         );
        ExtensionUtil::RegisterCastFunction(
            instance,
            SetTypes::floatset(),
            SpanTypes::FLOATSPAN(),
            SpanFunctions::Set_to_span_cast // floatset -> floatspan
         );
        ExtensionUtil::RegisterCastFunction(
            instance,
            SetTypes::tstzset(),
            SpanTypes::TSTZSPAN(),
            SpanFunctions::Set_to_span_cast // tstzset -> tstzspan
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
            ScalarFunction("span", {base_type, base_type, LogicalType::BOOLEAN, LogicalType::BOOLEAN}, span_type,
                           SpanFunctions::Span_binary_constructor)
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


        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("span", {SetTypes::intset()},SpanTypes::INTSPAN(), SpanFunctions::Set_to_span)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("span", {SetTypes::bigintset()},SpanTypes::BIGINTSPAN(), SpanFunctions::Set_to_span)
        );
        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("span", {SetTypes::floatset()},SpanTypes::FLOATSPAN(), SpanFunctions::Set_to_span)
        );
        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("span", {SetTypes::tstzset()},SpanTypes::TSTZSPAN(), SpanFunctions::Set_to_span) 
        );
        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("span", {SetTypes::dateset()},SpanTypes::DATESPAN(), SpanFunctions::Set_to_span) 
        );

        if (span_type == SpanTypes::INTSPAN() ||span_type == SpanTypes::DATESPAN()){

            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("shift", {span_type, LogicalType::INTEGER}, span_type, SpanFunctions::Numspan_shift)
            ); 
            
            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("scale", {span_type, LogicalType::INTERVAL}, span_type, SpanFunctions::Numspan_scale)
            );
            ExtensionUtil::RegisterFunction(
                db,
                ScalarFunction("shiftScale", {span_type, LogicalType::INTEGER, LogicalType::INTEGER}, span_type,
                               SpanFunctions::Numspan_shift_scale));

        }
        else if( span_type == SpanTypes::BIGINTSPAN() ){
            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("shift", {span_type, LogicalType::BIGINT}, span_type, SpanFunctions::Numspan_shift)
            ); 
            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("expand", {span_type, LogicalType::INTERVAL}, span_type, SpanFunctions::Numspan_expand)
            );
            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("scale", {span_type, LogicalType::INTERVAL}, span_type, SpanFunctions::Numspan_scale)
            );
            ExtensionUtil::RegisterFunction(
                db,
                ScalarFunction("shiftScale", {span_type, LogicalType::BIGINT, LogicalType::BIGINT}, span_type, SpanFunctions::Numspan_shift_scale)
            );    
        }
        else if( span_type == SpanTypes::FLOATSPAN() ){
            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("shift", {span_type, LogicalType::DOUBLE}, span_type, SpanFunctions::Numspan_shift)
            ); 
            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("expand", {span_type, LogicalType::INTERVAL}, span_type, SpanFunctions::Numspan_expand)
            );
            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("scale", {span_type, LogicalType::INTERVAL}, span_type, SpanFunctions::Numspan_scale)
            );
            ExtensionUtil::RegisterFunction(
                db,
                ScalarFunction("shiftScale", {span_type, LogicalType::DOUBLE, LogicalType::DOUBLE}, span_type, SpanFunctions::Numspan_shift_scale)
            );

        }
        else if( span_type == SpanTypes::TSTZSPAN() ){
            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("shift", {span_type, LogicalType::INTERVAL}, span_type, SpanFunctions::Tstzspan_shift)
            ); 

            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("expand", {span_type, LogicalType::INTERVAL}, span_type, SpanFunctions::Tstzspan_expand)
            );

            ExtensionUtil::RegisterFunction(
                db, ScalarFunction("scale", {span_type, LogicalType::INTERVAL}, span_type, SpanFunctions::Tstzspan_scale)
            );
            ExtensionUtil::RegisterFunction(
                db,
                ScalarFunction("shiftScale", {span_type, LogicalType::INTERVAL, LogicalType::INTERVAL}, span_type, SpanFunctions::Tstzspan_shift_scale)
            );

        }      
        
        ExtensionUtil::RegisterFunction(
            db, ScalarFunction("lower", {span_type}, base_type, SpanFunctions::Span_lower));
        ExtensionUtil::RegisterFunction(
            db, ScalarFunction("upper", {span_type}, base_type, SpanFunctions::Span_upper));

        ExtensionUtil::RegisterFunction(
            db, ScalarFunction("lowerInc",{span_type}, LogicalType::BOOLEAN, SpanFunctions::Span_lower_inc));
        ExtensionUtil::RegisterFunction(
            db, ScalarFunction("upperInc",{span_type}, LogicalType::BOOLEAN, SpanFunctions::Span_upper_inc));
        }
    
    ExtensionUtil::RegisterFunction(
            db, 
            ScalarFunction("expand", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpanTypes::INTSPAN(), SpanFunctions::Numspan_expand)
    );
    ExtensionUtil::RegisterFunction(
            db, 
            ScalarFunction("expand", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpanTypes::BIGINTSPAN(), SpanFunctions::Numspan_expand)
    );
    ExtensionUtil::RegisterFunction(
            db, 
            ScalarFunction("expand", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpanTypes::FLOATSPAN(), SpanFunctions::Numspan_expand)
    );  
    ExtensionUtil::RegisterFunction(
            db, 
            ScalarFunction("expand", {SpanTypes::DATESPAN(), LogicalType::INTEGER}, SpanTypes::DATESPAN(), SpanFunctions::Numspan_expand)
    );
    ExtensionUtil::RegisterFunction(
            db, 
            ScalarFunction("expand", {SpanTypes::TSTZSPAN(), LogicalType::INTERVAL}, SpanTypes::TSTZSPAN(), SpanFunctions::Tstzspan_expand)
    );

    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("width", {SpanTypes::INTSPAN()}, LogicalType::INTEGER, SpanFunctions::Numspan_width)
    );
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("width", {SpanTypes::BIGINTSPAN()}, LogicalType::BIGINT, SpanFunctions::Numspan_width)
    );
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("width", {SpanTypes::FLOATSPAN()}, LogicalType::DOUBLE, SpanFunctions::Numspan_width)
    );

    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("duration", {SpanTypes::DATESPAN()}, LogicalType::INTERVAL, SpanFunctions::Datespan_duration)
    );
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("duration", {SpanTypes::TSTZSPAN()}, LogicalType::INTERVAL, SpanFunctions::Tstzspan_duration)
    );



    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("splitNSpans", {SetTypes::intset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::INTSPAN()), SpanFunctions::Set_split_n_spans));
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("splitNSpans", {SetTypes::bigintset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::BIGINTSPAN()), SpanFunctions::Set_split_n_spans));
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("splitNSpans", {SetTypes::floatset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::FLOATSPAN()), SpanFunctions::Set_split_n_spans));
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("splitNSpans", {SetTypes::dateset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::DATESPAN()), SpanFunctions::Set_split_n_spans));
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("splitNSpans", {SetTypes::tstzset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::TSTZSPAN()), SpanFunctions::Set_split_n_spans));
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("splitEachNSpans", {SetTypes::intset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::INTSPAN()), SpanFunctions::Set_split_each_n_spans));
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("splitEachNSpans", {SetTypes::bigintset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::BIGINTSPAN()), SpanFunctions::Set_split_each_n_spans));
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("splitEachNSpans", {SetTypes::floatset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::FLOATSPAN()), SpanFunctions::Set_split_each_n_spans));
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("splitEachNSpans", {SetTypes::dateset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::DATESPAN()), SpanFunctions::Set_split_each_n_spans));
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("splitEachNSpans", {SetTypes::tstzset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::TSTZSPAN()), SpanFunctions::Set_split_each_n_spans));
                
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("floor", {SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Floatspan_floor)
    );
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("ceil", {SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Floatspan_ceil)
    );
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("round", {LogicalType::DOUBLE}, LogicalType::DOUBLE, SpanFunctions::Float_round)
    );
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("round", {LogicalType::DOUBLE, LogicalType::INTEGER}, LogicalType::DOUBLE, SpanFunctions::Float_round)
    );
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("round", {SpanTypes::FLOATSPAN(), LogicalType::INTEGER}, SpanTypes::FLOATSPAN(), SpanFunctions::Floatspan_round)
    );
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("round", {SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Floatspan_round)
    );
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("degrees", {SpanTypes::FLOATSPAN(), LogicalType::BOOLEAN}, SpanTypes::FLOATSPAN(), SpanFunctions::Floatspan_degrees)
    );
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("degrees", {SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Floatspan_degrees)
    );
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("radians", {SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Floatspan_radians)
    );

    ExtensionUtil::RegisterFunction(
        db, 
        ScalarFunction("span_contains", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, 
        ScalarFunction("@>", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, 
        ScalarFunction("span_contains", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, 
        ScalarFunction("@>", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, 
        ScalarFunction("span_contains", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, 
        ScalarFunction("@>", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, 
        ScalarFunction("span_contains", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, 
        ScalarFunction("@>", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, 
        ScalarFunction("span_contains", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, 
        ScalarFunction("@>", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, 
        ScalarFunction("span_contains", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, 
        ScalarFunction("@>", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, 
        ScalarFunction("span_contains", {SpanTypes::DATESPAN(), LogicalType::DATE}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, 
        ScalarFunction("@>", {SpanTypes::DATESPAN(), LogicalType::DATE}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, 
        ScalarFunction("span_contains", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, 
        ScalarFunction("@>", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, 
        ScalarFunction("span_contains", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, 
        ScalarFunction("@>", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, 
        ScalarFunction("span_contains", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, 
        ScalarFunction("@>", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_span)
    );

    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_contained", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<@", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_contained", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<@", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_contained", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<@", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_contained", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<@", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_contained", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<@", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_contained", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<@", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_contained", {LogicalType::DATE, SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<@", {LogicalType::DATE, SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_contained", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<@", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_contained", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_value_span)     
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<@", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_value_span)     
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_contained", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<@", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overlaps", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overlaps_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&&", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overlaps_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overlaps", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overlaps_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&&", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overlaps_span_span)
    );              
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overlaps", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overlaps_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&&", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overlaps_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overlaps", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overlaps_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&&", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overlaps_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overlaps", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overlaps_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&&", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overlaps_span_span)
    );      
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_adjacent", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-|-", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_adjacent", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-|-", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_adjacent", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-|-", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_adjacent", {LogicalType::BIGINT,SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-|-", {LogicalType::BIGINT,SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_adjacent", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-|-", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_adjacent", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-|-", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_adjacent", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-|-", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_adjacent", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-|-", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_adjacent", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-|-", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_adjacent", {LogicalType::DATE, SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-|-", {LogicalType::DATE, SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_adjacent", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-|-", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_adjacent", {SpanTypes::DATESPAN(), LogicalType::DATE}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-|-", {SpanTypes::DATESPAN(), LogicalType::DATE}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_adjacent", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_value_span)   
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-|-", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_value_span)   
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_adjacent", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_span)    
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-|-", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_span)    
    );  
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_adjacent", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_value)    
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-|-", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_value)    
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_left", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpanTypes::INTSPAN(), SpanFunctions::Left_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<<", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpanTypes::INTSPAN(), SpanFunctions::Left_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_left", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Left_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<<", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Left_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_left", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Left_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<<", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Left_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_left", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpanTypes::BIGINTSPAN(), SpanFunctions::Left_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<<", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpanTypes::BIGINTSPAN(), SpanFunctions::Left_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_left", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Left_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<<", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Left_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_left", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Left_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<<", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Left_span_span)
    );  
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_left", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpanTypes::FLOATSPAN(), SpanFunctions::Left_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<<", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpanTypes::FLOATSPAN(), SpanFunctions::Left_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_left", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Left_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<<", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Left_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_left", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Left_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<<", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Left_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_left", {SpanTypes::DATESPAN(), LogicalType::DATE}, SpanTypes::DATESPAN(), SpanFunctions::Left_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<<#", {SpanTypes::DATESPAN(), LogicalType::DATE}, SpanTypes::DATESPAN(), SpanFunctions::Left_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_left", {LogicalType::DATE, SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Left_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<<#", {LogicalType::DATE, SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Left_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_left", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Left_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<<#", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Left_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_left", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, SpanTypes::TSTZSPAN(), SpanFunctions::Left_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<<#", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, SpanTypes::TSTZSPAN(), SpanFunctions::Left_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_left", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Left_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<<#", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Left_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_left", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Left_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<<#", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Left_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_right", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpanTypes::INTSPAN(), SpanFunctions::Right_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction(">>", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpanTypes::INTSPAN(), SpanFunctions::Right_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_right", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Right_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction(">>", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Right_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_right", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Right_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction(">>", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Right_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_right", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpanTypes::BIGINTSPAN(), SpanFunctions::Right_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction(">>", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpanTypes::BIGINTSPAN(), SpanFunctions::Right_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_right", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Right_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction(">>", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Right_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_right", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Right_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction(">>", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Right_span_span)
    );      
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_right", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpanTypes::FLOATSPAN(), SpanFunctions::Right_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction(">>", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpanTypes::FLOATSPAN(), SpanFunctions::Right_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_right", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Right_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction(">>", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Right_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_right", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Right_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction(">>", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Right_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_right", {SpanTypes::DATESPAN(), LogicalType::DATE}, SpanTypes::DATESPAN(), SpanFunctions::Right_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("#>>", {SpanTypes::DATESPAN(), LogicalType::DATE}, SpanTypes::DATESPAN(), SpanFunctions::Right_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_right", {LogicalType::DATE, SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Right_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("#>>", {LogicalType::DATE, SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Right_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_right", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Right_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("#>>", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Right_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_right", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, SpanTypes::TSTZSPAN(), SpanFunctions::Right_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("#>>", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, SpanTypes::TSTZSPAN(), SpanFunctions::Right_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_right", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Right_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("#>>", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Right_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_right", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Right_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("#>>", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Right_span_span)
    );  
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overleft", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpanTypes::INTSPAN(), SpanFunctions::Overleft_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&<", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpanTypes::INTSPAN(), SpanFunctions::Overleft_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overleft", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Overleft_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&<", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Overleft_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overleft", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Overleft_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&<", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Overleft_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overleft", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpanTypes::BIGINTSPAN(), SpanFunctions::Overleft_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&<", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpanTypes::BIGINTSPAN(), SpanFunctions::Overleft_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overleft", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Overleft_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&<", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Overleft_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overleft", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Overleft_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&<", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Overleft_span_span)
    );  
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overleft", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpanTypes::FLOATSPAN(), SpanFunctions::Overleft_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&<", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpanTypes::FLOATSPAN(), SpanFunctions::Overleft_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overleft", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Overleft_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&<", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Overleft_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overleft", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Overleft_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&<", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Overleft_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overleft", {SpanTypes::DATESPAN(), LogicalType::DATE}, SpanTypes::DATESPAN(), SpanFunctions::Overleft_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&<#", {SpanTypes::DATESPAN(), LogicalType::DATE}, SpanTypes::DATESPAN(), SpanFunctions::Overleft_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overleft", {LogicalType::DATE, SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Overleft_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&<#", {LogicalType::DATE, SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Overleft_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overleft", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Overleft_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&<#", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Overleft_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overleft", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, SpanTypes::TSTZSPAN(), SpanFunctions::Overleft_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&<", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, SpanTypes::TSTZSPAN(), SpanFunctions::Overleft_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overleft", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Overleft_value_span)
    );  
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&<", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Overleft_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overleft", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Overleft_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&<#", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Overleft_span_span)
    );
    
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overright", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpanTypes::INTSPAN(), SpanFunctions::Overright_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&>", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpanTypes::INTSPAN(), SpanFunctions::Overright_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overright", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Overright_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&>", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Overright_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overright", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Overright_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&>", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Overright_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overright", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpanTypes::BIGINTSPAN(), SpanFunctions::Overright_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&>", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpanTypes::BIGINTSPAN(), SpanFunctions::Overright_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overright", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Overright_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overright", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Overright_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&>", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Overright_span_span)
    );      
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overright", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpanTypes::FLOATSPAN(), SpanFunctions::Overright_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&>", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpanTypes::FLOATSPAN(), SpanFunctions::Overright_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overright", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Overright_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&>", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Overright_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overright", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Overright_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("&>", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Overright_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overright", {SpanTypes::DATESPAN(), LogicalType::DATE}, SpanTypes::DATESPAN(), SpanFunctions::Overright_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("#&>", {SpanTypes::DATESPAN(), LogicalType::DATE}, SpanTypes::DATESPAN(), SpanFunctions::Overright_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overright", {LogicalType::DATE, SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Overright_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("#&>", {LogicalType::DATE, SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Overright_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overright", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Overright_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("#&>", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Overright_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overright", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, SpanTypes::TSTZSPAN(), SpanFunctions::Overright_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("#&>", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, SpanTypes::TSTZSPAN(), SpanFunctions::Overright_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overright", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Overright_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("#&>", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Overright_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_overright", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Overright_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("#&>", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Overright_span_span)
    );  

    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_union", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpansetTypes::intspanset(), SpanFunctions::Union_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_union", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, SpansetTypes::intspanset(), SpanFunctions::Union_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_union", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Union_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("+", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpansetTypes::intspanset(), SpanFunctions::Union_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("+", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, SpansetTypes::intspanset(), SpanFunctions::Union_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("+", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Union_span_span)
    );

    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_union", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpansetTypes::bigintspanset(), SpanFunctions::Union_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_union", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, SpansetTypes::bigintspanset(), SpanFunctions::Union_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_union", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Union_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("+", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpansetTypes::bigintspanset(), SpanFunctions::Union_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("+", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, SpansetTypes::bigintspanset(), SpanFunctions::Union_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("+", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Union_span_span)
    );

    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_union", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpansetTypes::floatspanset(), SpanFunctions::Union_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_union", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, SpansetTypes::floatspanset(), SpanFunctions::Union_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_union", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Union_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("+", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpansetTypes::floatspanset(), SpanFunctions::Union_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("+", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, SpansetTypes::floatspanset(), SpanFunctions::Union_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("+", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Union_span_span)
    );

    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_union", {SpanTypes::DATESPAN(), LogicalType::DATE}, SpansetTypes::datespanset(), SpanFunctions::Union_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_union", {LogicalType::DATE, SpanTypes::DATESPAN()}, SpansetTypes::datespanset(), SpanFunctions::Union_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_union", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Union_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("+", {SpanTypes::DATESPAN(), LogicalType::DATE}, SpansetTypes::datespanset(), SpanFunctions::Union_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("+", {LogicalType::DATE, SpanTypes::DATESPAN()}, SpansetTypes::datespanset(), SpanFunctions::Union_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("+", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Union_span_span)
    );  

    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_union", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, SpansetTypes::tstzspanset(), SpanFunctions::Union_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_union", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, SpansetTypes::tstzspanset(), SpanFunctions::Union_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_union", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Union_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("+", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, SpansetTypes::tstzspanset(), SpanFunctions::Union_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("+", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, SpansetTypes::tstzspanset(), SpanFunctions::Union_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("+", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Union_span_span)  
    );  

    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_intersection", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpanTypes::INTSPAN(), SpanFunctions::Intersection_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_intersection", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Intersection_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_intersection", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Intersection_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("*", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpanTypes::INTSPAN(), SpanFunctions::Intersection_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("*", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Intersection_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("*", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Intersection_span_span)
    );

    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_intersection", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpanTypes::BIGINTSPAN(), SpanFunctions::Intersection_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_intersection", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Intersection_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_intersection", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Intersection_span_span) 
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("*", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpanTypes::BIGINTSPAN(), SpanFunctions::Intersection_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("*", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Intersection_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("*", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Intersection_span_span) 
    );

    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_intersection", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpanTypes::FLOATSPAN(), SpanFunctions::Intersection_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_intersection", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Intersection_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_intersection", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Intersection_span_span) 
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("*", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpanTypes::FLOATSPAN(), SpanFunctions::Intersection_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("*", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Intersection_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("*", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Intersection_span_span) 
    );  

    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_intersection", {SpanTypes::DATESPAN(), LogicalType::DATE}, SpanTypes::DATESPAN(), SpanFunctions::Intersection_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_intersection", {LogicalType::DATE, SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Intersection_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_intersection", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Intersection_span_span) 
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("*", {SpanTypes::DATESPAN(), LogicalType::DATE}, SpanTypes::DATESPAN(), SpanFunctions::Intersection_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("*", {LogicalType::DATE, SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Intersection_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("*", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Intersection_span_span) 
    );  
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_intersection", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, SpanTypes::TSTZSPAN(), SpanFunctions::Intersection_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_intersection", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Intersection_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_intersection", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Intersection_span_span) 
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("*", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, SpanTypes::TSTZSPAN(), SpanFunctions::Intersection_span_value)  
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("*", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Intersection_value_span)
    );  
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("*", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Intersection_span_span) 
    );
    
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_minus", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpansetTypes::intspanset(), SpanFunctions::Minus_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_minus", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, SpansetTypes::intspanset(), SpanFunctions::Minus_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_minus", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, SpansetTypes::intspanset(), SpanFunctions::Minus_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpansetTypes::intspanset(), SpanFunctions::Minus_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, SpansetTypes::intspanset(), SpanFunctions::Minus_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, SpansetTypes::intspanset(), SpanFunctions::Minus_span_span)
    );

    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_minus", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpansetTypes::bigintspanset(), SpanFunctions::Minus_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_minus", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, SpansetTypes::bigintspanset(), SpanFunctions::Minus_value_span)    
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_minus", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, SpansetTypes::bigintspanset(), SpanFunctions::Minus_span_span) 
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpansetTypes::bigintspanset(), SpanFunctions::Minus_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, SpansetTypes::bigintspanset(), SpanFunctions::Minus_value_span)    
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, SpansetTypes::bigintspanset(), SpanFunctions::Minus_span_span) 
    );

    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_minus", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpansetTypes::floatspanset(), SpanFunctions::Minus_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_minus", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, SpansetTypes::floatspanset(), SpanFunctions::Minus_value_span)    
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_minus", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, SpansetTypes::floatspanset(), SpanFunctions::Minus_span_span) 
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpansetTypes::floatspanset(), SpanFunctions::Minus_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, SpansetTypes::floatspanset(), SpanFunctions::Minus_value_span)    
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, SpansetTypes::floatspanset(), SpanFunctions::Minus_span_span) 
    );

    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_minus", {SpanTypes::DATESPAN(), LogicalType::DATE}, SpansetTypes::datespanset(), SpanFunctions::Minus_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_minus", {LogicalType::DATE, SpanTypes::DATESPAN()}, SpansetTypes::datespanset(), SpanFunctions::Minus_value_span)    
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_minus", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, SpansetTypes::datespanset(), SpanFunctions::Minus_span_span) 
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-", {SpanTypes::DATESPAN(), LogicalType::DATE}, SpansetTypes::datespanset(), SpanFunctions::Minus_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-", {LogicalType::DATE, SpanTypes::DATESPAN()}, SpansetTypes::datespanset(), SpanFunctions::Minus_value_span)    
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, SpansetTypes::datespanset(), SpanFunctions::Minus_span_span) 
    );

    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_minus", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, SpansetTypes::tstzspanset(), SpanFunctions::Minus_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_minus", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, SpansetTypes::tstzspanset(), SpanFunctions::Minus_value_span)    
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_minus", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, SpansetTypes::tstzspanset(), SpanFunctions::Minus_span_span) 
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, SpansetTypes::tstzspanset(), SpanFunctions::Minus_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, SpansetTypes::tstzspanset(), SpanFunctions::Minus_value_span)    
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("-", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, SpansetTypes::tstzspanset(), SpanFunctions::Minus_span_span) 
    );

    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_distance", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, LogicalType::INTEGER, SpanFunctions::Distance_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_distance", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, LogicalType::INTEGER, SpanFunctions::Distance_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_distance", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::INTEGER, SpanFunctions::Distance_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<->", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, LogicalType::INTEGER, SpanFunctions::Distance_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<->", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, LogicalType::INTEGER, SpanFunctions::Distance_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<->", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::INTEGER, SpanFunctions::Distance_span_span)
    );

    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_distance", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, LogicalType::BIGINT, SpanFunctions::Distance_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_distance", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, LogicalType::BIGINT, SpanFunctions::Distance_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_distance", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BIGINT, SpanFunctions::Distance_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<->", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, LogicalType::BIGINT, SpanFunctions::Distance_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<->", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, LogicalType::BIGINT, SpanFunctions::Distance_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<->", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BIGINT, SpanFunctions::Distance_span_span)
    );

    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_distance", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, LogicalType::DOUBLE, SpanFunctions::Distance_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_distance", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, LogicalType::DOUBLE, SpanFunctions::Distance_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_distance", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::DOUBLE, SpanFunctions::Distance_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<->", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, LogicalType::DOUBLE, SpanFunctions::Distance_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<->", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, LogicalType::DOUBLE, SpanFunctions::Distance_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<->", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::DOUBLE, SpanFunctions::Distance_span_span)
    );  

    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_distance", {SpanTypes::DATESPAN(), LogicalType::DATE}, LogicalType::INTEGER, SpanFunctions::Distance_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_distance", {LogicalType::DATE, SpanTypes::DATESPAN()}, LogicalType::INTEGER, SpanFunctions::Distance_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_distance", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::INTEGER, SpanFunctions::Distance_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<->", {SpanTypes::DATESPAN(), LogicalType::DATE}, LogicalType::INTEGER, SpanFunctions::Distance_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<->", {LogicalType::DATE, SpanTypes::DATESPAN()}, LogicalType::INTEGER, SpanFunctions::Distance_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<->", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::INTEGER, SpanFunctions::Distance_span_span)
    );

    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_distance", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, LogicalType::INTERVAL, SpanFunctions::Distance_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_distance", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, LogicalType::INTERVAL, SpanFunctions::Distance_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("span_distance", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::INTERVAL, SpanFunctions::Distance_span_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<->", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, LogicalType::INTERVAL, SpanFunctions::Distance_span_value)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<->", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, LogicalType::INTERVAL, SpanFunctions::Distance_value_span)
    );
    ExtensionUtil::RegisterFunction(
        db, ScalarFunction("<->", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::INTERVAL, SpanFunctions::Distance_span_span)
    );
}


} // namespace duckdb

#ifndef MOBILITYDUCK_EXTENSION_TYPES
#endif