#include "temporal/set.hpp"
#include "duckdb/main/extension_util.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension_util.hpp"

#include "time_util.hpp"


extern "C" {
    #include "meos.h"    
    #include "meos_internal.h"   
    #include "meos_geo.h"
}

#ifndef WKB_EXTENDED
#define WKB_EXTENDED ((uint8_t)0x04)
#endif

namespace duckdb {

#define DEFINE_SET_TYPE(NAME)                                        \
    LogicalType SetTypes::NAME() {                                   \
        auto type = LogicalType(LogicalTypeId::BLOB);             \
        type.SetAlias(#NAME);                                        \
        return type;                                                 \
    }

DEFINE_SET_TYPE(intset)
DEFINE_SET_TYPE(bigintset)
DEFINE_SET_TYPE(floatset)
DEFINE_SET_TYPE(textset)
DEFINE_SET_TYPE(dateset)
DEFINE_SET_TYPE(tstzset)

#undef DEFINE_SET_TYPE

void SetTypes::RegisterTypes(DatabaseInstance &db) {
    ExtensionUtil::RegisterType(db, "intset", intset());
    ExtensionUtil::RegisterType(db, "bigintset", bigintset());
    ExtensionUtil::RegisterType(db, "floatset", floatset());
    ExtensionUtil::RegisterType(db, "textset", textset());
    ExtensionUtil::RegisterType(db, "dateset", dateset());
    ExtensionUtil::RegisterType(db, "tstzset", tstzset());    
}

const std::vector<LogicalType> &SetTypes::AllTypes() {
    static std::vector<LogicalType> types = {
        intset(),
        bigintset(),
        floatset(),
        textset(),
        dateset(),
        tstzset()
    };
    return types;
}

meosType SetTypeMapping::GetMeosTypeFromAlias(const std::string &alias) {
    static const std::unordered_map<std::string, meosType> alias_to_type = {
        {"intset", T_INTSET},
        {"bigintset", T_BIGINTSET},
        {"floatset", T_FLOATSET},
        {"textset", T_TEXTSET},
        {"dateset", T_DATESET},
        {"tstzset", T_TSTZSET}                
    };

    auto it = alias_to_type.find(alias);
    if (it != alias_to_type.end()) {
        return it->second;
    } else {
        return T_UNKNOWN;
    }
}

LogicalType SetTypeMapping::GetChildType(const LogicalType &type) {
    auto alias = type.ToString();
    if (alias == "intset") return LogicalType::INTEGER;
    if (alias == "bigintset") return LogicalType::BIGINT;
    if (alias == "floatset") return LogicalType::DOUBLE;
    if (alias == "textset") return LogicalType::VARCHAR;
    if (alias == "dateset") return LogicalType::DATE;
    if (alias == "tstzset") return LogicalType::TIMESTAMP_TZ;    
    throw NotImplementedException("GetChildType: unsupported alias: " + alias);
}


// Register all cast functions 
void SetTypes::RegisterCastFunctions(DatabaseInstance &instance) {
    for (const auto &set_type : SetTypes::AllTypes()) {
        ExtensionUtil::RegisterCastFunction(
            instance,
            set_type,                      
            LogicalType::VARCHAR,   
            SetFunctions::Set_to_text   
        ); // Blob to text
        ExtensionUtil::RegisterCastFunction(
            instance,
            LogicalType::VARCHAR, 
            set_type,                                    
            SetFunctions::Text_to_set   
        ); // text to blob
        
        auto base_type = SetTypeMapping::GetChildType(set_type);
        ExtensionUtil::RegisterCastFunction(
            instance,
            base_type,
            set_type,
            SetFunctions::Value_to_set_cast // set from base type
        );

        ExtensionUtil::RegisterCastFunction(
            instance,
            SetTypes::intset(),
            SetTypes::floatset(),
            SetFunctions::Intset_to_floatset_cast // intset -> floatset 
        );

        ExtensionUtil::RegisterCastFunction(
            instance,
            SetTypes::floatset(),
            SetTypes::intset(),
            SetFunctions::Floatset_to_intset_cast // floatset --> intset
        );
        
        ExtensionUtil::RegisterCastFunction(
            instance,
            SetTypes::dateset(),
            SetTypes::tstzset(),
            SetFunctions::Dateset_to_tstzset_cast // dateset -> tstzset
        );
        
        ExtensionUtil::RegisterCastFunction(
            instance,
            SetTypes::tstzset(),
            SetTypes::dateset(),
            SetFunctions::Tstzset_to_dateset_cast // tstz -> dateset 
        );

    }
}

void SetTypes::RegisterScalarFunctions(DatabaseInstance &db) {
    for (const auto &set_type : SetTypes::AllTypes()) {
        auto base_type = SetTypeMapping::GetChildType(set_type);         

        // Register: asText
        if (set_type == SetTypes::floatset()) {            
            ExtensionUtil::RegisterFunction( // asText(floatset)
                db, ScalarFunction("asText", {set_type}, LogicalType::VARCHAR, SetFunctions::Set_as_text)
            );
            
            ExtensionUtil::RegisterFunction( // asText(floatset, int)
                db, ScalarFunction("asText", {set_type, LogicalType::INTEGER}, LogicalType::VARCHAR, SetFunctions::Set_as_text)
            );
        } else {            
            ExtensionUtil::RegisterFunction( // All other set types
                db, ScalarFunction("asText", {set_type}, LogicalType::VARCHAR, SetFunctions::Set_as_text)
            );
        }

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("set", {LogicalType::LIST(base_type)}, set_type, SetFunctions::Set_constructor)                 
        );        

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("set", {base_type}, set_type, SetFunctions::Value_to_set)                 
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("intset", {SetTypes::floatset()}, SetTypes::intset(), SetFunctions::Floatset_to_intset)                 
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("floatset", {SetTypes::intset()}, SetTypes::floatset(), SetFunctions::Intset_to_floatset)                 
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("dateset", {SetTypes::tstzset()}, SetTypes::dateset(), SetFunctions::Tstzset_to_dateset)                 
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("tstzset", {SetTypes::dateset()}, SetTypes::tstzset(), SetFunctions::Dateset_to_tstzset)                 
        );

        ExtensionUtil::RegisterFunction(
            db, 
            ScalarFunction("memSize",{set_type}, LogicalType::INTEGER, SetFunctions::Set_mem_size)
        );
        
        ExtensionUtil::RegisterFunction(
            db, 
            ScalarFunction("numValues", {set_type}, LogicalType::INTEGER,SetFunctions::Set_num_values)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("startValue", {set_type}, base_type, SetFunctions::Set_start_value)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("endValue", {set_type}, base_type, SetFunctions::Set_end_value)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("valueN", {set_type, LogicalType::INTEGER}, base_type, SetFunctions::Set_value_n)
        );

        ExtensionUtil::RegisterFunction(
            db, 
            ScalarFunction("getValues", {set_type}, LogicalType::LIST(base_type), SetFunctions::Set_values)
        );

        ExtensionUtil::RegisterFunction(
            db, 
            ScalarFunction("shift", {SetTypes::intset(), LogicalType::INTEGER}, SetTypes::intset(), SetFunctions::Numset_shift)
        );

        ExtensionUtil::RegisterFunction(
            db, 
            ScalarFunction("shift", {SetTypes::bigintset(), LogicalType::BIGINT}, SetTypes::bigintset(), SetFunctions::Numset_shift)
        );

        ExtensionUtil::RegisterFunction(
            db, 
            ScalarFunction("shift", {SetTypes::floatset(), LogicalType::DOUBLE}, SetTypes::floatset(), SetFunctions::Numset_shift)
        );
        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("shift", {SetTypes::dateset(), LogicalType::INTEGER}, SetTypes::dateset(), SetFunctions::Numset_shift)
        );        

