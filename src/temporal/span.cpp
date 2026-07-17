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
        auto base_type = SpanTypeMapping::GetChildType(span_type);         

        // Register: asText
        if (span_type == SpanTypes::floatspan()) {            
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
            ScalarFunction("intspan", {SpanTypes::floatspan()}, SpanTypes::intspan(), SpanFunctions::Floatspan_to_intspan)                 
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("floatspan", {SpanTypes::intspan()}, SpanTypes::floatspan(), SpanFunctions::Intspan_to_floatspan)                 
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("datespan", {SpanTypes::tstzspan()}, SpanTypes::datespan(), SpanFunctions::Tstzspan_to_datespan)                 
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("tstzspan", {SpanTypes::datespan()}, SpanTypes::tstzspan(), SpanFunctions::Datespan_to_tstzspan)                 
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("span", {SetTypes::intset()},SpanTypes::intspan(), SpanFunctions::Set_to_span)
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("span", {SetTypes::bigintset()},SpanTypes::bigintspan(), SpanFunctions::Set_to_span)
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("span", {SetTypes::floatset()},SpanTypes::floatspan(), SpanFunctions::Set_to_span)
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("span", {SetTypes::tstzset()},SpanTypes::tstzspan(), SpanFunctions::Set_to_span) 
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction("span", {SetTypes::dateset()},SpanTypes::datespan(), SpanFunctions::Set_to_span) 
        );

        if (span_type == SpanTypes::intspan() ||span_type == SpanTypes::datespan()){

            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("shift", {span_type, LogicalType::INTEGER}, span_type, SpanFunctions::Numspan_shift)
            ); 
            
            duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("scale", {span_type, LogicalType::INTERVAL}, span_type, SpanFunctions::Numspan_scale)
            );
            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction("shiftScale", {span_type, LogicalType::INTEGER, LogicalType::INTEGER}, span_type,
                               SpanFunctions::Numspan_shift_scale));

        }
        else if( span_type == SpanTypes::bigintspan() ){
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
        else if( span_type == SpanTypes::floatspan() ){
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
        else if( span_type == SpanTypes::tstzspan() ){
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
        
        // lower / upper on span types are generated from the catalog
        // (meos_setspan_accessor) in generated_temporal_udfs.cpp.

        duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("lowerInc",{span_type}, LogicalType::BOOLEAN, SpanFunctions::Span_lower_inc));
        duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("upperInc",{span_type}, LogicalType::BOOLEAN, SpanFunctions::Span_upper_inc));

        duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_hash", {span_type}, LogicalType::UINTEGER, SpanFunctions::Span_hash));
        duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_hash_extended", {span_type, LogicalType::BIGINT}, LogicalType::UBIGINT, SpanFunctions::Span_hash_extended));
        }
    
    // expand(intspan/bigintspan/floatspan, <numeric>) is generated from the catalog
    // (meos_setspan_transf) in generated_temporal_udfs.cpp. The datespan/tstzspan
    // forms below are hand-only (not generated for these types).
    duckdb::RegisterSerializedScalarFunction(loader,
            ScalarFunction("expand", {SpanTypes::datespan(), LogicalType::INTEGER}, SpanTypes::datespan(), SpanFunctions::Numspan_expand)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  
            ScalarFunction("expand", {SpanTypes::tstzspan(), LogicalType::INTERVAL}, SpanTypes::tstzspan(), SpanFunctions::Tstzspan_expand)
    );

    // width(intspan/bigintspan/floatspan) and duration(datespan/tstzspan) are
    // generated from the catalog (meos_setspan_accessor) in
    // generated_temporal_udfs.cpp.

    // spans(<set_type>) is generated from the catalog (set_spans) in generated_temporal_udfs.cpp.

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction("splitNSpans", {SetTypes::intset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::intspan()), SpanFunctions::Set_split_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitNSpans", {SetTypes::bigintset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::bigintspan()), SpanFunctions::Set_split_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitNSpans", {SetTypes::floatset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::floatspan()), SpanFunctions::Set_split_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitNSpans", {SetTypes::dateset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::datespan()), SpanFunctions::Set_split_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitNSpans", {SetTypes::tstzset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::tstzspan()), SpanFunctions::Set_split_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitEachNSpans", {SetTypes::intset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::intspan()), SpanFunctions::Set_split_each_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitEachNSpans", {SetTypes::bigintset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::bigintspan()), SpanFunctions::Set_split_each_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitEachNSpans", {SetTypes::floatset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::floatspan()), SpanFunctions::Set_split_each_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitEachNSpans", {SetTypes::dateset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::datespan()), SpanFunctions::Set_split_each_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitEachNSpans", {SetTypes::tstzset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::tstzspan()), SpanFunctions::Set_split_each_n_spans));

    // Lowercase-"spans" aliases matching MobilityDB's SQL surface
    // (`splitNspans` / `splitEachNspans`). The camelCase forms above
    // stay registered for back-compat with existing MobilityDuck callers.
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitNspans", {SetTypes::intset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::intspan()), SpanFunctions::Set_split_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitNspans", {SetTypes::bigintset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::bigintspan()), SpanFunctions::Set_split_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitNspans", {SetTypes::floatset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::floatspan()), SpanFunctions::Set_split_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitNspans", {SetTypes::dateset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::datespan()), SpanFunctions::Set_split_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitNspans", {SetTypes::tstzset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::tstzspan()), SpanFunctions::Set_split_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitEachNspans", {SetTypes::intset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::intspan()), SpanFunctions::Set_split_each_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitEachNspans", {SetTypes::bigintset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::bigintspan()), SpanFunctions::Set_split_each_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitEachNspans", {SetTypes::floatset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::floatspan()), SpanFunctions::Set_split_each_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitEachNspans", {SetTypes::dateset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::datespan()), SpanFunctions::Set_split_each_n_spans));
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction("splitEachNspans", {SetTypes::tstzset(), LogicalType::INTEGER},
                       LogicalType::LIST(SpanTypes::tstzspan()), SpanFunctions::Set_split_each_n_spans));

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


    
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_union", {SpanTypes::intspan(), LogicalType::INTEGER}, SpansetTypes::intspanset(), SpanFunctions::Union_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_union", {LogicalType::INTEGER, SpanTypes::intspan()}, SpansetTypes::intspanset(), SpanFunctions::Union_value_span)
    );
