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

LogicalType TboxType::TBOX() {
    LogicalType type(LogicalTypeId::BLOB);
    type.SetAlias("TBOX");
    return type;
}

void TboxType::RegisterType(ExtensionLoader &loader) {
    loader.RegisterType( "TBOX", TBOX());
}

void TboxType::RegisterCastFunctions(ExtensionLoader &loader) {
    loader.RegisterCastFunction(
        LogicalType::VARCHAR,
        TBOX(),
        TboxFunctions::Tbox_in
    );

    loader.RegisterCastFunction(
        TBOX(),
        LogicalType::VARCHAR,
        TboxFunctions::Tbox_out
    );

    loader.RegisterCastFunction(
        LogicalType::INTEGER,
        TBOX(),
        TboxFunctions::Number_to_tbox_cast
    );

    loader.RegisterCastFunction(
        LogicalType::DOUBLE,
        TBOX(),
        TboxFunctions::Number_to_tbox_cast
    );

    loader.RegisterCastFunction(
        LogicalType::TIMESTAMP_TZ,
        TBOX(),
        TboxFunctions::Timestamptz_to_tbox_cast
    );

    loader.RegisterCastFunction(
        SetTypes::intset(),
        TBOX(),
        TboxFunctions::Set_to_tbox_cast
    );

    loader.RegisterCastFunction(
        SetTypes::floatset(),
        TBOX(),
        TboxFunctions::Set_to_tbox_cast
    );

    loader.RegisterCastFunction(
        SetTypes::tstzset(),
        TBOX(),
        TboxFunctions::Set_to_tbox_cast
    );

    loader.RegisterCastFunction(
        SpanTypes::INTSPAN(),
        TBOX(),
        TboxFunctions::Span_to_tbox_cast
    );

    loader.RegisterCastFunction(
        SpanTypes::FLOATSPAN(),
        TBOX(),
        TboxFunctions::Span_to_tbox_cast
    );

    loader.RegisterCastFunction(
        SpanTypes::TSTZSPAN(),
        TBOX(),
        TboxFunctions::Span_to_tbox_cast
    );

    loader.RegisterCastFunction(
        TBOX(),
        SpanTypes::INTSPAN(),
        TboxFunctions::Tbox_to_intspan_cast
    );

    loader.RegisterCastFunction(
        TBOX(),
        SpanTypes::FLOATSPAN(),
        TboxFunctions::Tbox_to_floatspan_cast
    );

    loader.RegisterCastFunction(
        TBOX(),
        SpanTypes::TSTZSPAN(),
        TboxFunctions::Tbox_to_tstzspan_cast
    );

    loader.RegisterCastFunction(
        SpansetTypes::intspanset(),
        TBOX(),
        TboxFunctions::Spanset_to_tbox_cast
    );

    loader.RegisterCastFunction(
        SpansetTypes::floatspanset(),
        TBOX(),
        TboxFunctions::Spanset_to_tbox_cast
    );

    loader.RegisterCastFunction(
        SpansetTypes::tstzspanset(),
        TBOX(),
        TboxFunctions::Spanset_to_tbox_cast
    );
}

