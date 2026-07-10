#include "temporal/spanset.hpp"
#include "temporal/spanset_functions.hpp"
#include "temporal/span.hpp"
#include "temporal/set.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include "time_util.hpp"
#include "mobilityduck/meos_exec_serial.hpp"


extern "C" {     
    #include "meos.h"    
    #include "meos_internal.h"   
    #include "meos_geo.h"
}

namespace duckdb {

// Collection type registration (accessors, RegisterTypes, AllTypes, alias->MeosType,
// GetChildType/GetSetType/GetBaseType) is generated from the catalog MeosType enum
// into src/generated/generated_type_registration.cpp.

// --- Register Cast ---
void SpansetTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    for (const auto &spanset_type : SpansetTypes::AllTypes()) {
        RegisterMeosCastFunction(loader, 
            spanset_type,                      
            LogicalType::VARCHAR,   
            SpansetFunctions::Spanset_to_text   
        ); // Blob to text
        RegisterMeosCastFunction(loader, 
            LogicalType::VARCHAR, 
            spanset_type,                                    
            SpansetFunctions::Text_to_spanset   
        ); // text to blob
        
        auto base_type = SpansetTypeMapping::GetBaseType(spanset_type);
        RegisterMeosCastFunction(loader, 
            base_type,
            spanset_type,
            SpansetFunctions::Value_to_spanset_cast
        );

        auto set_type = SpansetTypeMapping::GetSetType(spanset_type);        
        RegisterMeosCastFunction(loader, 
            set_type,
            spanset_type,
            SpansetFunctions::Set_to_spanset_cast
        );
        auto child_type = SpansetTypeMapping::GetChildType(spanset_type); // span
        RegisterMeosCastFunction(loader, 
            child_type,
            spanset_type,
            SpansetFunctions::Span_to_spanset_cast
        );

        RegisterMeosCastFunction(loader, 
            spanset_type,
            child_type,
            SpansetFunctions::Spanset_to_span_cast
        );

        RegisterMeosCastFunction(loader, 
            SpansetTypes::intspanset(),
            SpansetTypes::floatspanset(),
            SpansetFunctions::Intspanset_to_floatspanset_cast
        );

        RegisterMeosCastFunction(loader, 
            SpansetTypes::floatspanset(),
            SpansetTypes::intspanset(),
            SpansetFunctions::Floatspanset_to_intspanset_cast
        );

        RegisterMeosCastFunction(loader, 
            SpansetTypes::datespanset(),
            SpansetTypes::tstzspanset(),
            SpansetFunctions::Datespanset_to_tstzspanset_cast
        );

        RegisterMeosCastFunction(loader, 
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
            duckdb::RegisterSerializedScalarFunction(loader,  // asText(floatset)
                ScalarFunction("asText", {spanset_type}, LogicalType::VARCHAR, SpansetFunctions::Spanset_as_text)
            );
            
            duckdb::RegisterSerializedScalarFunction(loader,  // asText(floatset, int)
                ScalarFunction("asText", {spanset_type, LogicalType::INTEGER}, LogicalType::VARCHAR, SpansetFunctions::Spanset_as_text)
            );
        } else {            
            duckdb::RegisterSerializedScalarFunction(loader,  // All other set types
                ScalarFunction("asText", {spanset_type}, LogicalType::VARCHAR, SpansetFunctions::Spanset_as_text)
            );
        }

        // asBinary / asHexWKB and *FromBinary / *FromHexWKB — spanset_as_wkb /
        // spanset_from_wkb are subtype-agnostic; the format encodes the
        // spanset type, so each per-type alias routes to the same executor.
        const std::string ss_alias = StringUtil::Lower(spanset_type.ToString());
        duckdb::RegisterSerializedScalarFunction(loader,
            ScalarFunction("asBinary", {spanset_type}, LogicalType::BLOB,    SpansetFunctions::Spanset_as_binary));
        duckdb::RegisterSerializedScalarFunction(loader,
            ScalarFunction("asHexWKB", {spanset_type}, LogicalType::VARCHAR, SpansetFunctions::Spanset_as_hexwkb));
        duckdb::RegisterSerializedScalarFunction(loader,
            ScalarFunction(ss_alias + "FromBinary", {LogicalType::BLOB},    spanset_type, SpansetFunctions::Spanset_from_binary));
        duckdb::RegisterSerializedScalarFunction(loader,
            ScalarFunction(ss_alias + "FromHexWKB", {LogicalType::VARCHAR}, spanset_type, SpansetFunctions::Spanset_from_hexwkb));

        duckdb::RegisterSerializedScalarFunction(loader,
            ScalarFunction("spanset", {LogicalType::LIST(child_type)}, spanset_type, SpansetFunctions::Spanset_constructor)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("spanset", {base_type}, spanset_type, SpansetFunctions::Value_to_spanset)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("spanset", {SpansetTypeMapping::GetSetType(spanset_type)}, spanset_type, SpansetFunctions::Set_to_spanset)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("spanset", {child_type}, spanset_type, SpansetFunctions::Span_to_spanset)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("span", {spanset_type}, child_type, SpansetFunctions::Spanset_to_span)
        );
        
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("intspanset", {SpansetTypes::floatspanset()}, SpansetTypes::intspanset(), SpansetFunctions::Floatspanset_to_intspanset)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("floatspanset", {SpansetTypes::intspanset()}, SpansetTypes::floatspanset(), SpansetFunctions::Intspanset_to_floatspanset)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("datespanset", {SpansetTypes::tstzspanset()}, SpansetTypes::datespanset(), SpansetFunctions::Tstzspanset_to_datespanset)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("tstzspanset", {SpansetTypes::datespanset()}, SpansetTypes::tstzspanset(), SpansetFunctions::Datespanset_to_tstzspanset)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("memSize", {spanset_type}, LogicalType::INTEGER, SpansetFunctions::Spanset_mem_size)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("lower", {spanset_type}, base_type, SpansetFunctions::Spanset_lower)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("upper", {spanset_type}, base_type, SpansetFunctions::Spanset_upper)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("lowerInc", {spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_lower_inc)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("upperInc", {spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_upper_inc)
        );

        if (spanset_type == SpansetTypes::intspanset() || spanset_type == SpansetTypes::floatspanset() || spanset_type == SpansetTypes::bigintspanset()) {
            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction("width", {spanset_type}, base_type, SpansetFunctions::Numspanset_width)
            );

            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction("width", {spanset_type, LogicalType::BOOLEAN}, base_type, SpansetFunctions::Numspanset_width)
            );
        }

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("numSpans", {spanset_type}, LogicalType::INTEGER, SpansetFunctions::Spanset_num_spans)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("startSpan", {spanset_type}, child_type, SpansetFunctions::Spanset_start_span)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("endSpan", {spanset_type}, child_type, SpansetFunctions::Spanset_end_span)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("spanN", {spanset_type, LogicalType::INTEGER}, child_type, SpansetFunctions::Spanset_span_n)
        );

        if (spanset_type == SpansetTypes::intspanset() ||spanset_type == SpansetTypes::datespanset()){

            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("shift", {spanset_type, LogicalType::INTEGER}, spanset_type, SpansetFunctions::Numspanset_shift)
            ); 
            
            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("scale", {spanset_type, LogicalType::INTERVAL}, spanset_type, SpansetFunctions::Numspanset_scale)
            );

            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction("shiftScale", {spanset_type, LogicalType::INTEGER, LogicalType::INTEGER}, spanset_type,
                               SpansetFunctions::Numspanset_shift_scale));

        }
        else if( spanset_type == SpansetTypes::bigintspanset() ){
            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("shift", {spanset_type, LogicalType::BIGINT}, spanset_type, SpansetFunctions::Numspanset_shift)
            ); 

            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("scale", {spanset_type, LogicalType::INTERVAL}, spanset_type, SpansetFunctions::Numspanset_scale)
            );
            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction("shiftScale", {spanset_type, LogicalType::BIGINT, LogicalType::BIGINT}, spanset_type, SpansetFunctions::Numspanset_shift_scale)
            );    
        }
        else if( spanset_type == SpansetTypes::floatspanset() ){
            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("shift", {spanset_type, LogicalType::DOUBLE}, spanset_type, SpansetFunctions::Numspanset_shift)
            ); 
            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("scale", {spanset_type, LogicalType::INTERVAL}, spanset_type, SpansetFunctions::Numspanset_scale)
            );
            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction("shiftScale", {spanset_type, LogicalType::DOUBLE, LogicalType::DOUBLE}, spanset_type, SpansetFunctions::Numspanset_shift_scale)
            );

            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("floor", {spanset_type}, spanset_type, SpansetFunctions::Floatspanset_floor)
            );
            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("ceil", {spanset_type}, spanset_type, SpansetFunctions::Floatspanset_ceil)
            );
            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("round", {spanset_type}, spanset_type, SpansetFunctions::Floatspanset_round)
            );

            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("round", {spanset_type, LogicalType::INTEGER}, spanset_type, SpansetFunctions::Floatspanset_round)
            );
            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("degrees", {spanset_type}, spanset_type, SpansetFunctions::Floatspanset_degrees)
            );
            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("radians", {spanset_type}, spanset_type, SpansetFunctions::Floatspanset_radians)
            );

        }
        else if( spanset_type == SpansetTypes::tstzspanset() ){
            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("shift", {spanset_type, LogicalType::INTERVAL}, spanset_type, SpansetFunctions::Tstzspanset_shift)
            ); 

            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("scale", {spanset_type, LogicalType::INTERVAL}, spanset_type, SpansetFunctions::Tstzspanset_scale)
            );
            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction("shiftScale", {spanset_type, LogicalType::INTERVAL, LogicalType::INTERVAL}, spanset_type, SpansetFunctions::Tstzspanset_shift_scale)
            );

        } 
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("spans", {spanset_type}, LogicalType::LIST(child_type), SpansetFunctions::Spanset_spans)
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("splitNSpans", {spanset_type, LogicalType::INTEGER}, LogicalType::LIST(child_type), SpansetFunctions::Spanset_split_n_spans)
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("splitEachNSpans", {spanset_type, LogicalType::INTEGER}, LogicalType::LIST(child_type), SpansetFunctions::Spanset_split_each_n_spans)
        );

        // Lowercase aliases matching MobilityDB's SQL surface
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("splitNspans", {spanset_type, LogicalType::INTEGER}, LogicalType::LIST(child_type), SpansetFunctions::Spanset_split_n_spans)
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("splitEachNspans", {spanset_type, LogicalType::INTEGER}, LogicalType::LIST(child_type), SpansetFunctions::Spanset_split_each_n_spans)
        );

        // Hash
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("spanset_hash", {spanset_type}, LogicalType::UINTEGER, SpansetFunctions::Spanset_hash)
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("spanset_hash_extended", {spanset_type, LogicalType::BIGINT}, LogicalType::UBIGINT, SpansetFunctions::Spanset_hash_extended)
        );

        // comparison operators
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("spanset_eq", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_eq)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("=", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_eq)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("spanset_ne", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_ne)
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("<>", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_ne)
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("spanset_le", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_le)
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("<=", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_le)
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("spanset_lt", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_lt)
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("<", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_lt)
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("spanset_ge", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_ge)
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(">=", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_ge)
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("spanset_gt", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_gt)
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(">", {spanset_type, spanset_type}, LogicalType::BOOLEAN, SpansetFunctions::Spanset_gt)
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("spanset_cmp", {spanset_type, spanset_type}, LogicalType::INTEGER, SpansetFunctions::Spanset_cmp)
        );
    }
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("duration", {SpansetTypes::datespanset()}, LogicalType::INTERVAL, SpansetFunctions::Datespanset_duration)
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("duration", {SpansetTypes::tstzspanset()}, LogicalType::INTERVAL, SpansetFunctions::Tstzspanset_duration)
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("duration", {SpansetTypes::datespanset(), LogicalType::BOOLEAN}, LogicalType::INTERVAL, SpansetFunctions::Datespanset_duration)
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("duration", {SpansetTypes::tstzspanset(), LogicalType::BOOLEAN}, LogicalType::INTERVAL, SpansetFunctions::Tstzspanset_duration)
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("numDates", {SpansetTypes::datespanset()}, LogicalType::INTEGER, SpansetFunctions::Datespanset_num_dates)
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("startDate", {SpansetTypes::datespanset()}, LogicalType::DATE, SpansetFunctions::Datespanset_start_date)
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("endDate", {SpansetTypes::datespanset()}, LogicalType::DATE, SpansetFunctions::Datespanset_end_date)
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("dateN", {SpansetTypes::datespanset(), LogicalType::INTEGER}, LogicalType::DATE, SpansetFunctions::Datespanset_date_n)
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("dates", {SpansetTypes::datespanset()}, SetTypes::dateset(), SpansetFunctions::Datespanset_dates)
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("numTimestamps", {SpansetTypes::tstzspanset()}, LogicalType::INTEGER, SpansetFunctions::Tstzspanset_num_timestamps)
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("startTimestamp", {SpansetTypes::tstzspanset()}, LogicalType::TIMESTAMP_TZ, SpansetFunctions::Tstzspanset_start_timestamptz)
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("endTimestamp", {SpansetTypes::tstzspanset()}, LogicalType::TIMESTAMP_TZ, SpansetFunctions::Tstzspanset_end_timestamptz)
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("timestampN", {SpansetTypes::tstzspanset(), LogicalType::INTEGER}, LogicalType::TIMESTAMP_TZ, SpansetFunctions::Tstzspanset_timestamptz_n)
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("timestamps", {SpansetTypes::tstzspanset()}, SetTypes::tstzset(), SpansetFunctions::Tstzspanset_timestamps)
    );

}

} // namespace duckdb   
