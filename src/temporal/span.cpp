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

void SpanTypes::RegisterScalarFunctions(ExtensionLoader &loader) {    
    for (const auto &span_type : SpanTypes::AllTypes()) {

        // asBinary / asHexWKB, the <span>FromText / FromBinary / FromHexWKB constructors and
        // the function form of the cast, <span>(text), are generated from the catalog
        // (meos_setspan_inout and the type's input function).

        // span(<set>) is generated from the catalog (meos_setspan_conversion).

        if( span_type == SpanTypes::tstzspan() ){
            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {span_type, LogicalType::INTERVAL}, span_type, SpanFunctions::Tstzspan_shift)
            );
        }
        
        // lower / upper on span types are generated from the catalog
        // (meos_setspan_accessor) in generated_temporal_udfs.cpp.

        duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("lowerInc",{span_type}, LogicalType::BOOLEAN, SpanFunctions::Span_lower_inc));
        duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("upperInc",{span_type}, LogicalType::BOOLEAN, SpanFunctions::Span_upper_inc));

        // hash(span_type) / hashExtended(span_type, UBIGINT) are generated from the
        // catalog (meos_setspan_accessor) in generated_temporal_udfs.cpp.
        }
    
    // expand(intspan/bigintspan/floatspan/tstzspan, ...) is generated from the catalog
    // (meos_setspan_transf) in generated_temporal_udfs.cpp; the datespan form is not.
    duckdb::RegisterSerializedScalarFunction(loader,
            ScalarFunction("expand", {SpanTypes::datespan(), LogicalType::INTEGER}, SpanTypes::datespan(), SpanFunctions::Numspan_expand)
    );

    // width(intspan/bigintspan/floatspan) and duration(datespan/tstzspan) are
    // generated from the catalog (meos_setspan_accessor) in
    // generated_temporal_udfs.cpp.

    // spans(<set_type>) is generated from the catalog (set_spans) in generated_temporal_udfs.cpp.



    // floor/ceil/round/degrees/radians on floatspan are float-base scalar transforms
    // generated from the catalog (generated_temporal_udfs.cpp), full arity plus the
    // shorter DEFAULT-arg overload via sqlSignatures argDefaults. Only round(DOUBLE),
    // the scalar base helper with no catalog signature, remains hand-registered.
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("round", {LogicalType::DOUBLE}, LogicalType::DOUBLE, SpanFunctions::Float_round)
    );
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("round", {LogicalType::DOUBLE, LogicalType::INTEGER}, LogicalType::DOUBLE, SpanFunctions::Float_round)
    );


    
RegisterMeosFunction(loader, ScalarFunction("spanUnion", {SpanTypes::intspan(), SpanTypes::intspan()}, SpansetTypes::intspanset(), SpanFunctions::Union_span_span)
    );
RegisterMeosFunction(loader, ScalarFunction("+", {SpanTypes::intspan(), SpanTypes::intspan()}, SpansetTypes::intspanset(), SpanFunctions::Union_span_span)
    );

RegisterMeosFunction(loader, ScalarFunction("spanUnion", {SpanTypes::bigintspan(), SpanTypes::bigintspan()}, SpansetTypes::bigintspanset(), SpanFunctions::Union_span_span)
    );
RegisterMeosFunction(loader, ScalarFunction("+", {SpanTypes::bigintspan(), SpanTypes::bigintspan()}, SpansetTypes::bigintspanset(), SpanFunctions::Union_span_span)
    );

RegisterMeosFunction(loader, ScalarFunction("spanUnion", {SpanTypes::floatspan(), SpanTypes::floatspan()}, SpansetTypes::floatspanset(), SpanFunctions::Union_span_span)
    );
RegisterMeosFunction(loader, ScalarFunction("+", {SpanTypes::floatspan(), SpanTypes::floatspan()}, SpansetTypes::floatspanset(), SpanFunctions::Union_span_span)
    );

RegisterMeosFunction(loader, ScalarFunction("spanUnion", {SpanTypes::datespan(), SpanTypes::datespan()}, SpansetTypes::datespanset(), SpanFunctions::Union_span_span)
    );