        ExtensionUtil::RegisterFunction(
            db, 
            ScalarFunction("shift", {SetTypes::tstzset(), LogicalType::INTERVAL}, SetTypes::tstzset(), SetFunctions::Tstzset_shift)
        );

        ExtensionUtil::RegisterFunction(
            db, 
            ScalarFunction("scale", {SetTypes::intset(), LogicalType::INTEGER}, SetTypes::intset(), SetFunctions::Numset_scale)
        );

        ExtensionUtil::RegisterFunction(
            db, 
            ScalarFunction("scale", {SetTypes::bigintset(), LogicalType::BIGINT}, SetTypes::bigintset(), SetFunctions::Numset_scale)
        );

        ExtensionUtil::RegisterFunction(
            db, 
            ScalarFunction("scale", {SetTypes::floatset(), LogicalType::DOUBLE}, SetTypes::floatset(), SetFunctions::Numset_scale)
        );
        
        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("scale", {SetTypes::dateset(), LogicalType::INTEGER}, SetTypes::dateset(), SetFunctions::Numset_scale)
        ); 

        ExtensionUtil::RegisterFunction(
            db, 
            ScalarFunction("scale", {SetTypes::tstzset(), LogicalType::INTERVAL}, SetTypes::tstzset(), SetFunctions::Tstzset_scale)
        );

        ExtensionUtil::RegisterFunction(
            db, 
            ScalarFunction("shiftScale", {SetTypes::intset(), LogicalType::INTEGER, LogicalType::INTEGER}, SetTypes::intset(), SetFunctions::Numset_shift_scale)
        );

        ExtensionUtil::RegisterFunction(
            db, 
            ScalarFunction("shiftScale", {SetTypes::bigintset(), LogicalType::BIGINT, LogicalType::BIGINT}, SetTypes::bigintset(), SetFunctions::Numset_shift_scale)
        );

        ExtensionUtil::RegisterFunction(
            db, 
            ScalarFunction("shiftScale", {SetTypes::floatset(), LogicalType::DOUBLE, LogicalType::DOUBLE}, SetTypes::floatset(), SetFunctions::Numset_shift_scale)
        );
        
        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("shiftScale", {SetTypes::dateset(), LogicalType::INTEGER, LogicalType::INTEGER}, SetTypes::dateset(), SetFunctions::Numset_shift_scale)
        );

        ExtensionUtil::RegisterFunction(
            db, 
            ScalarFunction("shiftScale", {SetTypes::tstzset(), LogicalType::INTERVAL, LogicalType::INTERVAL}, SetTypes::tstzset(), SetFunctions::Tstzset_shift_scale)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("floor", {SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Floatset_floor)                 
        );
        
        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("ceil", {SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Floatset_ceil)                 
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("round", {SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Floatset_round)                 
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("round", {SetTypes::floatset(), LogicalType::INTEGER}, SetTypes::floatset(), SetFunctions::Floatset_round)                 
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("degrees", {SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Floatset_degrees)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("degrees", {SetTypes::floatset(), LogicalType::BOOLEAN}, SetTypes::floatset(), SetFunctions::Floatset_degrees)
        );
        
        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("radians", {SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Floatset_radians)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("lower", {SetTypes::textset()}, SetTypes::textset(), SetFunctions::Textset_lower)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("upper", {SetTypes::textset()}, SetTypes::textset(), SetFunctions::Textset_upper)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("initcap", {SetTypes::textset()}, SetTypes::textset(), SetFunctions::Textset_initcap)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("+", {set_type, set_type}, set_type, SetFunctions::Union_set_set)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("asBinary", {set_type}, LogicalType::BLOB, SetFunctions::Set_as_binary)
        );

        ExtensionUtil::RegisterFunction(
            db,
            ScalarFunction("asHexWKB", {set_type}, LogicalType::VARCHAR, SetFunctions::Set_as_hexwkb)
        );
    }

    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("textset_cat", {LogicalType::VARCHAR, SetTypes::textset()}, SetTypes::textset(),
                       SetFunctions::Textcat_text_textset)
    );
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("textset_cat", {SetTypes::textset(), LogicalType::VARCHAR}, SetTypes::textset(),
                       SetFunctions::Textcat_textset_text)
    );
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("||", {LogicalType::VARCHAR, SetTypes::textset()}, SetTypes::textset(), SetFunctions::Textcat_text_textset)
    );
    ExtensionUtil::RegisterFunction(
        db,
        ScalarFunction("||", {SetTypes::textset(), LogicalType::VARCHAR}, SetTypes::textset(), SetFunctions::Textcat_textset_text)
    );

    // --- set_contains / @> ---
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contains", {SetTypes::textset(), LogicalType::VARCHAR}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contains", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contains", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contains", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contains", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contains", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contains", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contains", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contains", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contains", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contains", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contains", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));

    ExtensionUtil::RegisterFunction(db, ScalarFunction("@>", {SetTypes::textset(), LogicalType::VARCHAR}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("@>", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("@>", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("@>", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("@>", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("@>", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("@>", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("@>", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("@>", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("@>", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("@>", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("@>", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));

    // --- set_contained / <@ ---
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contained", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contained", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contained", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contained", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contained", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contained", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contained", {LogicalType::VARCHAR, SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contained", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contained", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contained", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contained", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_contained", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));

    ExtensionUtil::RegisterFunction(db, ScalarFunction("<@", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<@", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<@", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<@", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<@", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<@", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<@", {LogicalType::VARCHAR, SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<@", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<@", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<@", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<@", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<@", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));

    // --- set_overlaps / && ---
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overlaps", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overlaps", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overlaps", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overlaps", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overlaps", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overlaps", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&&", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&&", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&&", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&&", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&&", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&&", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));

    // --- Position: set_left / << ---
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_left", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_left", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_left", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_left", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_left", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_left", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_left", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_left", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_left", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_left", {LogicalType::VARCHAR, SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_left", {SetTypes::textset(), LogicalType::VARCHAR}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_left", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_left", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_left", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_left", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_left", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_left", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_left", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<<", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<<", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<<", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<<", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<<", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<<", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<<", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<<", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<<", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<<", {LogicalType::VARCHAR, SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<<", {SetTypes::textset(), LogicalType::VARCHAR}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<<", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<<", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<<", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<<", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<<", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<<", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<<", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));

    // --- set_right / >> ---
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_right", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_right", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_right", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_right", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_right", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_right", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_right", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_right", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_right", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_right", {LogicalType::VARCHAR, SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_right", {SetTypes::textset(), LogicalType::VARCHAR}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_right", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_right", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_right", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_right", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_right", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_right", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_right", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">>", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">>", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">>", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">>", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">>", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">>", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">>", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">>", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">>", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">>", {LogicalType::VARCHAR, SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">>", {SetTypes::textset(), LogicalType::VARCHAR}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">>", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">>", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">>", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">>", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">>", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">>", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">>", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));

    // --- set_overleft / &< ---
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overleft", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overleft", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overleft", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overleft", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overleft", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overleft", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overleft", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overleft", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overleft", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overleft", {LogicalType::VARCHAR, SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overleft", {SetTypes::textset(), LogicalType::VARCHAR}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overleft", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overleft", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overleft", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overleft", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overleft", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overleft", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overleft", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&<", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&<", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&<", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&<", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&<", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&<", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&<", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&<", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&<", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&<", {LogicalType::VARCHAR, SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&<", {SetTypes::textset(), LogicalType::VARCHAR}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&<", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&<", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&<", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&<", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&<", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&<", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&<", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));

    // --- set_overright / &> ---
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overright", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overright", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overright", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overright", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overright", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overright", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overright", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overright", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overright", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overright", {LogicalType::VARCHAR, SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overright", {SetTypes::textset(), LogicalType::VARCHAR}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overright", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overright", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overright", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overright", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overright", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overright", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_overright", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&>", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&>", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&>", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&>", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&>", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&>", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&>", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&>", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&>", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&>", {LogicalType::VARCHAR, SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&>", {SetTypes::textset(), LogicalType::VARCHAR}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&>", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&>", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&>", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&>", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&>", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&>", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("&>", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));

    // --- set_union / + (value forms) ---
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_union", {LogicalType::INTEGER, SetTypes::intset()}, SetTypes::intset(), SetFunctions::Union_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_union", {SetTypes::intset(), LogicalType::INTEGER}, SetTypes::intset(), SetFunctions::Union_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_union", {LogicalType::BIGINT, SetTypes::bigintset()}, SetTypes::bigintset(), SetFunctions::Union_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_union", {SetTypes::bigintset(), LogicalType::BIGINT}, SetTypes::bigintset(), SetFunctions::Union_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_union", {LogicalType::DOUBLE, SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Union_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_union", {SetTypes::floatset(), LogicalType::DOUBLE}, SetTypes::floatset(), SetFunctions::Union_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_union", {LogicalType::VARCHAR, SetTypes::textset()}, SetTypes::textset(), SetFunctions::Union_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_union", {SetTypes::textset(), LogicalType::VARCHAR}, SetTypes::textset(), SetFunctions::Union_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_union", {LogicalType::DATE, SetTypes::dateset()}, SetTypes::dateset(), SetFunctions::Union_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_union", {SetTypes::dateset(), LogicalType::DATE}, SetTypes::dateset(), SetFunctions::Union_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_union", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, SetTypes::tstzset(), SetFunctions::Union_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_union", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, SetTypes::tstzset(), SetFunctions::Union_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("+", {LogicalType::INTEGER, SetTypes::intset()}, SetTypes::intset(), SetFunctions::Union_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("+", {SetTypes::intset(), LogicalType::INTEGER}, SetTypes::intset(), SetFunctions::Union_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("+", {LogicalType::BIGINT, SetTypes::bigintset()}, SetTypes::bigintset(), SetFunctions::Union_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("+", {SetTypes::bigintset(), LogicalType::BIGINT}, SetTypes::bigintset(), SetFunctions::Union_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("+", {LogicalType::DOUBLE, SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Union_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("+", {SetTypes::floatset(), LogicalType::DOUBLE}, SetTypes::floatset(), SetFunctions::Union_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("+", {LogicalType::VARCHAR, SetTypes::textset()}, SetTypes::textset(), SetFunctions::Union_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("+", {SetTypes::textset(), LogicalType::VARCHAR}, SetTypes::textset(), SetFunctions::Union_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("+", {LogicalType::DATE, SetTypes::dateset()}, SetTypes::dateset(), SetFunctions::Union_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("+", {SetTypes::dateset(), LogicalType::DATE}, SetTypes::dateset(), SetFunctions::Union_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("+", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, SetTypes::tstzset(), SetFunctions::Union_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("+", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, SetTypes::tstzset(), SetFunctions::Union_set_value));

    // --- set_minus / - ---
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_minus", {LogicalType::INTEGER, SetTypes::intset()}, SetTypes::intset(), SetFunctions::Minus_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_minus", {SetTypes::intset(), LogicalType::INTEGER}, SetTypes::intset(), SetFunctions::Minus_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_minus", {SetTypes::intset(), SetTypes::intset()}, SetTypes::intset(), SetFunctions::Minus_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_minus", {LogicalType::BIGINT, SetTypes::bigintset()}, SetTypes::bigintset(), SetFunctions::Minus_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_minus", {SetTypes::bigintset(), LogicalType::BIGINT}, SetTypes::bigintset(), SetFunctions::Minus_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_minus", {SetTypes::bigintset(), SetTypes::bigintset()}, SetTypes::bigintset(), SetFunctions::Minus_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_minus", {LogicalType::DOUBLE, SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Minus_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_minus", {SetTypes::floatset(), LogicalType::DOUBLE}, SetTypes::floatset(), SetFunctions::Minus_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_minus", {SetTypes::floatset(), SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Minus_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_minus", {LogicalType::VARCHAR, SetTypes::textset()}, SetTypes::textset(), SetFunctions::Minus_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_minus", {SetTypes::textset(), LogicalType::VARCHAR}, SetTypes::textset(), SetFunctions::Minus_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_minus", {SetTypes::textset(), SetTypes::textset()}, SetTypes::textset(), SetFunctions::Minus_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_minus", {LogicalType::DATE, SetTypes::dateset()}, SetTypes::dateset(), SetFunctions::Minus_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_minus", {SetTypes::dateset(), LogicalType::DATE}, SetTypes::dateset(), SetFunctions::Minus_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_minus", {SetTypes::dateset(), SetTypes::dateset()}, SetTypes::dateset(), SetFunctions::Minus_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_minus", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, SetTypes::tstzset(), SetFunctions::Minus_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_minus", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, SetTypes::tstzset(), SetFunctions::Minus_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_minus", {SetTypes::tstzset(), SetTypes::tstzset()}, SetTypes::tstzset(), SetFunctions::Minus_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("-", {LogicalType::INTEGER, SetTypes::intset()}, SetTypes::intset(), SetFunctions::Minus_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("-", {SetTypes::intset(), LogicalType::INTEGER}, SetTypes::intset(), SetFunctions::Minus_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("-", {SetTypes::intset(), SetTypes::intset()}, SetTypes::intset(), SetFunctions::Minus_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("-", {LogicalType::BIGINT, SetTypes::bigintset()}, SetTypes::bigintset(), SetFunctions::Minus_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("-", {SetTypes::bigintset(), LogicalType::BIGINT}, SetTypes::bigintset(), SetFunctions::Minus_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("-", {SetTypes::bigintset(), SetTypes::bigintset()}, SetTypes::bigintset(), SetFunctions::Minus_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("-", {LogicalType::DOUBLE, SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Minus_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("-", {SetTypes::floatset(), LogicalType::DOUBLE}, SetTypes::floatset(), SetFunctions::Minus_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("-", {SetTypes::floatset(), SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Minus_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("-", {LogicalType::VARCHAR, SetTypes::textset()}, SetTypes::textset(), SetFunctions::Minus_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("-", {SetTypes::textset(), LogicalType::VARCHAR}, SetTypes::textset(), SetFunctions::Minus_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("-", {SetTypes::textset(), SetTypes::textset()}, SetTypes::textset(), SetFunctions::Minus_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("-", {LogicalType::DATE, SetTypes::dateset()}, SetTypes::dateset(), SetFunctions::Minus_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("-", {SetTypes::dateset(), LogicalType::DATE}, SetTypes::dateset(), SetFunctions::Minus_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("-", {SetTypes::dateset(), SetTypes::dateset()}, SetTypes::dateset(), SetFunctions::Minus_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("-", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, SetTypes::tstzset(), SetFunctions::Minus_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("-", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, SetTypes::tstzset(), SetFunctions::Minus_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("-", {SetTypes::tstzset(), SetTypes::tstzset()}, SetTypes::tstzset(), SetFunctions::Minus_set_set));

    // --- set_intersection / * ---
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_intersection", {LogicalType::INTEGER, SetTypes::intset()}, SetTypes::intset(), SetFunctions::Intersect_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_intersection", {SetTypes::intset(), LogicalType::INTEGER}, SetTypes::intset(), SetFunctions::Intersect_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_intersection", {SetTypes::intset(), SetTypes::intset()}, SetTypes::intset(), SetFunctions::Intersect_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_intersection", {LogicalType::BIGINT, SetTypes::bigintset()}, SetTypes::bigintset(), SetFunctions::Intersect_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_intersection", {SetTypes::bigintset(), LogicalType::BIGINT}, SetTypes::bigintset(), SetFunctions::Intersect_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_intersection", {SetTypes::bigintset(), SetTypes::bigintset()}, SetTypes::bigintset(), SetFunctions::Intersect_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_intersection", {LogicalType::DOUBLE, SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Intersect_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_intersection", {SetTypes::floatset(), LogicalType::DOUBLE}, SetTypes::floatset(), SetFunctions::Intersect_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_intersection", {SetTypes::floatset(), SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Intersect_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_intersection", {LogicalType::VARCHAR, SetTypes::textset()}, SetTypes::textset(), SetFunctions::Intersect_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_intersection", {SetTypes::textset(), LogicalType::VARCHAR}, SetTypes::textset(), SetFunctions::Intersect_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_intersection", {SetTypes::textset(), SetTypes::textset()}, SetTypes::textset(), SetFunctions::Intersect_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_intersection", {LogicalType::DATE, SetTypes::dateset()}, SetTypes::dateset(), SetFunctions::Intersect_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_intersection", {SetTypes::dateset(), LogicalType::DATE}, SetTypes::dateset(), SetFunctions::Intersect_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_intersection", {SetTypes::dateset(), SetTypes::dateset()}, SetTypes::dateset(), SetFunctions::Intersect_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_intersection", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, SetTypes::tstzset(), SetFunctions::Intersect_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_intersection", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, SetTypes::tstzset(), SetFunctions::Intersect_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_intersection", {SetTypes::tstzset(), SetTypes::tstzset()}, SetTypes::tstzset(), SetFunctions::Intersect_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("*", {LogicalType::INTEGER, SetTypes::intset()}, SetTypes::intset(), SetFunctions::Intersect_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("*", {SetTypes::intset(), LogicalType::INTEGER}, SetTypes::intset(), SetFunctions::Intersect_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("*", {SetTypes::intset(), SetTypes::intset()}, SetTypes::intset(), SetFunctions::Intersect_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("*", {LogicalType::BIGINT, SetTypes::bigintset()}, SetTypes::bigintset(), SetFunctions::Intersect_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("*", {SetTypes::bigintset(), LogicalType::BIGINT}, SetTypes::bigintset(), SetFunctions::Intersect_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("*", {SetTypes::bigintset(), SetTypes::bigintset()}, SetTypes::bigintset(), SetFunctions::Intersect_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("*", {LogicalType::DOUBLE, SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Intersect_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("*", {SetTypes::floatset(), LogicalType::DOUBLE}, SetTypes::floatset(), SetFunctions::Intersect_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("*", {SetTypes::floatset(), SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Intersect_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("*", {LogicalType::VARCHAR, SetTypes::textset()}, SetTypes::textset(), SetFunctions::Intersect_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("*", {SetTypes::textset(), LogicalType::VARCHAR}, SetTypes::textset(), SetFunctions::Intersect_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("*", {SetTypes::textset(), SetTypes::textset()}, SetTypes::textset(), SetFunctions::Intersect_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("*", {LogicalType::DATE, SetTypes::dateset()}, SetTypes::dateset(), SetFunctions::Intersect_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("*", {SetTypes::dateset(), LogicalType::DATE}, SetTypes::dateset(), SetFunctions::Intersect_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("*", {SetTypes::dateset(), SetTypes::dateset()}, SetTypes::dateset(), SetFunctions::Intersect_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("*", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, SetTypes::tstzset(), SetFunctions::Intersect_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("*", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, SetTypes::tstzset(), SetFunctions::Intersect_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("*", {SetTypes::tstzset(), SetTypes::tstzset()}, SetTypes::tstzset(), SetFunctions::Intersect_set_set));

    // --- set_distance / <-> ---
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_distance", {LogicalType::INTEGER, LogicalType::INTEGER}, LogicalType::INTEGER, SetFunctions::Distance_value_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_distance", {LogicalType::BIGINT, LogicalType::BIGINT}, LogicalType::BIGINT, SetFunctions::Distance_value_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_distance", {LogicalType::DOUBLE, LogicalType::DOUBLE}, LogicalType::DOUBLE, SetFunctions::Distance_value_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_distance", {LogicalType::DATE, LogicalType::DATE}, LogicalType::INTEGER, SetFunctions::Distance_value_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_distance", {LogicalType::TIMESTAMP_TZ, LogicalType::TIMESTAMP_TZ}, LogicalType::DOUBLE, SetFunctions::Distance_value_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_distance", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::INTEGER, SetFunctions::Distance_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_distance", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::INTEGER, SetFunctions::Distance_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_distance", {SetTypes::intset(), SetTypes::intset()}, LogicalType::INTEGER, SetFunctions::Distance_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_distance", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BIGINT, SetFunctions::Distance_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_distance", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BIGINT, SetFunctions::Distance_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_distance", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BIGINT, SetFunctions::Distance_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_distance", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::DOUBLE, SetFunctions::Distance_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_distance", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::DOUBLE, SetFunctions::Distance_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_distance", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::DOUBLE, SetFunctions::Distance_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_distance", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::INTEGER, SetFunctions::Distance_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_distance", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::INTEGER, SetFunctions::Distance_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_distance", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::INTEGER, SetFunctions::Distance_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_distance", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::DOUBLE, SetFunctions::Distance_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_distance", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::DOUBLE, SetFunctions::Distance_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_distance", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::DOUBLE, SetFunctions::Distance_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<->", {LogicalType::INTEGER, LogicalType::INTEGER}, LogicalType::INTEGER, SetFunctions::Distance_value_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<->", {LogicalType::BIGINT, LogicalType::BIGINT}, LogicalType::BIGINT, SetFunctions::Distance_value_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<->", {LogicalType::DOUBLE, LogicalType::DOUBLE}, LogicalType::DOUBLE, SetFunctions::Distance_value_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<->", {LogicalType::DATE, LogicalType::DATE}, LogicalType::INTEGER, SetFunctions::Distance_value_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<->", {LogicalType::TIMESTAMP_TZ, LogicalType::TIMESTAMP_TZ}, LogicalType::DOUBLE, SetFunctions::Distance_value_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<->", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::INTEGER, SetFunctions::Distance_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<->", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::INTEGER, SetFunctions::Distance_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<->", {SetTypes::intset(), SetTypes::intset()}, LogicalType::INTEGER, SetFunctions::Distance_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<->", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BIGINT, SetFunctions::Distance_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<->", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BIGINT, SetFunctions::Distance_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<->", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BIGINT, SetFunctions::Distance_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<->", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::DOUBLE, SetFunctions::Distance_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<->", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::DOUBLE, SetFunctions::Distance_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<->", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::DOUBLE, SetFunctions::Distance_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<->", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::INTEGER, SetFunctions::Distance_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<->", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::INTEGER, SetFunctions::Distance_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<->", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::INTEGER, SetFunctions::Distance_set_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<->", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::DOUBLE, SetFunctions::Distance_set_value));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<->", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::DOUBLE, SetFunctions::Distance_value_set));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<->", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::DOUBLE, SetFunctions::Distance_set_set));

    // --- set_eq / = ---
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_eq", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_eq", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_eq", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_eq", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_eq", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_eq", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));

    ExtensionUtil::RegisterFunction(db, ScalarFunction("=", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("=", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("=", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("=", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("=", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("=", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));

    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_ne", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_ne", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_ne", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_ne", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_ne", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_ne", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));

    ExtensionUtil::RegisterFunction(db, ScalarFunction("<>", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<>", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<>", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<>", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<>", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<>", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));


    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_lt", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_lt", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_lt", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_lt", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_lt", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_lt", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));

    ExtensionUtil::RegisterFunction(db, ScalarFunction("<", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));

    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_le", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_le", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_le", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_le", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_le", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_le", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));

    ExtensionUtil::RegisterFunction(db, ScalarFunction("<=", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<=", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<=", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<=", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<=", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("<=", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));

    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_gt", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_gt", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_gt", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_gt", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_gt", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_gt", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));

    ExtensionUtil::RegisterFunction(db, ScalarFunction(">", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));

    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_ge", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_ge", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_ge", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_ge", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_ge", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_ge", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));

    ExtensionUtil::RegisterFunction(db, ScalarFunction(">=", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">=", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">=", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">=", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">=", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));
    ExtensionUtil::RegisterFunction(db, ScalarFunction(">=", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));

    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_cmp", {SetTypes::intset(), SetTypes::intset()}, LogicalType::INTEGER, SetFunctions::Set_cmp));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_cmp", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::INTEGER, SetFunctions::Set_cmp));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_cmp", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::INTEGER, SetFunctions::Set_cmp));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_cmp", {SetTypes::textset(), SetTypes::textset()}, LogicalType::INTEGER, SetFunctions::Set_cmp));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_cmp", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::INTEGER, SetFunctions::Set_cmp));
    ExtensionUtil::RegisterFunction(db, ScalarFunction("set_cmp", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::INTEGER, SetFunctions::Set_cmp));
}

// --- Unnest ---
struct SetUnnestBindData : public TableFunctionData {
    string_t blob;
    meosType set_type;
    LogicalType return_type;

    SetUnnestBindData(string_t blob, meosType set_type, LogicalType return_type)
        : blob(std::move(blob)), set_type(set_type), return_type(std::move(return_type)) {}
};


struct SetUnnestGlobalState : public GlobalTableFunctionState {
    idx_t idx = 0;
    std::vector<Value> values;    
};

static unique_ptr<FunctionData> SetUnnestBind(ClientContext &context,
                                              TableFunctionBindInput &input,
                                              vector<LogicalType> &return_types,
                                              vector<string> &names) {
    if (input.inputs.size() != 1 || input.inputs[0].IsNull()) {
        throw BinderException("SetUnnest: expects a non-null blob input");
    }

    auto in_val = input.inputs[0];
    if (in_val.type().id() != LogicalTypeId::BLOB) {
        throw BinderException("SetUnnest: expected BLOB as input");
    }

    string_t blob = StringValue::Get(in_val);

    auto duck_type = SetTypeMapping::GetChildType(in_val.type());
    auto set_type = SetTypeMapping::GetMeosTypeFromAlias(in_val.type().ToString());

    return_types.emplace_back(duck_type);
    names.emplace_back("unnest");

    return make_uniq<SetUnnestBindData>(blob, set_type, duck_type);
}

static unique_ptr<GlobalTableFunctionState> SetUnnestInit(ClientContext &context,
                                                          TableFunctionInitInput &input) {
    auto &bind = input.bind_data->Cast<SetUnnestBindData>();
    auto &blob = bind.blob;

    const uint8_t *data = (const uint8_t *)blob.GetData();
    size_t size = blob.GetSize();

    Set *s = (Set *)malloc(size);
    memcpy(s, data, size);

    auto state = make_uniq<SetUnnestGlobalState>();
    int count = s->count;

    for (int i = 1; i <= count; ++i) {
        Datum d;
        bool found = set_value_n(s, i, &d);
        if (!found) continue;

        switch (settype_basetype(bind.set_type)) {
            case T_INT4:
                state->values.emplace_back(Value::INTEGER((int32_t)d));
                break;
            case T_INT8:
                state->values.emplace_back(Value::BIGINT((int64_t)d));
                break;
            case T_FLOAT8:
                state->values.emplace_back(Value::DOUBLE(DatumGetFloat8(d)));
                break;
            case T_TEXT: {     
                text *txt = (text *)DatumGetPointer(d);
                int len = VARSIZE(txt) - VARHDRSZ;
                std::string str(VARDATA(txt), len);
                state->values.emplace_back(Value(str));
                break;
            }
            case T_DATE:
                state->values.emplace_back(Value::DATE(date_t(FromMeosDate((int32_t)d))));
                break;
            case T_TIMESTAMPTZ:
                state->values.emplace_back(Value::TIMESTAMPTZ(timestamp_tz_t(FromMeosTimestamp((int64_t)d))));
                break;
            default:
                free(s);
                throw NotImplementedException("SetUnnest: unsupported base type");
        }
    }

    free(s);
    return std::move(state);
}

static void SetUnnestExec(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
    auto &state = input.global_state->Cast<SetUnnestGlobalState>();
    auto count = MinValue<idx_t>(STANDARD_VECTOR_SIZE, state.values.size() - state.idx);

    for (idx_t i = 0; i < count; i++) {
        output.SetValue(0, i, state.values[state.idx++]);
    }

    output.SetCardinality(count);
}

void SetTypes::RegisterSetUnnest(DatabaseInstance &db) {
    for (auto &set_type : SetTypes::AllTypes()) {
        TableFunction fn("SetUnnest",
                         {set_type},
                         SetUnnestExec,
                         SetUnnestBind,
                         SetUnnestInit);
        ExtensionUtil::RegisterFunction(db, fn);
    }
}

} // namespace duckdb
