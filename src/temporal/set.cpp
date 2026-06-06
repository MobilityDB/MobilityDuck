#include "temporal/set.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/aggregate_function.hpp"
#include "duckdb/function/function_set.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "time_util.hpp"
#include "mobilityduck/meos_exec_serial.hpp"


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

void SetTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType( "intset", intset());
    loader.RegisterType( "bigintset", bigintset());
    loader.RegisterType( "floatset", floatset());
    loader.RegisterType( "textset", textset());
    loader.RegisterType( "dateset", dateset());
    loader.RegisterType( "tstzset", tstzset());    
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

MeosType SetTypeMapping::GetMeosTypeFromAlias(const std::string &alias) {
    static const std::unordered_map<std::string, MeosType> alias_to_type = {
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
void SetTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    for (const auto &set_type : SetTypes::AllTypes()) {
        RegisterMeosCastFunction(loader, 
            set_type,                      
            LogicalType::VARCHAR,   
            SetFunctions::Set_to_text   
        ); // Blob to text
        RegisterMeosCastFunction(loader, 
            LogicalType::VARCHAR, 
            set_type,                                    
            SetFunctions::Text_to_set   
        ); // text to blob
        
        auto base_type = SetTypeMapping::GetChildType(set_type);
        RegisterMeosCastFunction(loader, 
            base_type,
            set_type,
            SetFunctions::Value_to_set_cast // set from base type
        );

        RegisterMeosCastFunction(loader, 
            SetTypes::intset(),
            SetTypes::floatset(),
            SetFunctions::Intset_to_floatset_cast // intset -> floatset 
        );

        RegisterMeosCastFunction(loader, 
            SetTypes::floatset(),
            SetTypes::intset(),
            SetFunctions::Floatset_to_intset_cast // floatset --> intset
        );
        
        RegisterMeosCastFunction(loader, 
            SetTypes::dateset(),
            SetTypes::tstzset(),
            SetFunctions::Dateset_to_tstzset_cast // dateset -> tstzset
        );
        
        RegisterMeosCastFunction(loader, 
            SetTypes::tstzset(),
            SetTypes::dateset(),
            SetFunctions::Tstzset_to_dateset_cast // tstz -> dateset 
        );

    }
}

void SetTypes::RegisterScalarFunctions(ExtensionLoader &loader) {
    for (const auto &set_type : SetTypes::AllTypes()) {
        auto base_type = SetTypeMapping::GetChildType(set_type);         

        // Register: asText
        if (set_type == SetTypes::floatset()) {            
            duckdb::RegisterSerializedScalarFunction(loader,  // asText(floatset)
                ScalarFunction("asText", {set_type}, LogicalType::VARCHAR, SetFunctions::Set_as_text)
            );
            
            duckdb::RegisterSerializedScalarFunction(loader,  // asText(floatset, int)
                ScalarFunction("asText", {set_type, LogicalType::INTEGER}, LogicalType::VARCHAR, SetFunctions::Set_as_text)
            );
        } else {            
            duckdb::RegisterSerializedScalarFunction(loader,  // All other set types
                ScalarFunction("asText", {set_type}, LogicalType::VARCHAR, SetFunctions::Set_as_text)
            );
        }

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("set", {LogicalType::LIST(base_type)}, set_type, SetFunctions::Set_constructor)                 
        );        

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("set", {base_type}, set_type, SetFunctions::Value_to_set)                 
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("intset", {SetTypes::floatset()}, SetTypes::intset(), SetFunctions::Floatset_to_intset)                 
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("floatset", {SetTypes::intset()}, SetTypes::floatset(), SetFunctions::Intset_to_floatset)                 
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("dateset", {SetTypes::tstzset()}, SetTypes::dateset(), SetFunctions::Tstzset_to_dateset)                 
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("tstzset", {SetTypes::dateset()}, SetTypes::tstzset(), SetFunctions::Dateset_to_tstzset)                 
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("memSize",{set_type}, LogicalType::INTEGER, SetFunctions::Set_mem_size)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("set_hash", {set_type}, LogicalType::UINTEGER, SetFunctions::Set_hash)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("set_hash_extended", {set_type, LogicalType::BIGINT}, LogicalType::UBIGINT, SetFunctions::Set_hash_extended)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("numValues", {set_type}, LogicalType::INTEGER,SetFunctions::Set_num_values)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("startValue", {set_type}, base_type, SetFunctions::Set_start_value)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("endValue", {set_type}, base_type, SetFunctions::Set_end_value)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("valueN", {set_type, LogicalType::INTEGER}, base_type, SetFunctions::Set_value_n)
        );

        duckdb::RegisterSerializedScalarFunction(loader,  
            ScalarFunction("getValues", {set_type}, LogicalType::LIST(base_type), SetFunctions::Set_values)
        );

        duckdb::RegisterSerializedScalarFunction(loader,  
            ScalarFunction("shift", {SetTypes::intset(), LogicalType::INTEGER}, SetTypes::intset(), SetFunctions::Numset_shift)
        );

        duckdb::RegisterSerializedScalarFunction(loader,  
            ScalarFunction("shift", {SetTypes::bigintset(), LogicalType::BIGINT}, SetTypes::bigintset(), SetFunctions::Numset_shift)
        );

        duckdb::RegisterSerializedScalarFunction(loader,  
            ScalarFunction("shift", {SetTypes::floatset(), LogicalType::DOUBLE}, SetTypes::floatset(), SetFunctions::Numset_shift)
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("shift", {SetTypes::dateset(), LogicalType::INTEGER}, SetTypes::dateset(), SetFunctions::Numset_shift)
        );        

        duckdb::RegisterSerializedScalarFunction(loader,
            ScalarFunction("shift", {SetTypes::tstzset(), LogicalType::INTERVAL}, SetTypes::tstzset(), SetFunctions::Tstzset_shift)
        );
        duckdb::RegisterSerializedScalarFunction(loader,
            ScalarFunction("+", {SetTypes::tstzset(), LogicalType::INTERVAL}, SetTypes::tstzset(), SetFunctions::Tstzset_shift)
        );

        duckdb::RegisterSerializedScalarFunction(loader,  
            ScalarFunction("scale", {SetTypes::intset(), LogicalType::INTEGER}, SetTypes::intset(), SetFunctions::Numset_scale)
        );

        duckdb::RegisterSerializedScalarFunction(loader,  
            ScalarFunction("scale", {SetTypes::bigintset(), LogicalType::BIGINT}, SetTypes::bigintset(), SetFunctions::Numset_scale)
        );

        duckdb::RegisterSerializedScalarFunction(loader,  
            ScalarFunction("scale", {SetTypes::floatset(), LogicalType::DOUBLE}, SetTypes::floatset(), SetFunctions::Numset_scale)
        );
        
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("scale", {SetTypes::dateset(), LogicalType::INTEGER}, SetTypes::dateset(), SetFunctions::Numset_scale)
        ); 

        duckdb::RegisterSerializedScalarFunction(loader,  
            ScalarFunction("scale", {SetTypes::tstzset(), LogicalType::INTERVAL}, SetTypes::tstzset(), SetFunctions::Tstzset_scale)
        );

        duckdb::RegisterSerializedScalarFunction(loader,  
            ScalarFunction("shiftScale", {SetTypes::intset(), LogicalType::INTEGER, LogicalType::INTEGER}, SetTypes::intset(), SetFunctions::Numset_shift_scale)
        );

        duckdb::RegisterSerializedScalarFunction(loader,  
            ScalarFunction("shiftScale", {SetTypes::bigintset(), LogicalType::BIGINT, LogicalType::BIGINT}, SetTypes::bigintset(), SetFunctions::Numset_shift_scale)
        );

        duckdb::RegisterSerializedScalarFunction(loader,  
            ScalarFunction("shiftScale", {SetTypes::floatset(), LogicalType::DOUBLE, LogicalType::DOUBLE}, SetTypes::floatset(), SetFunctions::Numset_shift_scale)
        );
        
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("shiftScale", {SetTypes::dateset(), LogicalType::INTEGER, LogicalType::INTEGER}, SetTypes::dateset(), SetFunctions::Numset_shift_scale)
        );

        duckdb::RegisterSerializedScalarFunction(loader,  
            ScalarFunction("shiftScale", {SetTypes::tstzset(), LogicalType::INTERVAL, LogicalType::INTERVAL}, SetTypes::tstzset(), SetFunctions::Tstzset_shift_scale)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("floor", {SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Floatset_floor)                 
        );
        
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("ceil", {SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Floatset_ceil)                 
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("round", {SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Floatset_round)                 
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("round", {SetTypes::floatset(), LogicalType::INTEGER}, SetTypes::floatset(), SetFunctions::Floatset_round)                 
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("degrees", {SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Floatset_degrees)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("degrees", {SetTypes::floatset(), LogicalType::BOOLEAN}, SetTypes::floatset(), SetFunctions::Floatset_degrees)
        );
        
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("radians", {SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Floatset_radians)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("lower", {SetTypes::textset()}, SetTypes::textset(), SetFunctions::Textset_lower)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("upper", {SetTypes::textset()}, SetTypes::textset(), SetFunctions::Textset_upper)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("initcap", {SetTypes::textset()}, SetTypes::textset(), SetFunctions::Textset_initcap)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("+", {set_type, set_type}, set_type, SetFunctions::Union_set_set)
        );

        duckdb::RegisterSerializedScalarFunction(loader,
            ScalarFunction("asBinary", {set_type}, LogicalType::BLOB, SetFunctions::Set_as_binary)
        );

        duckdb::RegisterSerializedScalarFunction(loader,
            ScalarFunction("asHexWKB", {set_type}, LogicalType::VARCHAR, SetFunctions::Set_as_hexwkb)
        );

        // Per-type *FromBinary / *FromHexWKB constructors (e.g.
        // intsetFromBinary, tstzsetFromHexWKB).  set_from_wkb /
        // set_from_hexwkb are subtype-agnostic; the format encodes the
        // target type, so we just route every per-type alias to the same
        // executor.
        const std::string alias = set_type.ToString();
        duckdb::RegisterSerializedScalarFunction(loader,
            ScalarFunction(alias + "FromBinary", {LogicalType::BLOB},    set_type, SetFunctions::Set_from_binary));
        duckdb::RegisterSerializedScalarFunction(loader,
            ScalarFunction(alias + "FromHexWKB", {LogicalType::VARCHAR}, set_type, SetFunctions::Set_from_hexwkb));
    }

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("textset_cat", {LogicalType::VARCHAR, SetTypes::textset()}, SetTypes::textset(),
                       SetFunctions::Textcat_text_textset)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("textset_cat", {SetTypes::textset(), LogicalType::VARCHAR}, SetTypes::textset(),
                       SetFunctions::Textcat_textset_text)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("||", {LogicalType::VARCHAR, SetTypes::textset()}, SetTypes::textset(), SetFunctions::Textcat_text_textset)
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("||", {SetTypes::textset(), LogicalType::VARCHAR}, SetTypes::textset(), SetFunctions::Textcat_textset_text)
    );

    // --- set_contains / @> ---
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contains", {SetTypes::textset(), LogicalType::VARCHAR}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contains", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contains", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contains", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contains", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contains", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contains", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contains", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contains", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contains", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contains", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contains", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("@>", {SetTypes::textset(), LogicalType::VARCHAR}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("@>", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("@>", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("@>", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("@>", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("@>", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("@>", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("@>", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("@>", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("@>", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("@>", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SetFunctions::Contains_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("@>", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Contains_set_set));

    // --- set_contained / <@ ---
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contained", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contained", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contained", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contained", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contained", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contained", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contained", {LogicalType::VARCHAR, SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contained", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contained", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contained", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contained", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_contained", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {LogicalType::VARCHAR, SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Contained_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<@", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Contained_set_set));

    // --- set_overlaps / && ---
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overlaps", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overlaps", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overlaps", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overlaps", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overlaps", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overlaps", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&&", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&&", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&&", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&&", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&&", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&&", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Overlaps_set_set));

    // --- Position: set_left / << ---
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_left", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_left", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_left", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_left", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_left", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_left", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_left", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_left", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_left", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_left", {LogicalType::VARCHAR, SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_left", {SetTypes::textset(), LogicalType::VARCHAR}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_left", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_left", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_left", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_left", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_left", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_left", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_left", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {LogicalType::VARCHAR, SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {SetTypes::textset(), LogicalType::VARCHAR}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Left_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SetFunctions::Left_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<<", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Left_set_set));

    // --- set_right / >> ---
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_right", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_right", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_right", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_right", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_right", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_right", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_right", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_right", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_right", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_right", {LogicalType::VARCHAR, SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_right", {SetTypes::textset(), LogicalType::VARCHAR}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_right", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_right", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_right", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_right", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_right", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_right", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_right", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {LogicalType::VARCHAR, SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {SetTypes::textset(), LogicalType::VARCHAR}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Right_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SetFunctions::Right_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">>", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Right_set_set));

    // --- set_overleft / &< ---
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overleft", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overleft", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overleft", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overleft", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overleft", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overleft", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overleft", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overleft", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overleft", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overleft", {LogicalType::VARCHAR, SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overleft", {SetTypes::textset(), LogicalType::VARCHAR}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overleft", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overleft", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overleft", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overleft", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overleft", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overleft", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overleft", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {LogicalType::VARCHAR, SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {SetTypes::textset(), LogicalType::VARCHAR}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&<", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Overleft_set_set));

    // --- set_overright / &> ---
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overright", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overright", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overright", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overright", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overright", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overright", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overright", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overright", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overright", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overright", {LogicalType::VARCHAR, SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overright", {SetTypes::textset(), LogicalType::VARCHAR}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overright", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overright", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overright", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overright", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overright", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overright", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_overright", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {LogicalType::VARCHAR, SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {SetTypes::textset(), LogicalType::VARCHAR}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Overright_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::BOOLEAN, SetFunctions::Overright_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("&>", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Overright_set_set));

    // --- set_union / + (value forms) ---
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_union", {LogicalType::INTEGER, SetTypes::intset()}, SetTypes::intset(), SetFunctions::Union_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_union", {SetTypes::intset(), LogicalType::INTEGER}, SetTypes::intset(), SetFunctions::Union_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_union", {LogicalType::BIGINT, SetTypes::bigintset()}, SetTypes::bigintset(), SetFunctions::Union_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_union", {SetTypes::bigintset(), LogicalType::BIGINT}, SetTypes::bigintset(), SetFunctions::Union_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_union", {LogicalType::DOUBLE, SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Union_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_union", {SetTypes::floatset(), LogicalType::DOUBLE}, SetTypes::floatset(), SetFunctions::Union_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_union", {LogicalType::VARCHAR, SetTypes::textset()}, SetTypes::textset(), SetFunctions::Union_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_union", {SetTypes::textset(), LogicalType::VARCHAR}, SetTypes::textset(), SetFunctions::Union_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_union", {LogicalType::DATE, SetTypes::dateset()}, SetTypes::dateset(), SetFunctions::Union_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_union", {SetTypes::dateset(), LogicalType::DATE}, SetTypes::dateset(), SetFunctions::Union_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_union", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, SetTypes::tstzset(), SetFunctions::Union_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_union", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, SetTypes::tstzset(), SetFunctions::Union_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {LogicalType::INTEGER, SetTypes::intset()}, SetTypes::intset(), SetFunctions::Union_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {SetTypes::intset(), LogicalType::INTEGER}, SetTypes::intset(), SetFunctions::Union_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {LogicalType::BIGINT, SetTypes::bigintset()}, SetTypes::bigintset(), SetFunctions::Union_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {SetTypes::bigintset(), LogicalType::BIGINT}, SetTypes::bigintset(), SetFunctions::Union_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {LogicalType::DOUBLE, SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Union_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {SetTypes::floatset(), LogicalType::DOUBLE}, SetTypes::floatset(), SetFunctions::Union_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {LogicalType::VARCHAR, SetTypes::textset()}, SetTypes::textset(), SetFunctions::Union_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {SetTypes::textset(), LogicalType::VARCHAR}, SetTypes::textset(), SetFunctions::Union_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {LogicalType::DATE, SetTypes::dateset()}, SetTypes::dateset(), SetFunctions::Union_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {SetTypes::dateset(), LogicalType::DATE}, SetTypes::dateset(), SetFunctions::Union_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, SetTypes::tstzset(), SetFunctions::Union_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, SetTypes::tstzset(), SetFunctions::Union_set_value));

    // --- set_minus / - ---
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_minus", {LogicalType::INTEGER, SetTypes::intset()}, SetTypes::intset(), SetFunctions::Minus_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_minus", {SetTypes::intset(), LogicalType::INTEGER}, SetTypes::intset(), SetFunctions::Minus_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_minus", {SetTypes::intset(), SetTypes::intset()}, SetTypes::intset(), SetFunctions::Minus_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_minus", {LogicalType::BIGINT, SetTypes::bigintset()}, SetTypes::bigintset(), SetFunctions::Minus_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_minus", {SetTypes::bigintset(), LogicalType::BIGINT}, SetTypes::bigintset(), SetFunctions::Minus_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_minus", {SetTypes::bigintset(), SetTypes::bigintset()}, SetTypes::bigintset(), SetFunctions::Minus_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_minus", {LogicalType::DOUBLE, SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Minus_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_minus", {SetTypes::floatset(), LogicalType::DOUBLE}, SetTypes::floatset(), SetFunctions::Minus_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_minus", {SetTypes::floatset(), SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Minus_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_minus", {LogicalType::VARCHAR, SetTypes::textset()}, SetTypes::textset(), SetFunctions::Minus_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_minus", {SetTypes::textset(), LogicalType::VARCHAR}, SetTypes::textset(), SetFunctions::Minus_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_minus", {SetTypes::textset(), SetTypes::textset()}, SetTypes::textset(), SetFunctions::Minus_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_minus", {LogicalType::DATE, SetTypes::dateset()}, SetTypes::dateset(), SetFunctions::Minus_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_minus", {SetTypes::dateset(), LogicalType::DATE}, SetTypes::dateset(), SetFunctions::Minus_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_minus", {SetTypes::dateset(), SetTypes::dateset()}, SetTypes::dateset(), SetFunctions::Minus_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_minus", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, SetTypes::tstzset(), SetFunctions::Minus_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_minus", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, SetTypes::tstzset(), SetFunctions::Minus_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_minus", {SetTypes::tstzset(), SetTypes::tstzset()}, SetTypes::tstzset(), SetFunctions::Minus_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {LogicalType::INTEGER, SetTypes::intset()}, SetTypes::intset(), SetFunctions::Minus_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SetTypes::intset(), LogicalType::INTEGER}, SetTypes::intset(), SetFunctions::Minus_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SetTypes::intset(), SetTypes::intset()}, SetTypes::intset(), SetFunctions::Minus_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {LogicalType::BIGINT, SetTypes::bigintset()}, SetTypes::bigintset(), SetFunctions::Minus_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SetTypes::bigintset(), LogicalType::BIGINT}, SetTypes::bigintset(), SetFunctions::Minus_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SetTypes::bigintset(), SetTypes::bigintset()}, SetTypes::bigintset(), SetFunctions::Minus_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {LogicalType::DOUBLE, SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Minus_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SetTypes::floatset(), LogicalType::DOUBLE}, SetTypes::floatset(), SetFunctions::Minus_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SetTypes::floatset(), SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Minus_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {LogicalType::VARCHAR, SetTypes::textset()}, SetTypes::textset(), SetFunctions::Minus_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SetTypes::textset(), LogicalType::VARCHAR}, SetTypes::textset(), SetFunctions::Minus_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SetTypes::textset(), SetTypes::textset()}, SetTypes::textset(), SetFunctions::Minus_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {LogicalType::DATE, SetTypes::dateset()}, SetTypes::dateset(), SetFunctions::Minus_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SetTypes::dateset(), LogicalType::DATE}, SetTypes::dateset(), SetFunctions::Minus_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SetTypes::dateset(), SetTypes::dateset()}, SetTypes::dateset(), SetFunctions::Minus_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, SetTypes::tstzset(), SetFunctions::Minus_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, SetTypes::tstzset(), SetFunctions::Minus_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SetTypes::tstzset(), SetTypes::tstzset()}, SetTypes::tstzset(), SetFunctions::Minus_set_set));

    // --- set_intersection / * ---
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_intersection", {LogicalType::INTEGER, SetTypes::intset()}, SetTypes::intset(), SetFunctions::Intersect_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_intersection", {SetTypes::intset(), LogicalType::INTEGER}, SetTypes::intset(), SetFunctions::Intersect_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_intersection", {SetTypes::intset(), SetTypes::intset()}, SetTypes::intset(), SetFunctions::Intersect_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_intersection", {LogicalType::BIGINT, SetTypes::bigintset()}, SetTypes::bigintset(), SetFunctions::Intersect_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_intersection", {SetTypes::bigintset(), LogicalType::BIGINT}, SetTypes::bigintset(), SetFunctions::Intersect_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_intersection", {SetTypes::bigintset(), SetTypes::bigintset()}, SetTypes::bigintset(), SetFunctions::Intersect_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_intersection", {LogicalType::DOUBLE, SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Intersect_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_intersection", {SetTypes::floatset(), LogicalType::DOUBLE}, SetTypes::floatset(), SetFunctions::Intersect_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_intersection", {SetTypes::floatset(), SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Intersect_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_intersection", {LogicalType::VARCHAR, SetTypes::textset()}, SetTypes::textset(), SetFunctions::Intersect_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_intersection", {SetTypes::textset(), LogicalType::VARCHAR}, SetTypes::textset(), SetFunctions::Intersect_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_intersection", {SetTypes::textset(), SetTypes::textset()}, SetTypes::textset(), SetFunctions::Intersect_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_intersection", {LogicalType::DATE, SetTypes::dateset()}, SetTypes::dateset(), SetFunctions::Intersect_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_intersection", {SetTypes::dateset(), LogicalType::DATE}, SetTypes::dateset(), SetFunctions::Intersect_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_intersection", {SetTypes::dateset(), SetTypes::dateset()}, SetTypes::dateset(), SetFunctions::Intersect_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_intersection", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, SetTypes::tstzset(), SetFunctions::Intersect_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_intersection", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, SetTypes::tstzset(), SetFunctions::Intersect_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_intersection", {SetTypes::tstzset(), SetTypes::tstzset()}, SetTypes::tstzset(), SetFunctions::Intersect_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::INTEGER, SetTypes::intset()}, SetTypes::intset(), SetFunctions::Intersect_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SetTypes::intset(), LogicalType::INTEGER}, SetTypes::intset(), SetFunctions::Intersect_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SetTypes::intset(), SetTypes::intset()}, SetTypes::intset(), SetFunctions::Intersect_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::BIGINT, SetTypes::bigintset()}, SetTypes::bigintset(), SetFunctions::Intersect_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SetTypes::bigintset(), LogicalType::BIGINT}, SetTypes::bigintset(), SetFunctions::Intersect_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SetTypes::bigintset(), SetTypes::bigintset()}, SetTypes::bigintset(), SetFunctions::Intersect_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::DOUBLE, SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Intersect_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SetTypes::floatset(), LogicalType::DOUBLE}, SetTypes::floatset(), SetFunctions::Intersect_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SetTypes::floatset(), SetTypes::floatset()}, SetTypes::floatset(), SetFunctions::Intersect_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::VARCHAR, SetTypes::textset()}, SetTypes::textset(), SetFunctions::Intersect_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SetTypes::textset(), LogicalType::VARCHAR}, SetTypes::textset(), SetFunctions::Intersect_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SetTypes::textset(), SetTypes::textset()}, SetTypes::textset(), SetFunctions::Intersect_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::DATE, SetTypes::dateset()}, SetTypes::dateset(), SetFunctions::Intersect_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SetTypes::dateset(), LogicalType::DATE}, SetTypes::dateset(), SetFunctions::Intersect_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SetTypes::dateset(), SetTypes::dateset()}, SetTypes::dateset(), SetFunctions::Intersect_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, SetTypes::tstzset(), SetFunctions::Intersect_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, SetTypes::tstzset(), SetFunctions::Intersect_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SetTypes::tstzset(), SetTypes::tstzset()}, SetTypes::tstzset(), SetFunctions::Intersect_set_set));

    // --- set_distance / <-> ---
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_distance", {LogicalType::INTEGER, LogicalType::INTEGER}, LogicalType::INTEGER, SetFunctions::Distance_value_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_distance", {LogicalType::BIGINT, LogicalType::BIGINT}, LogicalType::BIGINT, SetFunctions::Distance_value_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_distance", {LogicalType::DOUBLE, LogicalType::DOUBLE}, LogicalType::DOUBLE, SetFunctions::Distance_value_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_distance", {LogicalType::DATE, LogicalType::DATE}, LogicalType::INTEGER, SetFunctions::Distance_value_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_distance", {LogicalType::TIMESTAMP_TZ, LogicalType::TIMESTAMP_TZ}, LogicalType::DOUBLE, SetFunctions::Distance_value_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_distance", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::INTEGER, SetFunctions::Distance_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_distance", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::INTEGER, SetFunctions::Distance_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_distance", {SetTypes::intset(), SetTypes::intset()}, LogicalType::INTEGER, SetFunctions::Distance_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_distance", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BIGINT, SetFunctions::Distance_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_distance", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BIGINT, SetFunctions::Distance_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_distance", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BIGINT, SetFunctions::Distance_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_distance", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::DOUBLE, SetFunctions::Distance_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_distance", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::DOUBLE, SetFunctions::Distance_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_distance", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::DOUBLE, SetFunctions::Distance_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_distance", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::INTEGER, SetFunctions::Distance_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_distance", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::INTEGER, SetFunctions::Distance_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_distance", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::INTEGER, SetFunctions::Distance_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_distance", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::DOUBLE, SetFunctions::Distance_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_distance", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::DOUBLE, SetFunctions::Distance_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_distance", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::DOUBLE, SetFunctions::Distance_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {LogicalType::INTEGER, LogicalType::INTEGER}, LogicalType::INTEGER, SetFunctions::Distance_value_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {LogicalType::BIGINT, LogicalType::BIGINT}, LogicalType::BIGINT, SetFunctions::Distance_value_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {LogicalType::DOUBLE, LogicalType::DOUBLE}, LogicalType::DOUBLE, SetFunctions::Distance_value_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {LogicalType::DATE, LogicalType::DATE}, LogicalType::INTEGER, SetFunctions::Distance_value_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {LogicalType::TIMESTAMP_TZ, LogicalType::TIMESTAMP_TZ}, LogicalType::DOUBLE, SetFunctions::Distance_value_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SetTypes::intset(), LogicalType::INTEGER}, LogicalType::INTEGER, SetFunctions::Distance_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {LogicalType::INTEGER, SetTypes::intset()}, LogicalType::INTEGER, SetFunctions::Distance_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SetTypes::intset(), SetTypes::intset()}, LogicalType::INTEGER, SetFunctions::Distance_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SetTypes::bigintset(), LogicalType::BIGINT}, LogicalType::BIGINT, SetFunctions::Distance_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {LogicalType::BIGINT, SetTypes::bigintset()}, LogicalType::BIGINT, SetFunctions::Distance_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BIGINT, SetFunctions::Distance_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SetTypes::floatset(), LogicalType::DOUBLE}, LogicalType::DOUBLE, SetFunctions::Distance_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {LogicalType::DOUBLE, SetTypes::floatset()}, LogicalType::DOUBLE, SetFunctions::Distance_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::DOUBLE, SetFunctions::Distance_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SetTypes::dateset(), LogicalType::DATE}, LogicalType::INTEGER, SetFunctions::Distance_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {LogicalType::DATE, SetTypes::dateset()}, LogicalType::INTEGER, SetFunctions::Distance_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::INTEGER, SetFunctions::Distance_set_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SetTypes::tstzset(), LogicalType::TIMESTAMP_TZ}, LogicalType::INTERVAL, SetFunctions::Distance_set_value));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {LogicalType::TIMESTAMP_TZ, SetTypes::tstzset()}, LogicalType::INTERVAL, SetFunctions::Distance_value_set));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::INTERVAL, SetFunctions::Distance_set_set));

    // --- set_eq / = ---
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_eq", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_eq", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_eq", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_eq", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_eq", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_eq", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("=", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("=", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("=", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("=", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("=", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("=", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_eq));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_ne", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_ne", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_ne", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_ne", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_ne", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_ne", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<>", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<>", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<>", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<>", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<>", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<>", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_ne));


    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_lt", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_lt", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_lt", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_lt", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_lt", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_lt", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_lt));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_le", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_le", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_le", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_le", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_le", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_le", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<=", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<=", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<=", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<=", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<=", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<=", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_le));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_gt", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_gt", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_gt", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_gt", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_gt", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_gt", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_gt));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_ge", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_ge", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_ge", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_ge", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_ge", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_ge", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">=", {SetTypes::intset(), SetTypes::intset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">=", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">=", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">=", {SetTypes::textset(), SetTypes::textset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">=", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(">=", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::BOOLEAN, SetFunctions::Set_ge));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_cmp", {SetTypes::intset(), SetTypes::intset()}, LogicalType::INTEGER, SetFunctions::Set_cmp));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_cmp", {SetTypes::bigintset(), SetTypes::bigintset()}, LogicalType::INTEGER, SetFunctions::Set_cmp));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_cmp", {SetTypes::floatset(), SetTypes::floatset()}, LogicalType::INTEGER, SetFunctions::Set_cmp));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_cmp", {SetTypes::textset(), SetTypes::textset()}, LogicalType::INTEGER, SetFunctions::Set_cmp));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_cmp", {SetTypes::dateset(), SetTypes::dateset()}, LogicalType::INTEGER, SetFunctions::Set_cmp));
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("set_cmp", {SetTypes::tstzset(), SetTypes::tstzset()}, LogicalType::INTEGER, SetFunctions::Set_cmp));
}