void TboxType::RegisterScalarFunctions(ExtensionLoader &loader) {
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {LogicalType::INTEGER, LogicalType::TIMESTAMP_TZ},
            TBOX(),
            TboxFunctions::Number_timestamptz_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {LogicalType::DOUBLE, LogicalType::TIMESTAMP_TZ},
            TBOX(),
            TboxFunctions::Number_timestamptz_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SpanTypes::INTSPAN(), LogicalType::TIMESTAMP_TZ},
            TBOX(),
            TboxFunctions::Numspan_timestamptz_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SpanTypes::FLOATSPAN(), LogicalType::TIMESTAMP_TZ},
            TBOX(),
            TboxFunctions::Numspan_timestamptz_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {LogicalType::INTEGER, SpanTypes::TSTZSPAN()},
            TBOX(),
            TboxFunctions::Number_tstzspan_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {LogicalType::DOUBLE, SpanTypes::TSTZSPAN()},
            TBOX(),
            TboxFunctions::Number_tstzspan_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SpanTypes::INTSPAN(), SpanTypes::TSTZSPAN()},
            TBOX(),
            TboxFunctions::Numspan_tstzspan_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SpanTypes::FLOATSPAN(), SpanTypes::TSTZSPAN()},
            TBOX(),
            TboxFunctions::Numspan_tstzspan_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {LogicalType::INTEGER},
            TBOX(),
            TboxFunctions::Number_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {LogicalType::DOUBLE},
            TBOX(),
            TboxFunctions::Number_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {LogicalType::TIMESTAMP_TZ},
            TBOX(),
            TboxFunctions::Timestamptz_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SetTypes::intset()},
            TBOX(),
            TboxFunctions::Set_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SetTypes::floatset()},
            TBOX(),
            TboxFunctions::Set_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SetTypes::tstzset()},
            TBOX(),
            TboxFunctions::Set_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SpanTypes::INTSPAN()},
            TBOX(),
            TboxFunctions::Span_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SpanTypes::FLOATSPAN()},
            TBOX(),
            TboxFunctions::Span_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SpanTypes::TSTZSPAN()},
            TBOX(),
            TboxFunctions::Span_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "intspan",
            {TBOX()},
            SpanTypes::INTSPAN(),
            TboxFunctions::Tbox_to_intspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "floatspan",
            {TBOX()},
            SpanTypes::FLOATSPAN(),
            TboxFunctions::Tbox_to_floatspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "timeSpan",
            {TBOX()},
            SpanTypes::TSTZSPAN(),
            TboxFunctions::Tbox_to_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SpansetTypes::intspanset()},
            TBOX(),
            TboxFunctions::Spanset_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SpansetTypes::floatspanset()},
            TBOX(),
            TboxFunctions::Spanset_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SpansetTypes::tstzspanset()},
            TBOX(),
            TboxFunctions::Spanset_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "hasX",
            {TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_hasx
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "hasT",
            {TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_hast
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Xmin",
            {TBOX()},
            LogicalType::DOUBLE,
            TboxFunctions::Tbox_xmin
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "XminInc",
            {TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_xmin_inc
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Xmax",
            {TBOX()},
            LogicalType::DOUBLE,
            TboxFunctions::Tbox_xmax
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "XmaxInc",
            {TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_xmax_inc
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Tmin",
            {TBOX()},
            LogicalType::TIMESTAMP_TZ,
            TboxFunctions::Tbox_tmin
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "TminInc",
            {TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_tmin_inc
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Tmax",
            {TBOX()},
            LogicalType::TIMESTAMP_TZ,
            TboxFunctions::Tbox_tmax
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "TmaxInc",
            {TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_tmax_inc
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "shiftValue",
            {TBOX(), LogicalType::INTEGER},
            TBOX(),
            TboxFunctions::Tbox_shift_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "shiftValue",
            {TBOX(), LogicalType::DOUBLE},
            TBOX(),
            TboxFunctions::Tbox_shift_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "shiftTime",
            {TBOX(), LogicalType::INTERVAL},
            TBOX(),
            TboxFunctions::Tbox_shift_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "scaleValue",
            {TBOX(), LogicalType::INTEGER},
            TBOX(),
            TboxFunctions::Tbox_scale_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "scaleValue",
            {TBOX(), LogicalType::DOUBLE},
            TBOX(),
            TboxFunctions::Tbox_scale_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "scaleTime",
            {TBOX(), LogicalType::INTERVAL},
            TBOX(),
            TboxFunctions::Tbox_scale_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "shiftScaleValue",
            {TBOX(), LogicalType::INTEGER, LogicalType::INTEGER},
            TBOX(),
            TboxFunctions::Tbox_shift_scale_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "shiftScaleValue",
            {TBOX(), LogicalType::DOUBLE, LogicalType::DOUBLE},
            TBOX(),
            TboxFunctions::Tbox_shift_scale_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "shiftScaleTime",
            {TBOX(), LogicalType::INTERVAL, LogicalType::INTERVAL},
            TBOX(),
            TboxFunctions::Tbox_shift_scale_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "expandValue",
            {TBOX(), LogicalType::INTEGER},
            TBOX(),
            TboxFunctions::Tbox_expand_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "expandValue",
            {TBOX(), LogicalType::DOUBLE},
            TBOX(),
            TboxFunctions::Tbox_expand_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "expandTime",
            {TBOX(), LogicalType::INTERVAL},
            TBOX(),
            TboxFunctions::Tbox_expand_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "round",
            {TBOX()},
            TBOX(),
            TboxFunctions::Tbox_round
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "round",
            {TBOX(), LogicalType::INTEGER},
            TBOX(),
            TboxFunctions::Tbox_round
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_contains",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Contains_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "@>",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Contains_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_contained",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Contained_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<@",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Contained_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_overlaps",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overlaps_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&&",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overlaps_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_same",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Same_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "~=",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Same_tbox_tbox   
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_adjacent",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Adjacent_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "-|-",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Adjacent_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_left",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Left_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<<",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Left_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_overleft",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overleft_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&<",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overleft_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_right",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Right_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            ">>",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Right_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_overright",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overright_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&>",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overright_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_before",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Before_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<<#",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Before_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_overbefore",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overbefore_tbox_tbox
        )
    );

    // Error with #, DuckDB's lexer defines op_chars without #

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "&<#",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overbefore_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_after",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::After_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "#>>",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::After_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_overafter",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overafter_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "#&>",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overafter_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_union",
            {TBOX(), TBOX()},
            TBOX(),
            TboxFunctions::Union_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_intersection",
            {TBOX(), TBOX()},
            TBOX(),
            TboxFunctions::Intersection_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "+",
            {TBOX(), TBOX()},
            TBOX(),
            TboxFunctions::Union_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "*",
            {TBOX(), TBOX()},
            TBOX(),
            TboxFunctions::Intersection_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_eq",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_eq
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_ne",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_ne
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_lt",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_lt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_le",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_le
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_ge",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_ge
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_gt",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_gt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_cmp",
            {TBOX(), TBOX()},
            LogicalType::INTEGER,
            TboxFunctions::Tbox_cmp
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "=",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_eq
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<>",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_ne
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_lt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "<=",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_le
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            ">=",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_ge
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            ">",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_gt
        )
    );
}

} // namespace duckdb