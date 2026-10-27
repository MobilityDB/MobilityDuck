#include "meos_wrapper_simple.hpp"

#include "common.hpp"
#include "temporal/tbox.hpp"
#include "temporal/tbox_functions.hpp"
#include "temporal/spanset.hpp"

#include "duckdb/common/types/blob.hpp"
// #include "duckdb/common/exception.hpp"
// #include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "mobilityduck/meos_registration.hpp"
// #include "duckdb/common/extension_type_info.hpp"

namespace duckdb {

LogicalType TboxType::tbox() {
    LogicalType type(LogicalTypeId::BLOB);
    type.SetAlias("tbox");
    return type;
}

void TboxType::RegisterType(ExtensionLoader &loader) {
    loader.RegisterType( "tbox", tbox());
}

void TboxType::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, 
        LogicalType::VARCHAR,
        tbox(),
        TboxFunctions::Tbox_in
    );

    RegisterMeosCastFunction(loader, 
        tbox(),
        LogicalType::VARCHAR,
        TboxFunctions::Tbox_out
    );

    RegisterMeosCastFunction(loader, 
        LogicalType::INTEGER,
        tbox(),
        TboxFunctions::Number_to_tbox_cast
    );

    RegisterMeosCastFunction(loader, 
        LogicalType::DOUBLE,
        tbox(),
        TboxFunctions::Number_to_tbox_cast
    );

    RegisterMeosCastFunction(loader, 
        LogicalType::TIMESTAMP_TZ,
        tbox(),
        TboxFunctions::Timestamptz_to_tbox_cast
    );

    RegisterMeosCastFunction(loader, 
        SetTypes::intset(),
        tbox(),
        TboxFunctions::Set_to_tbox_cast
    );

    RegisterMeosCastFunction(loader, 
        SetTypes::floatset(),
        tbox(),
        TboxFunctions::Set_to_tbox_cast
    );

    RegisterMeosCastFunction(loader, 
        SetTypes::tstzset(),
        tbox(),
        TboxFunctions::Set_to_tbox_cast
    );

    RegisterMeosCastFunction(loader, 
        SpanTypes::intspan(),
        tbox(),
        TboxFunctions::Span_to_tbox_cast
    );

    RegisterMeosCastFunction(loader, 
        SpanTypes::floatspan(),
        tbox(),
        TboxFunctions::Span_to_tbox_cast
    );

    RegisterMeosCastFunction(loader, 
        SpanTypes::tstzspan(),
        tbox(),
        TboxFunctions::Span_to_tbox_cast
    );

    RegisterMeosCastFunction(loader, 
        tbox(),
        SpanTypes::intspan(),
        TboxFunctions::Tbox_to_intspan_cast
    );

    RegisterMeosCastFunction(loader, 
        tbox(),
        SpanTypes::floatspan(),
        TboxFunctions::Tbox_to_floatspan_cast
    );

    RegisterMeosCastFunction(loader, 
        tbox(),
        SpanTypes::tstzspan(),
        TboxFunctions::Tbox_to_tstzspan_cast
    );

    RegisterMeosCastFunction(loader, 
        SpansetTypes::intspanset(),
        tbox(),
        TboxFunctions::Spanset_to_tbox_cast
    );

    RegisterMeosCastFunction(loader, 
        SpansetTypes::floatspanset(),
        tbox(),
        TboxFunctions::Spanset_to_tbox_cast
    );

    RegisterMeosCastFunction(loader, 
        SpansetTypes::tstzspanset(),
        tbox(),
        TboxFunctions::Spanset_to_tbox_cast
    );
}

void TboxType::RegisterScalarFunctions(ExtensionLoader &loader) {
    // hash(tbox) / hashExtended(tbox, UBIGINT) are generated from the catalog
    // (bare canonical names) in generated_temporal_udfs.cpp.
}

} // namespace duckdb
