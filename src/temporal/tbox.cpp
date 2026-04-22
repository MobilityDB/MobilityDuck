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
    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {LogicalType::INTEGER, LogicalType::TIMESTAMP_TZ},
            TBOX(),
            TboxFunctions::Number_timestamptz_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {LogicalType::DOUBLE, LogicalType::TIMESTAMP_TZ},
            TBOX(),
            TboxFunctions::Number_timestamptz_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {SpanTypes::INTSPAN(), LogicalType::TIMESTAMP_TZ},
            TBOX(),
            TboxFunctions::Numspan_timestamptz_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {SpanTypes::FLOATSPAN(), LogicalType::TIMESTAMP_TZ},
            TBOX(),
            TboxFunctions::Numspan_timestamptz_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {LogicalType::INTEGER, SpanTypes::TSTZSPAN()},
            TBOX(),
            TboxFunctions::Number_tstzspan_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {LogicalType::DOUBLE, SpanTypes::TSTZSPAN()},
            TBOX(),
            TboxFunctions::Number_tstzspan_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {SpanTypes::INTSPAN(), SpanTypes::TSTZSPAN()},
            TBOX(),
            TboxFunctions::Numspan_tstzspan_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {SpanTypes::FLOATSPAN(), SpanTypes::TSTZSPAN()},
            TBOX(),
            TboxFunctions::Numspan_tstzspan_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {LogicalType::INTEGER},
            TBOX(),
            TboxFunctions::Number_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {LogicalType::DOUBLE},
            TBOX(),
            TboxFunctions::Number_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {LogicalType::TIMESTAMP_TZ},
            TBOX(),
            TboxFunctions::Timestamptz_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {SetTypes::intset()},
            TBOX(),
            TboxFunctions::Set_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {SetTypes::floatset()},
            TBOX(),
            TboxFunctions::Set_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {SetTypes::tstzset()},
            TBOX(),
            TboxFunctions::Set_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {SpanTypes::INTSPAN()},
            TBOX(),
            TboxFunctions::Span_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {SpanTypes::FLOATSPAN()},
            TBOX(),
            TboxFunctions::Span_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {SpanTypes::TSTZSPAN()},
            TBOX(),
            TboxFunctions::Span_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "intspan",
            {TBOX()},
            SpanTypes::INTSPAN(),
            TboxFunctions::Tbox_to_intspan
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "floatspan",
            {TBOX()},
            SpanTypes::FLOATSPAN(),
            TboxFunctions::Tbox_to_floatspan
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "timeSpan",
            {TBOX()},
            SpanTypes::TSTZSPAN(),
            TboxFunctions::Tbox_to_tstzspan
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {SpansetTypes::intspanset()},
            TBOX(),
            TboxFunctions::Spanset_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {SpansetTypes::floatspanset()},
            TBOX(),
            TboxFunctions::Spanset_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {SpansetTypes::tstzspanset()},
            TBOX(),
            TboxFunctions::Spanset_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "hasX",
            {TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_hasx
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "hasT",
            {TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_hast
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "Xmin",
            {TBOX()},
            LogicalType::DOUBLE,
            TboxFunctions::Tbox_xmin
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "XminInc",
            {TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_xmin_inc
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "Xmax",
            {TBOX()},
            LogicalType::DOUBLE,
            TboxFunctions::Tbox_xmax
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "XmaxInc",
            {TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_xmax_inc
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "Tmin",
            {TBOX()},
            LogicalType::TIMESTAMP_TZ,
            TboxFunctions::Tbox_tmin
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "TminInc",
            {TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_tmin_inc
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "Tmax",
            {TBOX()},
            LogicalType::TIMESTAMP_TZ,
            TboxFunctions::Tbox_tmax
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "TmaxInc",
            {TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_tmax_inc
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "shiftValue",
            {TBOX(), LogicalType::INTEGER},
            TBOX(),
            TboxFunctions::Tbox_shift_value
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "shiftValue",
            {TBOX(), LogicalType::DOUBLE},
            TBOX(),
            TboxFunctions::Tbox_shift_value
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "shiftTime",
            {TBOX(), LogicalType::INTERVAL},
            TBOX(),
            TboxFunctions::Tbox_shift_time
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "scaleValue",
            {TBOX(), LogicalType::INTEGER},
            TBOX(),
            TboxFunctions::Tbox_scale_value
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "scaleValue",
            {TBOX(), LogicalType::DOUBLE},
            TBOX(),
            TboxFunctions::Tbox_scale_value
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "scaleTime",
            {TBOX(), LogicalType::INTERVAL},
            TBOX(),
            TboxFunctions::Tbox_scale_time
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "shiftScaleValue",
            {TBOX(), LogicalType::INTEGER, LogicalType::INTEGER},
            TBOX(),
            TboxFunctions::Tbox_shift_scale_value
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "shiftScaleValue",
            {TBOX(), LogicalType::DOUBLE, LogicalType::DOUBLE},
            TBOX(),
            TboxFunctions::Tbox_shift_scale_value
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "shiftScaleTime",
            {TBOX(), LogicalType::INTERVAL, LogicalType::INTERVAL},
            TBOX(),
            TboxFunctions::Tbox_shift_scale_time
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "expandValue",
            {TBOX(), LogicalType::INTEGER},
            TBOX(),
            TboxFunctions::Tbox_expand_value
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "expandValue",
            {TBOX(), LogicalType::DOUBLE},
            TBOX(),
            TboxFunctions::Tbox_expand_value
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "expandTime",
            {TBOX(), LogicalType::INTERVAL},
            TBOX(),
            TboxFunctions::Tbox_expand_time
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "round",
            {TBOX()},
            TBOX(),
            TboxFunctions::Tbox_round
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "round",
            {TBOX(), LogicalType::INTEGER},
            TBOX(),
            TboxFunctions::Tbox_round
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_contains",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Contains_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "@>",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Contains_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_contained",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Contained_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "<@",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Contained_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_overlaps",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overlaps_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "&&",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overlaps_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_same",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Same_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "~=",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Same_tbox_tbox   
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_adjacent",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Adjacent_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "-|-",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Adjacent_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_left",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Left_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "<<",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Left_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_overleft",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overleft_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "&<",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overleft_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_right",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Right_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            ">>",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Right_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_overright",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overright_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "&>",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overright_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_before",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Before_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "<<#",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Before_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_overbefore",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overbefore_tbox_tbox
        )
    );

    // Error with #, DuckDB's lexer defines op_chars without #

    loader.RegisterFunction(
        ScalarFunction(
            "&<#",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overbefore_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_after",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::After_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "#>>",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::After_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_overafter",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overafter_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "#&>",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overafter_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_union",
            {TBOX(), TBOX()},
            TBOX(),
            TboxFunctions::Union_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_intersection",
            {TBOX(), TBOX()},
            TBOX(),
            TboxFunctions::Intersection_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "+",
            {TBOX(), TBOX()},
            TBOX(),
            TboxFunctions::Union_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "*",
            {TBOX(), TBOX()},
            TBOX(),
            TboxFunctions::Intersection_tbox_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_eq",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_eq
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_ne",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_ne
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_lt",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_lt
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_le",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_le
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_ge",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_ge
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "tbox_gt",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_gt
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox_cmp",
            {TBOX(), TBOX()},
            LogicalType::INTEGER,
            TboxFunctions::Tbox_cmp
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "=",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_eq
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "<>",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_ne
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "<",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_lt
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "<=",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_le
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            ">=",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_ge
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            ">",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_gt
        )
    );
}

} // namespace duckdb