loader.RegisterFunction( ScalarFunction("span_union", {SpanTypes::intspan(), SpanTypes::intspan()}, SpansetTypes::intspanset(), SpanFunctions::Union_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {SpanTypes::intspan(), LogicalType::INTEGER}, SpansetTypes::intspanset(), SpanFunctions::Union_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {LogicalType::INTEGER, SpanTypes::intspan()}, SpansetTypes::intspanset(), SpanFunctions::Union_value_span)
    );
loader.RegisterFunction( ScalarFunction("+", {SpanTypes::intspan(), SpanTypes::intspan()}, SpansetTypes::intspanset(), SpanFunctions::Union_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_union", {SpanTypes::bigintspan(), LogicalType::BIGINT}, SpansetTypes::bigintspanset(), SpanFunctions::Union_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_union", {LogicalType::BIGINT, SpanTypes::bigintspan()}, SpansetTypes::bigintspanset(), SpanFunctions::Union_value_span)
    );
loader.RegisterFunction( ScalarFunction("span_union", {SpanTypes::bigintspan(), SpanTypes::bigintspan()}, SpansetTypes::bigintspanset(), SpanFunctions::Union_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {SpanTypes::bigintspan(), LogicalType::BIGINT}, SpansetTypes::bigintspanset(), SpanFunctions::Union_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {LogicalType::BIGINT, SpanTypes::bigintspan()}, SpansetTypes::bigintspanset(), SpanFunctions::Union_value_span)
    );
