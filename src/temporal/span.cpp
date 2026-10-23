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

// Collection type registration (accessors, RegisterTypes, AllTypes, alias->MeosType,
// GetChildType) is generated from the catalog MeosType enum into
// src/generated/generated_type_registration.cpp.

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
            SpanTypes::intspan(),
            SpanTypes::floatspan(),
            SpanFunctions::Intspan_to_floatspan_cast // intspan -> floatspan 
        );

        RegisterMeosCastFunction(loader, 
            SpanTypes::floatspan(),
            SpanTypes::intspan(),
            SpanFunctions::Floatspan_to_intspan_cast // floatspan -> intspan
        );
        
        RegisterMeosCastFunction(loader, 
            SpanTypes::datespan(),
            SpanTypes::tstzspan(),
            SpanFunctions::Datespan_to_tstzspan_cast // datespan -> tstzspan
        );
        
        RegisterMeosCastFunction(loader, 
            SpanTypes::tstzspan(),
            SpanTypes::datespan(),
            SpanFunctions::Tstzspan_to_datespan_cast // tstzspan -> datespan 
        );

        RegisterMeosCastFunction(loader, 
            SetTypes::intset(),
            SpanTypes::intspan(),
            SpanFunctions::Set_to_span_cast // intset -> intspan
         );
        RegisterMeosCastFunction(loader, 
            SetTypes::bigintset(),
            SpanTypes::bigintspan(),
            SpanFunctions::Set_to_span_cast // bigintset -> bigintspan
         );
        RegisterMeosCastFunction(loader, 
            SetTypes::floatset(),
            SpanTypes::floatspan(),
            SpanFunctions::Set_to_span_cast // floatset -> floatspan
         );
        RegisterMeosCastFunction(loader, 
            SetTypes::tstzset(),
            SpanTypes::tstzspan(),
            SpanFunctions::Set_to_span_cast // tstzset -> tstzspan
         );

        // Scalar value -> span casts
        RegisterMeosCastFunction(loader, LogicalType::INTEGER,      SpanTypes::intspan(),    SpanFunctions::Value_to_span_cast);
        RegisterMeosCastFunction(loader, LogicalType::BIGINT,       SpanTypes::bigintspan(), SpanFunctions::Value_to_span_cast);
        RegisterMeosCastFunction(loader, LogicalType::DOUBLE,       SpanTypes::floatspan(),  SpanFunctions::Value_to_span_cast);
        RegisterMeosCastFunction(loader, LogicalType::DATE,         SpanTypes::datespan(),   SpanFunctions::Value_to_span_cast);
        RegisterMeosCastFunction(loader, LogicalType::TIMESTAMP_TZ, SpanTypes::tstzspan(),   SpanFunctions::Value_to_span_cast);
    }
}

} // namespace duckdb

#ifndef MOBILITYDUCK_EXTENSION_TYPES
#endif