// --- Unnest ---
struct SetUnnestBindData : public TableFunctionData {
    string_t blob;
    MeosType set_type;
    LogicalType return_type;

    SetUnnestBindData(string_t blob, MeosType set_type, LogicalType return_type)
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

void SetTypes::RegisterSetUnnest(ExtensionLoader &loader) {
    for (auto &set_type : SetTypes::AllTypes()) {
        TableFunction fn("SetUnnest",
                         {set_type},
                         SetUnnestExec,
                         SetUnnestBind,
                         SetUnnestInit);
        loader.RegisterFunction(fn);
    }
}

// ============================================================================
// SetUnionAgg — aggregate that unions sets or scalar values into a Set.
//
// Overloads:
//   SetUnionAgg(int)      → intset      (via int_to_set + set_union_transfn)
//   SetUnionAgg(bigint)   → bigintset   (via bigint_to_set)
//   SetUnionAgg(float)    → floatset    (via float_to_set)
//   SetUnionAgg(date)     → dateset     (via date_to_set)
//   SetUnionAgg(intset)   → intset      (direct set_union_transfn)
//   SetUnionAgg(dateset)  → dateset
//   ... etc. for all Set types
// ============================================================================

namespace {

static inline Set *date_to_set_duckdb(DateADT d) {
    return date_to_set(ToMeosDate(duckdb::date_t(d)));
}

// MEOS `int64` is `long`; on macOS (LP64) `int64_t` is `long long`.
// Same width, distinct types — go through a forwarding wrapper so the
// template instantiates with a `int64_t`-typed function pointer.
static inline Set *bigint_to_set_duckdb(int64_t i) {
    return bigint_to_set(static_cast<int64>(i));
}

struct SetPtrState {
    Set *accumulated;
};

// Combine and Finalize are type-independent.
static inline void SetPtrCombine(const SetPtrState &source, SetPtrState &target) {
    if (!source.accumulated) {
        return;
    }
    int src_size = set_mem_size(source.accumulated);
    Set *src_copy = reinterpret_cast<Set *>(malloc(src_size));
    memcpy(src_copy, source.accumulated, src_size);
    if (!target.accumulated) {
        target.accumulated = src_copy;
    } else {
        Set *merged = set_union_transfn(target.accumulated, src_copy);
        free(src_copy);
        target.accumulated = merged;
    }
}

// Shared finalizer: calls set_union_finalfn to sort and deduplicate.
// set_union_finalfn calls pfree(state) internally (= free in MEOS standalone
// mode), so we null out state.accumulated to prevent a double-free in Destroy.
static inline void SetUnionFinalize(SetPtrState &state, string_t &target,
                                    AggregateFinalizeData &finalize_data) {
    if (!state.accumulated) { finalize_data.ReturnNull(); return; }
    Set *result = set_union_finalfn(state.accumulated);
    state.accumulated = nullptr;
    if (!result) { finalize_data.ReturnNull(); return; }
    int out_size = set_mem_size(result);
    target = finalize_data.ReturnString(
        string_t(reinterpret_cast<const char *>(result), out_size));
    free(result);
}

// SetUnionAgg(scalar) — Operation converts scalar to a single-element Set
// then calls set_union_transfn.
template <typename SCALAR_T, Set *(*TO_SET_FN)(SCALAR_T)>
struct SetUnionScalarFunction {
    template <class STATE>
    static void Initialize(STATE &state) { state.accumulated = nullptr; }