loader.RegisterFunction( ScalarFunction("+", {SpanTypes::bigintspan(), SpanTypes::bigintspan()}, SpansetTypes::bigintspanset(), SpanFunctions::Union_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_union", {SpanTypes::floatspan(), LogicalType::DOUBLE}, SpansetTypes::floatspanset(), SpanFunctions::Union_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_union", {LogicalType::DOUBLE, SpanTypes::floatspan()}, SpansetTypes::floatspanset(), SpanFunctions::Union_value_span)
    );
loader.RegisterFunction( ScalarFunction("span_union", {SpanTypes::floatspan(), SpanTypes::floatspan()}, SpansetTypes::floatspanset(), SpanFunctions::Union_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {SpanTypes::floatspan(), LogicalType::DOUBLE}, SpansetTypes::floatspanset(), SpanFunctions::Union_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {LogicalType::DOUBLE, SpanTypes::floatspan()}, SpansetTypes::floatspanset(), SpanFunctions::Union_value_span)
    );
loader.RegisterFunction( ScalarFunction("+", {SpanTypes::floatspan(), SpanTypes::floatspan()}, SpansetTypes::floatspanset(), SpanFunctions::Union_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_union", {SpanTypes::datespan(), LogicalType::DATE}, SpansetTypes::datespanset(), SpanFunctions::Union_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_union", {LogicalType::DATE, SpanTypes::datespan()}, SpansetTypes::datespanset(), SpanFunctions::Union_value_span)
    );
loader.RegisterFunction( ScalarFunction("span_union", {SpanTypes::datespan(), SpanTypes::datespan()}, SpansetTypes::datespanset(), SpanFunctions::Union_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {SpanTypes::datespan(), LogicalType::DATE}, SpansetTypes::datespanset(), SpanFunctions::Union_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {LogicalType::DATE, SpanTypes::datespan()}, SpansetTypes::datespanset(), SpanFunctions::Union_value_span)
    );
loader.RegisterFunction( ScalarFunction("+", {SpanTypes::datespan(), SpanTypes::datespan()}, SpansetTypes::datespanset(), SpanFunctions::Union_span_span)
    );  

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_union", {SpanTypes::tstzspan(), LogicalType::TIMESTAMP_TZ}, SpansetTypes::tstzspanset(), SpanFunctions::Union_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_union", {LogicalType::TIMESTAMP_TZ, SpanTypes::tstzspan()}, SpansetTypes::tstzspanset(), SpanFunctions::Union_value_span)
    );
