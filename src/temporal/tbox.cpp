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
#include "mobilityduck/meos_exec_serial.hpp"
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
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {LogicalType::INTEGER, LogicalType::TIMESTAMP_TZ},
            tbox(),
            TboxFunctions::Number_timestamptz_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {LogicalType::DOUBLE, LogicalType::TIMESTAMP_TZ},
            tbox(),
            TboxFunctions::Number_timestamptz_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SpanTypes::intspan(), LogicalType::TIMESTAMP_TZ},
            tbox(),
            TboxFunctions::Numspan_timestamptz_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SpanTypes::floatspan(), LogicalType::TIMESTAMP_TZ},
            tbox(),
            TboxFunctions::Numspan_timestamptz_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {LogicalType::INTEGER, SpanTypes::tstzspan()},
            tbox(),
            TboxFunctions::Number_tstzspan_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {LogicalType::DOUBLE, SpanTypes::tstzspan()},
            tbox(),
            TboxFunctions::Number_tstzspan_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SpanTypes::intspan(), SpanTypes::tstzspan()},
            tbox(),
            TboxFunctions::Numspan_tstzspan_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SpanTypes::floatspan(), SpanTypes::tstzspan()},
            tbox(),
            TboxFunctions::Numspan_tstzspan_to_tbox
        )
    );


    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "@>",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Contains_tbox_tbox
        )
    );


    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<@",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Contained_tbox_tbox
        )
    );


    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&&",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overlaps_tbox_tbox
        )
    );


    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "~=",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Same_tbox_tbox   
        )
    );


    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "-|-",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Adjacent_tbox_tbox
        )
    );


    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<<",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Left_tbox_tbox
        )
    );


    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&<",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overleft_tbox_tbox
        )
    );


    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            ">>",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Right_tbox_tbox
        )
    );


    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&>",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overright_tbox_tbox
        )
    );


    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<<#",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Before_tbox_tbox
        )
    );


    // Error with #, DuckDB's lexer defines op_chars without #

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&<#",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overbefore_tbox_tbox
        )
    );


    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "#>>",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::After_tbox_tbox
        )
    );


    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "#&>",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overafter_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "*",
            {tbox(), tbox()},
            tbox(),
            TboxFunctions::Intersection_tbox_tbox
        )
    );






    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "cmp",
            {tbox(), tbox()},
            LogicalType::INTEGER,
            TboxFunctions::Tbox_cmp
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "=",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_eq
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<>",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_ne
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_lt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<=",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_le
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            ">=",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_ge
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            ">",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_gt
        )
    );

    // hash(tbox) / hashExtended(tbox, UBIGINT) are generated from the catalog
    // (bare canonical names) in generated_temporal_udfs.cpp.
}

} // namespace duckdb