    static bool IgnoreNull() { return true; }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        Set *s = TO_SET_FN(static_cast<SCALAR_T>(input));
        state.accumulated = set_union_transfn(state.accumulated, s);
        free(s);
    }

    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input,
                                  AggregateUnaryInput &ui, idx_t /*count*/) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, ui);
    }

    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        SetPtrCombine(source, target);
    }

    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
        SetUnionFinalize(state, target, finalize_data);
    }

    template <class STATE>
    static void Destroy(STATE &state, AggregateInputData &) {
        if (state.accumulated) { free(state.accumulated); state.accumulated = nullptr; }
    }
};

// SetUnionAgg(Set) — Operation copies the blob and calls set_union_transfn.
struct SetUnionSetFunction {
    template <class STATE>
    static void Initialize(STATE &state) { state.accumulated = nullptr; }

    static bool IgnoreNull() { return true; }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        size_t size = input.GetSize();
        Set *s = reinterpret_cast<Set *>(malloc(size));
        memcpy(s, input.GetData(), size);
        state.accumulated = set_union_transfn(state.accumulated, s);
        free(s);
    }

    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input,
                                  AggregateUnaryInput &ui, idx_t /*count*/) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, ui);
    }

    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        SetPtrCombine(source, target);
    }

    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
        SetUnionFinalize(state, target, finalize_data);
    }

    template <class STATE>
    static void Destroy(STATE &state, AggregateInputData &) {
        if (state.accumulated) { free(state.accumulated); state.accumulated = nullptr; }
    }
};

} // anonymous namespace

