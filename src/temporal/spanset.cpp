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
        // asBinary / asHexWKB and the <spanset>FromText / FromBinary / FromHexWKB constructors
        // are generated from the catalog (meos_setspan_inout).

        duckdb::RegisterSerializedScalarFunction(loader,
            ScalarFunction("spanset", {LogicalType::LIST(child_type)}, spanset_type, SpansetFunctions::Spanset_constructor)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("spanset", {base_type}, spanset_type, SpansetFunctions::Value_to_spanset)
        );

        // spanset(<set>) and spanset(<span>) are generated from the catalog
        // (meos_setspan_conversion).

        




        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("memSize", {spanset_type}, LogicalType::INTEGER, SpansetFunctions::Spanset_mem_size)
        );

        // lower / upper on spanset types are generated from the catalog
        // (meos_setspan_accessor) in generated_temporal_udfs.cpp.

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

        // numSpans on spanset types is generated from the catalog
        // (meos_setspan_accessor) in generated_temporal_udfs.cpp.




        if (spanset_type == SpansetTypes::intspanset() ||spanset_type == SpansetTypes::datespanset()){

            


        }
        else if( spanset_type == SpansetTypes::bigintspanset() ){

        }
        else if( spanset_type == SpansetTypes::floatspanset() ){

            // floor/ceil/round/degrees/radians on floatspanset are float-base scalar
            // transforms generated from the catalog (generated_temporal_udfs.cpp), full
            // arity plus the shorter DEFAULT-arg overload via sqlSignatures argDefaults.
        }
        else if( spanset_type == SpansetTypes::tstzspanset() ){


        } 
        // spans(<spanset_type>) is generated from the catalog (spanset_spans) in generated_temporal_udfs.cpp.

        // Lowercase aliases matching MobilityDB's SQL surface

        // hash(spanset_type) / hashExtended(spanset_type, UBIGINT) are generated from
        // the catalog (meos_setspan_accessor) in generated_temporal_udfs.cpp.
    }
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("duration", {SpansetTypes::datespanset()}, LogicalType::INTERVAL, SpansetFunctions::Datespanset_duration)
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("duration", {SpansetTypes::tstzspanset()}, LogicalType::INTERVAL, SpansetFunctions::Tstzspanset_duration)
    );

    // duration(datespanset/tstzspanset, boolean) is generated from the catalog
    // (meos_setspan_accessor) in generated_temporal_udfs.cpp. The 1-arg forms
    // above are hand-only (not generated).

    // numDates / startDate / endDate on datespanset are generated from the
    // catalog (meos_setspan_accessor) in generated_temporal_udfs.cpp.



    // numTimestamps / startTimestamp / endTimestamp on tstzspanset are generated
    // from the catalog (meos_setspan_accessor) in generated_temporal_udfs.cpp.



}

} // namespace duckdb   
