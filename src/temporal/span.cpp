#define MOBILITYDUCK_EXTENSION_TYPES

#include "temporal/span.hpp"
#include "temporal/span_functions.hpp"
#include "temporal/set.hpp"
#include "temporal/spanset.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/common/vector_operations/binary_executor.hpp"
#include "duckdb/common/vector_operations/generic_executor.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "time_util.hpp"

#include <regex>
#include <string>
#include "mobilityduck/meos_exec_serial.hpp"

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

void SpanTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType( "INTSPAN", INTSPAN());
    loader.RegisterType( "BIGINTSPAN", BIGINTSPAN());
    loader.RegisterType( "FLOATSPAN", FLOATSPAN());
    loader.RegisterType( "DATESPAN", DATESPAN());
    loader.RegisterType( "TSTZSPAN", TSTZSPAN());    
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

MeosType SpanTypeMapping::GetMeosTypeFromAlias(const std::string &alias) {
    static const std::unordered_map<std::string, MeosType> alias_to_type = {
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
void SpanTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    for (const auto &span_type : SpanTypes::AllTypes()) {
        RegisterMeosCastFunction(loader, 
            span_type,                      
            LogicalType::VARCHAR,   
            SpanFunctions::Span_to_text   
        ); // Blob to text
        RegisterMeosCastFunction(loader, 
            LogicalType::VARCHAR, 
            span_type,                                    
            SpanFunctions::Text_to_span   
        ); // text to blob
        
        RegisterMeosCastFunction(loader, 
            SpanTypes::INTSPAN(),
            SpanTypes::FLOATSPAN(),
            SpanFunctions::Intspan_to_floatspan_cast // intspan -> floatspan 
        );

        RegisterMeosCastFunction(loader, 
            SpanTypes::FLOATSPAN(),
            SpanTypes::INTSPAN(),
            SpanFunctions::Floatspan_to_intspan_cast // floatspan -> intspan
        );
        
        RegisterMeosCastFunction(loader, 
            SpanTypes::DATESPAN(),
            SpanTypes::TSTZSPAN(),
            SpanFunctions::Datespan_to_tstzspan_cast // datespan -> tstzspan
        );
        
        RegisterMeosCastFunction(loader, 
            SpanTypes::TSTZSPAN(),
            SpanTypes::DATESPAN(),
            SpanFunctions::Tstzspan_to_datespan_cast // tstzspan -> datespan 
        );

        RegisterMeosCastFunction(loader, 
            SetTypes::intset(),
            SpanTypes::INTSPAN(),
            SpanFunctions::Set_to_span_cast // intset -> intspan
         );
        RegisterMeosCastFunction(loader, 
            SetTypes::bigintset(),
            SpanTypes::BIGINTSPAN(),
            SpanFunctions::Set_to_span_cast // bigintset -> bigintspan
         );
        RegisterMeosCastFunction(loader, 
            SetTypes::floatset(),
            SpanTypes::FLOATSPAN(),
            SpanFunctions::Set_to_span_cast // floatset -> floatspan
         );
        RegisterMeosCastFunction(loader, 
            SetTypes::tstzset(),
            SpanTypes::TSTZSPAN(),
            SpanFunctions::Set_to_span_cast // tstzset -> tstzspan
         );

        // Scalar value -> span casts
        RegisterMeosCastFunction(loader, LogicalType::INTEGER,      SpanTypes::INTSPAN(),    SpanFunctions::Value_to_span_cast);
        RegisterMeosCastFunction(loader, LogicalType::BIGINT,       SpanTypes::BIGINTSPAN(), SpanFunctions::Value_to_span_cast);
        RegisterMeosCastFunction(loader, LogicalType::DOUBLE,       SpanTypes::FLOATSPAN(),  SpanFunctions::Value_to_span_cast);
        RegisterMeosCastFunction(loader, LogicalType::DATE,         SpanTypes::DATESPAN(),   SpanFunctions::Value_to_span_cast);
        RegisterMeosCastFunction(loader, LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN(),   SpanFunctions::Value_to_span_cast);
    }
}

void SpanTypes::RegisterScalarFunctions(ExtensionLoader &loader) {    
    for (const auto &span_type : SpanTypes::AllTypes()) {
        auto base_type = SpanTypeMapping::GetChildType(span_type);         

        // Register: asText
        if (span_type == SpanTypes::FLOATSPAN()) {            
            duckdb::RegisterSerializedScalarFunction(loader,  // asText(floatspan)
                ScalarFunction("asText", {span_type}, LogicalType::VARCHAR, SpanFunctions::Span_as_text)
            );
            
            duckdb::RegisterSerializedScalarFunction(loader,  // asText(floatspan, int)
                ScalarFunction("asText", {span_type, LogicalType::INTEGER}, LogicalType::VARCHAR, SpanFunctions::Span_as_text)
            );
        } else {            
            duckdb::RegisterSerializedScalarFunction(loader,  // All other span types
                ScalarFunction("asText", {span_type}, LogicalType::VARCHAR, SpanFunctions::Span_as_text)
            );
        }

        // asBinary / asHexWKB and the *FromBinary / *FromHexWKB constructors.
        // span_as_wkb / span_from_wkb are subtype-agnostic; the format
        // encodes the span type, so each per-type FromBinary alias routes
        // to the same executor.
        const std::string sp_alias = StringUtil::Lower(span_type.ToString());
        duckdb::RegisterSerializedScalarFunction(loader,
            ScalarFunction("asBinary", {span_type}, LogicalType::BLOB,    SpanFunctions::Span_as_binary));
        duckdb::RegisterSerializedScalarFunction(loader,
            ScalarFunction("asHexWKB", {span_type}, LogicalType::VARCHAR, SpanFunctions::Span_as_hexwkb));
        duckdb::RegisterSerializedScalarFunction(loader,
            ScalarFunction(sp_alias + "FromBinary", {LogicalType::BLOB},    span_type, SpanFunctions::Span_from_binary));
        duckdb::RegisterSerializedScalarFunction(loader,
            ScalarFunction(sp_alias + "FromHexWKB", {LogicalType::VARCHAR}, span_type, SpanFunctions::Span_from_hexwkb));

        // Register span constructor functions
        duckdb::RegisterSerializedScalarFunction(loader,
            ScalarFunction(span_type.ToString(), {LogicalType::VARCHAR}, span_type, SpanFunctions::Span_constructor)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("span", {base_type, base_type}, span_type, SpanFunctions::Span_binary_constructor)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("span", {base_type, base_type, LogicalType::BOOLEAN, LogicalType::BOOLEAN}, span_type,
                           SpanFunctions::Span_binary_constructor)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("span", {base_type}, span_type, SpanFunctions::Value_to_span)                 
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("intspan", {SpanTypes::FLOATSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Floatspan_to_intspan)                 
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("floatspan", {SpanTypes::INTSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Intspan_to_floatspan)                 
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("datespan", {SpanTypes::TSTZSPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Tstzspan_to_datespan)                 
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("tstzspan", {SpanTypes::DATESPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Datespan_to_tstzspan)                 
        );


        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("span", {SetTypes::intset()},SpanTypes::INTSPAN(), SpanFunctions::Set_to_span)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("span", {SetTypes::bigintset()},SpanTypes::BIGINTSPAN(), SpanFunctions::Set_to_span)
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("span", {SetTypes::floatset()},SpanTypes::FLOATSPAN(), SpanFunctions::Set_to_span)
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("span", {SetTypes::tstzset()},SpanTypes::TSTZSPAN(), SpanFunctions::Set_to_span) 
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("span", {SetTypes::dateset()},SpanTypes::DATESPAN(), SpanFunctions::Set_to_span) 
        );

        if (span_type == SpanTypes::INTSPAN() ||span_type == SpanTypes::DATESPAN()){

            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("shift", {span_type, LogicalType::INTEGER}, span_type, SpanFunctions::Numspan_shift)
            ); 
            
            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("scale", {span_type, LogicalType::INTERVAL}, span_type, SpanFunctions::Numspan_scale)
            );
            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction("shiftScale", {span_type, LogicalType::INTEGER, LogicalType::INTEGER}, span_type,
                               SpanFunctions::Numspan_shift_scale));

        }
        else if( span_type == SpanTypes::BIGINTSPAN() ){
            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("shift", {span_type, LogicalType::BIGINT}, span_type, SpanFunctions::Numspan_shift)
            ); 
            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("expand", {span_type, LogicalType::INTERVAL}, span_type, SpanFunctions::Numspan_expand)
            );
            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("scale", {span_type, LogicalType::INTERVAL}, span_type, SpanFunctions::Numspan_scale)
            );
            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction("shiftScale", {span_type, LogicalType::BIGINT, LogicalType::BIGINT}, span_type, SpanFunctions::Numspan_shift_scale)
            );    
        }
        else if( span_type == SpanTypes::FLOATSPAN() ){
            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("shift", {span_type, LogicalType::DOUBLE}, span_type, SpanFunctions::Numspan_shift)
            ); 
            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("expand", {span_type, LogicalType::INTERVAL}, span_type, SpanFunctions::Numspan_expand)
            );
            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("scale", {span_type, LogicalType::INTERVAL}, span_type, SpanFunctions::Numspan_scale)
            );
            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction("shiftScale", {span_type, LogicalType::DOUBLE, LogicalType::DOUBLE}, span_type, SpanFunctions::Numspan_shift_scale)
            );

        }
        else if( span_type == SpanTypes::TSTZSPAN() ){
            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("shift", {span_type, LogicalType::INTERVAL}, span_type, SpanFunctions::Tstzspan_shift)
            );
            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {span_type, LogicalType::INTERVAL}, span_type, SpanFunctions::Tstzspan_shift)
            );

            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("expand", {span_type, LogicalType::INTERVAL}, span_type, SpanFunctions::Tstzspan_expand)
            );

            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("scale", {span_type, LogicalType::INTERVAL}, span_type, SpanFunctions::Tstzspan_scale)
            );
            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction("shiftScale", {span_type, LogicalType::INTERVAL, LogicalType::INTERVAL}, span_type, SpanFunctions::Tstzspan_shift_scale)
            );

        }      
        
        duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("lower", {span_type}, base_type, SpanFunctions::Span_lower));
        duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("upper", {span_type}, base_type, SpanFunctions::Span_upper));

        duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("lowerInc",{span_type}, LogicalType::BOOLEAN, SpanFunctions::Span_lower_inc));
        duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("upperInc",{span_type}, LogicalType::BOOLEAN, SpanFunctions::Span_upper_inc));

        duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_hash", {span_type}, LogicalType::UINTEGER, SpanFunctions::Span_hash));
        duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_hash_extended", {span_type, LogicalType::BIGINT}, LogicalType::UBIGINT, SpanFunctions::Span_hash_extended));
        }
    
    duckdb::RegisterSerializedScalarFunction(loader,  
            ScalarFunction("expand", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpanTypes::INTSPAN(), SpanFunctions::Numspan_expand)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
            ScalarFunction("expand", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpanTypes::BIGINTSPAN(), SpanFunctions::Numspan_expand)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
            ScalarFunction("expand", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpanTypes::FLOATSPAN(), SpanFunctions::Numspan_expand)
    );  
    duckdb::RegisterSerializedScalarFunction(loader,  
            ScalarFunction("expand", {SpanTypes::DATESPAN(), LogicalType::INTEGER}, SpanTypes::DATESPAN(), SpanFunctions::Numspan_expand)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
            ScalarFunction("expand", {SpanTypes::TSTZSPAN(), LogicalType::INTERVAL}, SpanTypes::TSTZSPAN(), SpanFunctions::Tstzspan_expand)
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("width", {SpanTypes::INTSPAN()}, LogicalType::INTEGER, SpanFunctions::Numspan_width)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("width", {SpanTypes::BIGINTSPAN()}, LogicalType::BIGINT, SpanFunctions::Numspan_width)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("width", {SpanTypes::FLOATSPAN()}, LogicalType::DOUBLE, SpanFunctions::Numspan_width)
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("duration", {SpanTypes::DATESPAN()}, LogicalType::INTERVAL, SpanFunctions::Datespan_duration)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("duration", {SpanTypes::TSTZSPAN()}, LogicalType::INTERVAL, SpanFunctions::Tstzspan_duration)
    );



    // spans(<set_type>) — list of unit spans, one per set element
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("spans", {SetTypes::intset()},
                       LogicalType::LIST(SpanTypes::INTSPAN()), SpanFunctions::Set_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("spans", {SetTypes::bigintset()},
                       LogicalType::LIST(SpanTypes::BIGINTSPAN()), SpanFunctions::Set_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("spans", {SetTypes::floatset()},
                       LogicalType::LIST(SpanTypes::FLOATSPAN()), SpanFunctions::Set_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("spans", {SetTypes::dateset()},
                       LogicalType::LIST(SpanTypes::DATESPAN()), SpanFunctions::Set_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("spans", {SetTypes::tstzset()},
                       LogicalType::LIST(SpanTypes::TSTZSPAN()), SpanFunctions::Set_spans));

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitNSpans", {SetTypes::intset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::INTSPAN()), SpanFunctions::Set_split_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitNSpans", {SetTypes::bigintset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::BIGINTSPAN()), SpanFunctions::Set_split_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitNSpans", {SetTypes::floatset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::FLOATSPAN()), SpanFunctions::Set_split_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitNSpans", {SetTypes::dateset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::DATESPAN()), SpanFunctions::Set_split_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitNSpans", {SetTypes::tstzset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::TSTZSPAN()), SpanFunctions::Set_split_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitEachNSpans", {SetTypes::intset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::INTSPAN()), SpanFunctions::Set_split_each_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitEachNSpans", {SetTypes::bigintset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::BIGINTSPAN()), SpanFunctions::Set_split_each_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitEachNSpans", {SetTypes::floatset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::FLOATSPAN()), SpanFunctions::Set_split_each_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitEachNSpans", {SetTypes::dateset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::DATESPAN()), SpanFunctions::Set_split_each_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitEachNSpans", {SetTypes::tstzset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::TSTZSPAN()), SpanFunctions::Set_split_each_n_spans));

    // Lowercase-"spans" aliases matching MobilityDB's SQL surface
    // (`splitNspans` / `splitEachNspans`). The camelCase forms above
    // stay registered for back-compat with existing MobilityDuck callers.
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitNspans", {SetTypes::intset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::INTSPAN()), SpanFunctions::Set_split_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitNspans", {SetTypes::bigintset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::BIGINTSPAN()), SpanFunctions::Set_split_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitNspans", {SetTypes::floatset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::FLOATSPAN()), SpanFunctions::Set_split_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitNspans", {SetTypes::dateset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::DATESPAN()), SpanFunctions::Set_split_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitNspans", {SetTypes::tstzset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::TSTZSPAN()), SpanFunctions::Set_split_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitEachNspans", {SetTypes::intset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::INTSPAN()), SpanFunctions::Set_split_each_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitEachNspans", {SetTypes::bigintset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::BIGINTSPAN()), SpanFunctions::Set_split_each_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitEachNspans", {SetTypes::floatset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::FLOATSPAN()), SpanFunctions::Set_split_each_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitEachNspans", {SetTypes::dateset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::DATESPAN()), SpanFunctions::Set_split_each_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitEachNspans", {SetTypes::tstzset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::TSTZSPAN()), SpanFunctions::Set_split_each_n_spans));

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("floor", {SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Floatspan_floor)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("ceil", {SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Floatspan_ceil)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("round", {LogicalType::DOUBLE}, LogicalType::DOUBLE, SpanFunctions::Float_round)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("round", {LogicalType::DOUBLE, LogicalType::INTEGER}, LogicalType::DOUBLE, SpanFunctions::Float_round)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("round", {SpanTypes::FLOATSPAN(), LogicalType::INTEGER}, SpanTypes::FLOATSPAN(), SpanFunctions::Floatspan_round)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("round", {SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Floatspan_round)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("degrees", {SpanTypes::FLOATSPAN(), LogicalType::BOOLEAN}, SpanTypes::FLOATSPAN(), SpanFunctions::Floatspan_degrees)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("degrees", {SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Floatspan_degrees)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("radians", {SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Floatspan_radians)
    );

    for (const auto &span_type : SpanTypes::AllTypes()) {
        duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("span_eq", {span_type, span_type}, LogicalType::BOOLEAN, SpanFunctions::Span_eq)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("span_ne", {span_type, span_type}, LogicalType::BOOLEAN, SpanFunctions::Span_ne)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("span_lt", {span_type, span_type}, LogicalType::BOOLEAN, SpanFunctions::Span_lt)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("span_le", {span_type, span_type}, LogicalType::BOOLEAN, SpanFunctions::Span_le)
    );
    
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("span_ge", {span_type, span_type}, LogicalType::BOOLEAN, SpanFunctions::Span_ge)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("span_gt", {span_type, span_type}, LogicalType::BOOLEAN, SpanFunctions::Span_gt)
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("span_cmp", {span_type, span_type}, LogicalType::INTEGER, SpanFunctions::Span_cmp)
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("=", {span_type, span_type}, LogicalType::BOOLEAN, SpanFunctions::Span_eq)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("<>", {span_type, span_type}, LogicalType::BOOLEAN, SpanFunctions::Span_ne)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("<", {span_type, span_type}, LogicalType::BOOLEAN, SpanFunctions::Span_lt)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("<=", {span_type, span_type}, LogicalType::BOOLEAN, SpanFunctions::Span_le)
    );
    
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(">=", {span_type, span_type}, LogicalType::BOOLEAN, SpanFunctions::Span_ge)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(">", {span_type, span_type}, LogicalType::BOOLEAN, SpanFunctions::Span_gt)
    );
    }

    duckdb::RegisterSerializedScalarFunction(loader,  
        ScalarFunction("span_contains", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
        ScalarFunction("@>", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
        ScalarFunction("span_contains", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
        ScalarFunction("@>", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
        ScalarFunction("span_contains", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
        ScalarFunction("@>", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
        ScalarFunction("span_contains", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
        ScalarFunction("@>", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
        ScalarFunction("span_contains", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
        ScalarFunction("@>", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
        ScalarFunction("span_contains", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
        ScalarFunction("@>", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
        ScalarFunction("span_contains", {SpanTypes::DATESPAN(), LogicalType::DATE}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
        ScalarFunction("@>", {SpanTypes::DATESPAN(), LogicalType::DATE}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
        ScalarFunction("span_contains", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
        ScalarFunction("@>", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
        ScalarFunction("span_contains", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
        ScalarFunction("@>", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
        ScalarFunction("span_contains", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
        ScalarFunction("@>", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contains_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_contained", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_contained", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_contained", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_contained", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_contained", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_contained", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_contained", {LogicalType::DATE, SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {LogicalType::DATE, SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_contained", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_contained", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_value_span)     
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_value_span)     
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_contained", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Contained_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overlaps", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overlaps_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&&", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overlaps_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overlaps", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overlaps_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&&", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overlaps_span_span)
    );              
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overlaps", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overlaps_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&&", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overlaps_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overlaps", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overlaps_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&&", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overlaps_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overlaps", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overlaps_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&&", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overlaps_span_span)
    );      
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_adjacent", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-|-", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_adjacent", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-|-", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_adjacent", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-|-", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_adjacent", {LogicalType::BIGINT,SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-|-", {LogicalType::BIGINT,SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_adjacent", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-|-", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_adjacent", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-|-", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_adjacent", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-|-", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_adjacent", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-|-", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_adjacent", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-|-", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_adjacent", {LogicalType::DATE, SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-|-", {LogicalType::DATE, SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_adjacent", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-|-", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_adjacent", {SpanTypes::DATESPAN(), LogicalType::DATE}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-|-", {SpanTypes::DATESPAN(), LogicalType::DATE}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_adjacent", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_value_span)   
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-|-", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_value_span)   
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_adjacent", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_span)    
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-|-", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_span)    
    );  
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_adjacent", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_value)    
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-|-", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SpanFunctions::Adjacent_span_value)    
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_left", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SpanFunctions::Left_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SpanFunctions::Left_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_left", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Left_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Left_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_left", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Left_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Left_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_left", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SpanFunctions::Left_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SpanFunctions::Left_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_left", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Left_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Left_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_left", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Left_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Left_span_span)
    );  
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_left", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SpanFunctions::Left_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SpanFunctions::Left_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_left", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Left_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Left_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_left", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Left_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Left_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_left", {SpanTypes::DATESPAN(), LogicalType::DATE}, LogicalType::BOOLEAN, SpanFunctions::Left_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<#", {SpanTypes::DATESPAN(), LogicalType::DATE}, LogicalType::BOOLEAN, SpanFunctions::Left_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_left", {LogicalType::DATE, SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Left_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<#", {LogicalType::DATE, SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Left_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_left", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Left_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<#", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Left_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_left", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SpanFunctions::Left_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<#", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SpanFunctions::Left_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_left", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Left_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<#", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Left_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_left", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Left_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<#", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Left_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_right", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SpanFunctions::Right_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SpanFunctions::Right_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_right", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Right_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Right_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_right", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Right_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Right_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_right", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SpanFunctions::Right_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SpanFunctions::Right_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_right", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Right_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Right_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_right", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Right_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Right_span_span)
    );      
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_right", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SpanFunctions::Right_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SpanFunctions::Right_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_right", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Right_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Right_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_right", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Right_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Right_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_right", {SpanTypes::DATESPAN(), LogicalType::DATE}, LogicalType::BOOLEAN, SpanFunctions::Right_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("#>>", {SpanTypes::DATESPAN(), LogicalType::DATE}, LogicalType::BOOLEAN, SpanFunctions::Right_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_right", {LogicalType::DATE, SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Right_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("#>>", {LogicalType::DATE, SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Right_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_right", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Right_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("#>>", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Right_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_right", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SpanFunctions::Right_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("#>>", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SpanFunctions::Right_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_right", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Right_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("#>>", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Right_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_right", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Right_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("#>>", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Right_span_span)
    );  
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overleft", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SpanFunctions::Overleft_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SpanFunctions::Overleft_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overleft", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overleft_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overleft_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overleft", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overleft_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overleft_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overleft", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SpanFunctions::Overleft_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SpanFunctions::Overleft_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overleft", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overleft_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overleft_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overleft", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overleft_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overleft_span_span)
    );  
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overleft", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SpanFunctions::Overleft_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SpanFunctions::Overleft_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overleft", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overleft_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overleft_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overleft", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overleft_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overleft_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overleft", {SpanTypes::DATESPAN(), LogicalType::DATE}, LogicalType::BOOLEAN, SpanFunctions::Overleft_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<#", {SpanTypes::DATESPAN(), LogicalType::DATE}, LogicalType::BOOLEAN, SpanFunctions::Overleft_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overleft", {LogicalType::DATE, SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overleft_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<#", {LogicalType::DATE, SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overleft_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overleft", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overleft_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<#", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overleft_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overleft", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SpanFunctions::Overleft_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SpanFunctions::Overleft_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overleft", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overleft_value_span)
    );  
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overleft_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overleft", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overleft_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<#", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overleft_span_span)
    );
    
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overright", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SpanFunctions::Overright_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SpanFunctions::Overright_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overright", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overright_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overright_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overright", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overright_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overright_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overright", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SpanFunctions::Overright_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SpanFunctions::Overright_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overright", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overright_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overright", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overright_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overright_span_span)
    );      
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overright", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SpanFunctions::Overright_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SpanFunctions::Overright_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overright", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overright_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overright_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overright", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overright_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overright_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overright", {SpanTypes::DATESPAN(), LogicalType::DATE}, LogicalType::BOOLEAN, SpanFunctions::Overright_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("#&>", {SpanTypes::DATESPAN(), LogicalType::DATE}, LogicalType::BOOLEAN, SpanFunctions::Overright_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overright", {LogicalType::DATE, SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overright_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("#&>", {LogicalType::DATE, SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overright_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overright", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overright_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("#&>", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overright_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overright", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SpanFunctions::Overright_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("#&>", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SpanFunctions::Overright_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overright", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overright_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("#&>", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overright_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_overright", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overright_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("#&>", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, SpanFunctions::Overright_span_span)
    );  

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_union", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpansetTypes::intspanset(), SpanFunctions::Union_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_union", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, SpansetTypes::intspanset(), SpanFunctions::Union_value_span)
    );
loader.RegisterFunction( ScalarFunction("span_union", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, SpansetTypes::intspanset(), SpanFunctions::Union_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpansetTypes::intspanset(), SpanFunctions::Union_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, SpansetTypes::intspanset(), SpanFunctions::Union_value_span)
    );
loader.RegisterFunction( ScalarFunction("+", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, SpansetTypes::intspanset(), SpanFunctions::Union_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_union", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpansetTypes::bigintspanset(), SpanFunctions::Union_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_union", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, SpansetTypes::bigintspanset(), SpanFunctions::Union_value_span)
    );
loader.RegisterFunction( ScalarFunction("span_union", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, SpansetTypes::bigintspanset(), SpanFunctions::Union_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpansetTypes::bigintspanset(), SpanFunctions::Union_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, SpansetTypes::bigintspanset(), SpanFunctions::Union_value_span)
    );
loader.RegisterFunction( ScalarFunction("+", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, SpansetTypes::bigintspanset(), SpanFunctions::Union_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_union", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpansetTypes::floatspanset(), SpanFunctions::Union_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_union", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, SpansetTypes::floatspanset(), SpanFunctions::Union_value_span)
    );
loader.RegisterFunction( ScalarFunction("span_union", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, SpansetTypes::floatspanset(), SpanFunctions::Union_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpansetTypes::floatspanset(), SpanFunctions::Union_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, SpansetTypes::floatspanset(), SpanFunctions::Union_value_span)
    );
loader.RegisterFunction( ScalarFunction("+", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, SpansetTypes::floatspanset(), SpanFunctions::Union_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_union", {SpanTypes::DATESPAN(), LogicalType::DATE}, SpansetTypes::datespanset(), SpanFunctions::Union_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_union", {LogicalType::DATE, SpanTypes::DATESPAN()}, SpansetTypes::datespanset(), SpanFunctions::Union_value_span)
    );
loader.RegisterFunction( ScalarFunction("span_union", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, SpansetTypes::datespanset(), SpanFunctions::Union_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {SpanTypes::DATESPAN(), LogicalType::DATE}, SpansetTypes::datespanset(), SpanFunctions::Union_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {LogicalType::DATE, SpanTypes::DATESPAN()}, SpansetTypes::datespanset(), SpanFunctions::Union_value_span)
    );
loader.RegisterFunction( ScalarFunction("+", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, SpansetTypes::datespanset(), SpanFunctions::Union_span_span)
    );  

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_union", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, SpansetTypes::tstzspanset(), SpanFunctions::Union_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_union", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, SpansetTypes::tstzspanset(), SpanFunctions::Union_value_span)
    );
loader.RegisterFunction( ScalarFunction("span_union", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, SpansetTypes::tstzspanset(), SpanFunctions::Union_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, SpansetTypes::tstzspanset(), SpanFunctions::Union_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, SpansetTypes::tstzspanset(), SpanFunctions::Union_value_span)
    );
loader.RegisterFunction( ScalarFunction("+", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, SpansetTypes::tstzspanset(), SpanFunctions::Union_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpanTypes::INTSPAN(), SpanFunctions::Intersection_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Intersection_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpanTypes::INTSPAN(), SpanFunctions::Intersection_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, SpanTypes::INTSPAN(), SpanFunctions::Intersection_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpanTypes::BIGINTSPAN(), SpanFunctions::Intersection_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Intersection_span_span) 
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpanTypes::BIGINTSPAN(), SpanFunctions::Intersection_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, SpanTypes::BIGINTSPAN(), SpanFunctions::Intersection_span_span) 
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpanTypes::FLOATSPAN(), SpanFunctions::Intersection_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Intersection_span_span) 
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpanTypes::FLOATSPAN(), SpanFunctions::Intersection_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, SpanTypes::FLOATSPAN(), SpanFunctions::Intersection_span_span) 
    );  

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {SpanTypes::DATESPAN(), LogicalType::DATE}, SpanTypes::DATESPAN(), SpanFunctions::Intersection_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {LogicalType::DATE, SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Intersection_span_span) 
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::DATESPAN(), LogicalType::DATE}, SpanTypes::DATESPAN(), SpanFunctions::Intersection_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::DATE, SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, SpanTypes::DATESPAN(), SpanFunctions::Intersection_span_span) 
    );  
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, SpanTypes::TSTZSPAN(), SpanFunctions::Intersection_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Intersection_span_span) 
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, SpanTypes::TSTZSPAN(), SpanFunctions::Intersection_span_value)  
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Intersection_value_span)
    );  
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, SpanTypes::TSTZSPAN(), SpanFunctions::Intersection_span_span) 
    );
    
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpansetTypes::intspanset(), SpanFunctions::Minus_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, SpansetTypes::intspanset(), SpanFunctions::Minus_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, SpansetTypes::intspanset(), SpanFunctions::Minus_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, SpansetTypes::intspanset(), SpanFunctions::Minus_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, SpansetTypes::intspanset(), SpanFunctions::Minus_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, SpansetTypes::intspanset(), SpanFunctions::Minus_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpansetTypes::bigintspanset(), SpanFunctions::Minus_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, SpansetTypes::bigintspanset(), SpanFunctions::Minus_value_span)    
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, SpansetTypes::bigintspanset(), SpanFunctions::Minus_span_span) 
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, SpansetTypes::bigintspanset(), SpanFunctions::Minus_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, SpansetTypes::bigintspanset(), SpanFunctions::Minus_value_span)    
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, SpansetTypes::bigintspanset(), SpanFunctions::Minus_span_span) 
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpansetTypes::floatspanset(), SpanFunctions::Minus_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, SpansetTypes::floatspanset(), SpanFunctions::Minus_value_span)    
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, SpansetTypes::floatspanset(), SpanFunctions::Minus_span_span) 
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, SpansetTypes::floatspanset(), SpanFunctions::Minus_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, SpansetTypes::floatspanset(), SpanFunctions::Minus_value_span)    
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, SpansetTypes::floatspanset(), SpanFunctions::Minus_span_span) 
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {SpanTypes::DATESPAN(), LogicalType::DATE}, SpansetTypes::datespanset(), SpanFunctions::Minus_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {LogicalType::DATE, SpanTypes::DATESPAN()}, SpansetTypes::datespanset(), SpanFunctions::Minus_value_span)    
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, SpansetTypes::datespanset(), SpanFunctions::Minus_span_span) 
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::DATESPAN(), LogicalType::DATE}, SpansetTypes::datespanset(), SpanFunctions::Minus_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {LogicalType::DATE, SpanTypes::DATESPAN()}, SpansetTypes::datespanset(), SpanFunctions::Minus_value_span)    
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, SpansetTypes::datespanset(), SpanFunctions::Minus_span_span) 
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, SpansetTypes::tstzspanset(), SpanFunctions::Minus_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, SpansetTypes::tstzspanset(), SpanFunctions::Minus_value_span)    
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, SpansetTypes::tstzspanset(), SpanFunctions::Minus_span_span) 
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, SpansetTypes::tstzspanset(), SpanFunctions::Minus_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, SpansetTypes::tstzspanset(), SpanFunctions::Minus_value_span)    
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, SpansetTypes::tstzspanset(), SpanFunctions::Minus_span_span) 
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, LogicalType::INTEGER, SpanFunctions::Distance_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, LogicalType::INTEGER, SpanFunctions::Distance_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::INTEGER, SpanFunctions::Distance_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SpanTypes::INTSPAN(), LogicalType::INTEGER}, LogicalType::INTEGER, SpanFunctions::Distance_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {LogicalType::INTEGER, SpanTypes::INTSPAN()}, LogicalType::INTEGER, SpanFunctions::Distance_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SpanTypes::INTSPAN(), SpanTypes::INTSPAN()}, LogicalType::INTEGER, SpanFunctions::Distance_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, LogicalType::BIGINT, SpanFunctions::Distance_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, LogicalType::BIGINT, SpanFunctions::Distance_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BIGINT, SpanFunctions::Distance_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SpanTypes::BIGINTSPAN(), LogicalType::BIGINT}, LogicalType::BIGINT, SpanFunctions::Distance_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {LogicalType::BIGINT, SpanTypes::BIGINTSPAN()}, LogicalType::BIGINT, SpanFunctions::Distance_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SpanTypes::BIGINTSPAN(), SpanTypes::BIGINTSPAN()}, LogicalType::BIGINT, SpanFunctions::Distance_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, LogicalType::DOUBLE, SpanFunctions::Distance_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, LogicalType::DOUBLE, SpanFunctions::Distance_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::DOUBLE, SpanFunctions::Distance_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SpanTypes::FLOATSPAN(), LogicalType::DOUBLE}, LogicalType::DOUBLE, SpanFunctions::Distance_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {LogicalType::DOUBLE, SpanTypes::FLOATSPAN()}, LogicalType::DOUBLE, SpanFunctions::Distance_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SpanTypes::FLOATSPAN(), SpanTypes::FLOATSPAN()}, LogicalType::DOUBLE, SpanFunctions::Distance_span_span)
    );  

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {SpanTypes::DATESPAN(), LogicalType::DATE}, LogicalType::INTEGER, SpanFunctions::Distance_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {LogicalType::DATE, SpanTypes::DATESPAN()}, LogicalType::INTEGER, SpanFunctions::Distance_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::INTEGER, SpanFunctions::Distance_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SpanTypes::DATESPAN(), LogicalType::DATE}, LogicalType::INTEGER, SpanFunctions::Distance_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {LogicalType::DATE, SpanTypes::DATESPAN()}, LogicalType::INTEGER, SpanFunctions::Distance_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SpanTypes::DATESPAN(), SpanTypes::DATESPAN()}, LogicalType::INTEGER, SpanFunctions::Distance_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, LogicalType::INTERVAL, SpanFunctions::Distance_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, LogicalType::INTERVAL, SpanFunctions::Distance_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::INTERVAL, SpanFunctions::Distance_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SpanTypes::TSTZSPAN(), LogicalType::TIMESTAMP_TZ}, LogicalType::INTERVAL, SpanFunctions::Distance_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()}, LogicalType::INTERVAL, SpanFunctions::Distance_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SpanTypes::TSTZSPAN(), SpanTypes::TSTZSPAN()}, LogicalType::INTERVAL, SpanFunctions::Distance_span_span)
    );
}


} // namespace duckdb

#ifndef MOBILITYDUCK_EXTENSION_TYPES
#endif