loader.RegisterFunction( ScalarFunction("span_union", {SpanTypes::tstzspan(), SpanTypes::tstzspan()}, SpansetTypes::tstzspanset(), SpanFunctions::Union_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {SpanTypes::tstzspan(), LogicalType::TIMESTAMP_TZ}, SpansetTypes::tstzspanset(), SpanFunctions::Union_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("+", {LogicalType::TIMESTAMP_TZ, SpanTypes::tstzspan()}, SpansetTypes::tstzspanset(), SpanFunctions::Union_value_span)
    );
loader.RegisterFunction( ScalarFunction("+", {SpanTypes::tstzspan(), SpanTypes::tstzspan()}, SpansetTypes::tstzspanset(), SpanFunctions::Union_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {SpanTypes::intspan(), LogicalType::INTEGER}, SpanTypes::intspan(), SpanFunctions::Intersection_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {LogicalType::INTEGER, SpanTypes::intspan()}, SpanTypes::intspan(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {SpanTypes::intspan(), SpanTypes::intspan()}, SpanTypes::intspan(), SpanFunctions::Intersection_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::intspan(), LogicalType::INTEGER}, SpanTypes::intspan(), SpanFunctions::Intersection_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::INTEGER, SpanTypes::intspan()}, SpanTypes::intspan(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::intspan(), SpanTypes::intspan()}, SpanTypes::intspan(), SpanFunctions::Intersection_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {SpanTypes::bigintspan(), LogicalType::BIGINT}, SpanTypes::bigintspan(), SpanFunctions::Intersection_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {LogicalType::BIGINT, SpanTypes::bigintspan()}, SpanTypes::bigintspan(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {SpanTypes::bigintspan(), SpanTypes::bigintspan()}, SpanTypes::bigintspan(), SpanFunctions::Intersection_span_span) 
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::bigintspan(), LogicalType::BIGINT}, SpanTypes::bigintspan(), SpanFunctions::Intersection_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::BIGINT, SpanTypes::bigintspan()}, SpanTypes::bigintspan(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::bigintspan(), SpanTypes::bigintspan()}, SpanTypes::bigintspan(), SpanFunctions::Intersection_span_span) 
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {SpanTypes::floatspan(), LogicalType::DOUBLE}, SpanTypes::floatspan(), SpanFunctions::Intersection_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {LogicalType::DOUBLE, SpanTypes::floatspan()}, SpanTypes::floatspan(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {SpanTypes::floatspan(), SpanTypes::floatspan()}, SpanTypes::floatspan(), SpanFunctions::Intersection_span_span) 
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::floatspan(), LogicalType::DOUBLE}, SpanTypes::floatspan(), SpanFunctions::Intersection_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::DOUBLE, SpanTypes::floatspan()}, SpanTypes::floatspan(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::floatspan(), SpanTypes::floatspan()}, SpanTypes::floatspan(), SpanFunctions::Intersection_span_span) 
    );  

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {SpanTypes::datespan(), LogicalType::DATE}, SpanTypes::datespan(), SpanFunctions::Intersection_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {LogicalType::DATE, SpanTypes::datespan()}, SpanTypes::datespan(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {SpanTypes::datespan(), SpanTypes::datespan()}, SpanTypes::datespan(), SpanFunctions::Intersection_span_span) 
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::datespan(), LogicalType::DATE}, SpanTypes::datespan(), SpanFunctions::Intersection_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::DATE, SpanTypes::datespan()}, SpanTypes::datespan(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::datespan(), SpanTypes::datespan()}, SpanTypes::datespan(), SpanFunctions::Intersection_span_span) 
    );  
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {SpanTypes::tstzspan(), LogicalType::TIMESTAMP_TZ}, SpanTypes::tstzspan(), SpanFunctions::Intersection_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {LogicalType::TIMESTAMP_TZ, SpanTypes::tstzspan()}, SpanTypes::tstzspan(), SpanFunctions::Intersection_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_intersection", {SpanTypes::tstzspan(), SpanTypes::tstzspan()}, SpanTypes::tstzspan(), SpanFunctions::Intersection_span_span) 
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::tstzspan(), LogicalType::TIMESTAMP_TZ}, SpanTypes::tstzspan(), SpanFunctions::Intersection_span_value)  
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {LogicalType::TIMESTAMP_TZ, SpanTypes::tstzspan()}, SpanTypes::tstzspan(), SpanFunctions::Intersection_value_span)
    );  
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("*", {SpanTypes::tstzspan(), SpanTypes::tstzspan()}, SpanTypes::tstzspan(), SpanFunctions::Intersection_span_span) 
    );
    
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {SpanTypes::intspan(), LogicalType::INTEGER}, SpansetTypes::intspanset(), SpanFunctions::Minus_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {LogicalType::INTEGER, SpanTypes::intspan()}, SpansetTypes::intspanset(), SpanFunctions::Minus_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {SpanTypes::intspan(), SpanTypes::intspan()}, SpansetTypes::intspanset(), SpanFunctions::Minus_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::intspan(), LogicalType::INTEGER}, SpansetTypes::intspanset(), SpanFunctions::Minus_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {LogicalType::INTEGER, SpanTypes::intspan()}, SpansetTypes::intspanset(), SpanFunctions::Minus_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::intspan(), SpanTypes::intspan()}, SpansetTypes::intspanset(), SpanFunctions::Minus_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {SpanTypes::bigintspan(), LogicalType::BIGINT}, SpansetTypes::bigintspanset(), SpanFunctions::Minus_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {LogicalType::BIGINT, SpanTypes::bigintspan()}, SpansetTypes::bigintspanset(), SpanFunctions::Minus_value_span)    
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {SpanTypes::bigintspan(), SpanTypes::bigintspan()}, SpansetTypes::bigintspanset(), SpanFunctions::Minus_span_span) 
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::bigintspan(), LogicalType::BIGINT}, SpansetTypes::bigintspanset(), SpanFunctions::Minus_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {LogicalType::BIGINT, SpanTypes::bigintspan()}, SpansetTypes::bigintspanset(), SpanFunctions::Minus_value_span)    
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::bigintspan(), SpanTypes::bigintspan()}, SpansetTypes::bigintspanset(), SpanFunctions::Minus_span_span) 
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {SpanTypes::floatspan(), LogicalType::DOUBLE}, SpansetTypes::floatspanset(), SpanFunctions::Minus_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {LogicalType::DOUBLE, SpanTypes::floatspan()}, SpansetTypes::floatspanset(), SpanFunctions::Minus_value_span)    
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {SpanTypes::floatspan(), SpanTypes::floatspan()}, SpansetTypes::floatspanset(), SpanFunctions::Minus_span_span) 
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::floatspan(), LogicalType::DOUBLE}, SpansetTypes::floatspanset(), SpanFunctions::Minus_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {LogicalType::DOUBLE, SpanTypes::floatspan()}, SpansetTypes::floatspanset(), SpanFunctions::Minus_value_span)    
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::floatspan(), SpanTypes::floatspan()}, SpansetTypes::floatspanset(), SpanFunctions::Minus_span_span) 
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {SpanTypes::datespan(), LogicalType::DATE}, SpansetTypes::datespanset(), SpanFunctions::Minus_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {LogicalType::DATE, SpanTypes::datespan()}, SpansetTypes::datespanset(), SpanFunctions::Minus_value_span)    
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {SpanTypes::datespan(), SpanTypes::datespan()}, SpansetTypes::datespanset(), SpanFunctions::Minus_span_span) 
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::datespan(), LogicalType::DATE}, SpansetTypes::datespanset(), SpanFunctions::Minus_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {LogicalType::DATE, SpanTypes::datespan()}, SpansetTypes::datespanset(), SpanFunctions::Minus_value_span)    
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::datespan(), SpanTypes::datespan()}, SpansetTypes::datespanset(), SpanFunctions::Minus_span_span) 
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {SpanTypes::tstzspan(), LogicalType::TIMESTAMP_TZ}, SpansetTypes::tstzspanset(), SpanFunctions::Minus_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {LogicalType::TIMESTAMP_TZ, SpanTypes::tstzspan()}, SpansetTypes::tstzspanset(), SpanFunctions::Minus_value_span)    
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_minus", {SpanTypes::tstzspan(), SpanTypes::tstzspan()}, SpansetTypes::tstzspanset(), SpanFunctions::Minus_span_span) 
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::tstzspan(), LogicalType::TIMESTAMP_TZ}, SpansetTypes::tstzspanset(), SpanFunctions::Minus_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {LogicalType::TIMESTAMP_TZ, SpanTypes::tstzspan()}, SpansetTypes::tstzspanset(), SpanFunctions::Minus_value_span)    
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("-", {SpanTypes::tstzspan(), SpanTypes::tstzspan()}, SpansetTypes::tstzspanset(), SpanFunctions::Minus_span_span) 
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {SpanTypes::intspan(), LogicalType::INTEGER}, LogicalType::INTEGER, SpanFunctions::Distance_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {LogicalType::INTEGER, SpanTypes::intspan()}, LogicalType::INTEGER, SpanFunctions::Distance_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {SpanTypes::intspan(), SpanTypes::intspan()}, LogicalType::INTEGER, SpanFunctions::Distance_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SpanTypes::intspan(), LogicalType::INTEGER}, LogicalType::INTEGER, SpanFunctions::Distance_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {LogicalType::INTEGER, SpanTypes::intspan()}, LogicalType::INTEGER, SpanFunctions::Distance_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SpanTypes::intspan(), SpanTypes::intspan()}, LogicalType::INTEGER, SpanFunctions::Distance_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {SpanTypes::bigintspan(), LogicalType::BIGINT}, LogicalType::BIGINT, SpanFunctions::Distance_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {LogicalType::BIGINT, SpanTypes::bigintspan()}, LogicalType::BIGINT, SpanFunctions::Distance_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {SpanTypes::bigintspan(), SpanTypes::bigintspan()}, LogicalType::BIGINT, SpanFunctions::Distance_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SpanTypes::bigintspan(), LogicalType::BIGINT}, LogicalType::BIGINT, SpanFunctions::Distance_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {LogicalType::BIGINT, SpanTypes::bigintspan()}, LogicalType::BIGINT, SpanFunctions::Distance_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SpanTypes::bigintspan(), SpanTypes::bigintspan()}, LogicalType::BIGINT, SpanFunctions::Distance_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {SpanTypes::floatspan(), LogicalType::DOUBLE}, LogicalType::DOUBLE, SpanFunctions::Distance_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {LogicalType::DOUBLE, SpanTypes::floatspan()}, LogicalType::DOUBLE, SpanFunctions::Distance_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {SpanTypes::floatspan(), SpanTypes::floatspan()}, LogicalType::DOUBLE, SpanFunctions::Distance_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SpanTypes::floatspan(), LogicalType::DOUBLE}, LogicalType::DOUBLE, SpanFunctions::Distance_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {LogicalType::DOUBLE, SpanTypes::floatspan()}, LogicalType::DOUBLE, SpanFunctions::Distance_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SpanTypes::floatspan(), SpanTypes::floatspan()}, LogicalType::DOUBLE, SpanFunctions::Distance_span_span)
    );  

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {SpanTypes::datespan(), LogicalType::DATE}, LogicalType::INTEGER, SpanFunctions::Distance_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {LogicalType::DATE, SpanTypes::datespan()}, LogicalType::INTEGER, SpanFunctions::Distance_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {SpanTypes::datespan(), SpanTypes::datespan()}, LogicalType::INTEGER, SpanFunctions::Distance_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SpanTypes::datespan(), LogicalType::DATE}, LogicalType::INTEGER, SpanFunctions::Distance_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {LogicalType::DATE, SpanTypes::datespan()}, LogicalType::INTEGER, SpanFunctions::Distance_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SpanTypes::datespan(), SpanTypes::datespan()}, LogicalType::INTEGER, SpanFunctions::Distance_span_span)
    );

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {SpanTypes::tstzspan(), LogicalType::TIMESTAMP_TZ}, LogicalType::INTERVAL, SpanFunctions::Distance_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {LogicalType::TIMESTAMP_TZ, SpanTypes::tstzspan()}, LogicalType::INTERVAL, SpanFunctions::Distance_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("span_distance", {SpanTypes::tstzspan(), SpanTypes::tstzspan()}, LogicalType::INTERVAL, SpanFunctions::Distance_span_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SpanTypes::tstzspan(), LogicalType::TIMESTAMP_TZ}, LogicalType::INTERVAL, SpanFunctions::Distance_span_value)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {LogicalType::TIMESTAMP_TZ, SpanTypes::tstzspan()}, LogicalType::INTERVAL, SpanFunctions::Distance_value_span)
    );
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction("<->", {SpanTypes::tstzspan(), SpanTypes::tstzspan()}, LogicalType::INTERVAL, SpanFunctions::Distance_span_span)
    );
}

} // namespace duckdb

#ifndef MOBILITYDUCK_EXTENSION_TYPES
#endif