RegisterMeosFunction(loader, ScalarFunction("+", {SpanTypes::datespan(), SpanTypes::datespan()}, SpansetTypes::datespanset(), SpanFunctions::Union_span_span)
    );  

RegisterMeosFunction(loader, ScalarFunction("spanUnion", {SpanTypes::tstzspan(), SpanTypes::tstzspan()}, SpansetTypes::tstzspanset(), SpanFunctions::Union_span_span)
    );
RegisterMeosFunction(loader, ScalarFunction("+", {SpanTypes::tstzspan(), SpanTypes::tstzspan()}, SpansetTypes::tstzspanset(), SpanFunctions::Union_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("spanIntersection", {LogicalType::INTEGER, SpanTypes::intspan()}, SpanTypes::intspan(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::INTEGER, SpanTypes::intspan()}, SpanTypes::intspan(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::intspan(), SpanTypes::intspan()}, SpanTypes::intspan(), SpanFunctions::Intersection_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("spanIntersection", {LogicalType::BIGINT, SpanTypes::bigintspan()}, SpanTypes::bigintspan(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::BIGINT, SpanTypes::bigintspan()}, SpanTypes::bigintspan(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::bigintspan(), SpanTypes::bigintspan()}, SpanTypes::bigintspan(), SpanFunctions::Intersection_span_span) 
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("spanIntersection", {LogicalType::DOUBLE, SpanTypes::floatspan()}, SpanTypes::floatspan(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::DOUBLE, SpanTypes::floatspan()}, SpanTypes::floatspan(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::floatspan(), SpanTypes::floatspan()}, SpanTypes::floatspan(), SpanFunctions::Intersection_span_span) 
    );  

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("spanIntersection", {LogicalType::DATE, SpanTypes::datespan()}, SpanTypes::datespan(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::DATE, SpanTypes::datespan()}, SpanTypes::datespan(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::datespan(), SpanTypes::datespan()}, SpanTypes::datespan(), SpanFunctions::Intersection_span_span) 
    );  
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("spanIntersection", {LogicalType::TIMESTAMP_TZ, SpanTypes::tstzspan()}, SpanTypes::tstzspan(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::TIMESTAMP_TZ, SpanTypes::tstzspan()}, SpanTypes::tstzspan(), SpanFunctions::Intersection_value_span)
    );  
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::tstzspan(), SpanTypes::tstzspan()}, SpanTypes::tstzspan(), SpanFunctions::Intersection_span_span) 
    );
    
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("spanMinus", {SpanTypes::intspan(), SpanTypes::intspan()}, SpansetTypes::intspanset(), SpanFunctions::Minus_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::intspan(), SpanTypes::intspan()}, SpansetTypes::intspanset(), SpanFunctions::Minus_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("spanMinus", {SpanTypes::bigintspan(), SpanTypes::bigintspan()}, SpansetTypes::bigintspanset(), SpanFunctions::Minus_span_span) 
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::bigintspan(), SpanTypes::bigintspan()}, SpansetTypes::bigintspanset(), SpanFunctions::Minus_span_span) 
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("spanMinus", {SpanTypes::floatspan(), SpanTypes::floatspan()}, SpansetTypes::floatspanset(), SpanFunctions::Minus_span_span) 
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::floatspan(), SpanTypes::floatspan()}, SpansetTypes::floatspanset(), SpanFunctions::Minus_span_span) 
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("spanMinus", {SpanTypes::datespan(), SpanTypes::datespan()}, SpansetTypes::datespanset(), SpanFunctions::Minus_span_span) 
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::datespan(), SpanTypes::datespan()}, SpansetTypes::datespanset(), SpanFunctions::Minus_span_span) 
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("spanMinus", {SpanTypes::tstzspan(), SpanTypes::tstzspan()}, SpansetTypes::tstzspanset(), SpanFunctions::Minus_span_span) 
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::tstzspan(), SpanTypes::tstzspan()}, SpansetTypes::tstzspanset(), SpanFunctions::Minus_span_span) 
    );
}

} // namespace duckdb

#ifndef MOBILITYDUCK_EXTENSION_TYPES
#endif