void SetTypes::RegisterSetUnionAgg(ExtensionLoader &loader) {
    AggregateFunctionSet set_union_set("SetUnionAgg");

    // Scalar overloads: convert each value to a single-element Set.
    set_union_set.AddFunction(
        AggregateFunction::UnaryAggregateDestructor<SetPtrState, int32_t, string_t,
            SetUnionScalarFunction<int32_t, int_to_set>>(
            LogicalType::INTEGER, SetTypes::intset()));
    set_union_set.AddFunction(
        AggregateFunction::UnaryAggregateDestructor<SetPtrState, int64_t, string_t,
            SetUnionScalarFunction<int64_t, bigint_to_set_duckdb>>(
            LogicalType::BIGINT, SetTypes::bigintset()));
    set_union_set.AddFunction(
        AggregateFunction::UnaryAggregateDestructor<SetPtrState, double, string_t,
            SetUnionScalarFunction<double, float_to_set>>(
            LogicalType::DOUBLE, SetTypes::floatset()));
    set_union_set.AddFunction(
        AggregateFunction::UnaryAggregateDestructor<SetPtrState, date_t, string_t,
            SetUnionScalarFunction<DateADT, date_to_set_duckdb>>(
            LogicalType::DATE, SetTypes::dateset()));

    // Set-input overloads: union existing sets together.
    for (const auto &set_type : SetTypes::AllTypes()) {
        set_union_set.AddFunction(
            AggregateFunction::UnaryAggregateDestructor<SetPtrState, string_t, string_t,
                SetUnionSetFunction>(set_type, set_type));
    }

    loader.RegisterFunction(std::move(set_union_set));
}

} // namespace duckdb
