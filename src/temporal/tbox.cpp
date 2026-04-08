#include "meos_wrapper_simple.hpp"

#include "common.hpp"
#include "temporal/tbox.hpp"
#include "temporal/tbox_functions.hpp"
#include "temporal/spanset.hpp"

#include "duckdb/common/types/blob.hpp"
// #include "duckdb/common/exception.hpp"
// #include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
// #include "duckdb/common/extension_type_info.hpp"

namespace duckdb {

LogicalType TboxType::TBOX() {
    LogicalType type(LogicalTypeId::BLOB);
    type.SetAlias("TBOX");
    return type;
}

void TboxType::RegisterType(DatabaseInstance &instance) {
    ExtensionUtil::RegisterType(instance, "TBOX", TBOX());
}

void TboxType::RegisterCastFunctions(DatabaseInstance &instance) {
    ExtensionUtil::RegisterCastFunction(
        instance,
        LogicalType::VARCHAR,
        TBOX(),
        TboxFunctions::Tbox_in
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        TBOX(),
        LogicalType::VARCHAR,
        TboxFunctions::Tbox_out
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        LogicalType::INTEGER,
        TBOX(),
        TboxFunctions::Number_to_tbox_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        LogicalType::DOUBLE,
        TBOX(),
        TboxFunctions::Number_to_tbox_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        LogicalType::TIMESTAMP_TZ,
        TBOX(),
        TboxFunctions::Timestamptz_to_tbox_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        SetTypes::intset(),
        TBOX(),
        TboxFunctions::Set_to_tbox_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        SetTypes::floatset(),
        TBOX(),
        TboxFunctions::Set_to_tbox_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        SetTypes::tstzset(),
        TBOX(),
        TboxFunctions::Set_to_tbox_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        SpanTypes::INTSPAN(),
        TBOX(),
        TboxFunctions::Span_to_tbox_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        SpanTypes::FLOATSPAN(),
        TBOX(),
        TboxFunctions::Span_to_tbox_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        SpanTypes::TSTZSPAN(),
        TBOX(),
        TboxFunctions::Span_to_tbox_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        TBOX(),
        SpanTypes::INTSPAN(),
        TboxFunctions::Tbox_to_intspan_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        TBOX(),
        SpanTypes::FLOATSPAN(),
        TboxFunctions::Tbox_to_floatspan_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        TBOX(),
        SpanTypes::TSTZSPAN(),
        TboxFunctions::Tbox_to_tstzspan_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        SpansetTypes::intspanset(),
        TBOX(),
        TboxFunctions::Spanset_to_tbox_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        SpansetTypes::floatspanset(),
        TBOX(),
        TboxFunctions::Spanset_to_tbox_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        SpansetTypes::tstzspanset(),
        TBOX(),
        TboxFunctions::Spanset_to_tbox_cast
    );
}

void TboxType::RegisterScalarFunctions(DatabaseInstance &instance) {
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {LogicalType::INTEGER, LogicalType::TIMESTAMP_TZ},
            TBOX(),
            TboxFunctions::Number_timestamptz_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {LogicalType::DOUBLE, LogicalType::TIMESTAMP_TZ},
            TBOX(),
            TboxFunctions::Number_timestamptz_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {SpanTypes::INTSPAN(), LogicalType::TIMESTAMP_TZ},
            TBOX(),
            TboxFunctions::Numspan_timestamptz_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {SpanTypes::FLOATSPAN(), LogicalType::TIMESTAMP_TZ},
            TBOX(),
            TboxFunctions::Numspan_timestamptz_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {LogicalType::INTEGER, SpanTypes::TSTZSPAN()},
            TBOX(),
            TboxFunctions::Number_tstzspan_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {LogicalType::DOUBLE, SpanTypes::TSTZSPAN()},
            TBOX(),
            TboxFunctions::Number_tstzspan_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {SpanTypes::INTSPAN(), SpanTypes::TSTZSPAN()},
            TBOX(),
            TboxFunctions::Numspan_tstzspan_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {SpanTypes::FLOATSPAN(), SpanTypes::TSTZSPAN()},
            TBOX(),
            TboxFunctions::Numspan_tstzspan_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {LogicalType::INTEGER},
            TBOX(),
            TboxFunctions::Number_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {LogicalType::DOUBLE},
            TBOX(),
            TboxFunctions::Number_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {LogicalType::TIMESTAMP_TZ},
            TBOX(),
            TboxFunctions::Timestamptz_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {SetTypes::intset()},
            TBOX(),
            TboxFunctions::Set_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {SetTypes::floatset()},
            TBOX(),
            TboxFunctions::Set_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {SetTypes::tstzset()},
            TBOX(),
            TboxFunctions::Set_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {SpanTypes::INTSPAN()},
            TBOX(),
            TboxFunctions::Span_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {SpanTypes::FLOATSPAN()},
            TBOX(),
            TboxFunctions::Span_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {SpanTypes::TSTZSPAN()},
            TBOX(),
            TboxFunctions::Span_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "intspan",
            {TBOX()},
            SpanTypes::INTSPAN(),
            TboxFunctions::Tbox_to_intspan
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "floatspan",
            {TBOX()},
            SpanTypes::FLOATSPAN(),
            TboxFunctions::Tbox_to_floatspan
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "timeSpan",
            {TBOX()},
            SpanTypes::TSTZSPAN(),
            TboxFunctions::Tbox_to_tstzspan
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {SpansetTypes::intspanset()},
            TBOX(),
            TboxFunctions::Spanset_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {SpansetTypes::floatspanset()},
            TBOX(),
            TboxFunctions::Spanset_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {SpansetTypes::tstzspanset()},
            TBOX(),
            TboxFunctions::Spanset_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "hasX",
            {TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_hasx
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "hasT",
            {TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_hast
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "Xmin",
            {TBOX()},
            LogicalType::DOUBLE,
            TboxFunctions::Tbox_xmin
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "XminInc",
            {TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_xmin_inc
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "Xmax",
            {TBOX()},
            LogicalType::DOUBLE,
            TboxFunctions::Tbox_xmax
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "XmaxInc",
            {TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_xmax_inc
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "Tmin",
            {TBOX()},
            LogicalType::TIMESTAMP_TZ,
            TboxFunctions::Tbox_tmin
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "TminInc",
            {TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_tmin_inc
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "Tmax",
            {TBOX()},
            LogicalType::TIMESTAMP_TZ,
            TboxFunctions::Tbox_tmax
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "TmaxInc",
            {TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_tmax_inc
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "shiftValue",
            {TBOX(), LogicalType::INTEGER},
            TBOX(),
            TboxFunctions::Tbox_shift_value
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "shiftValue",
            {TBOX(), LogicalType::DOUBLE},
            TBOX(),
            TboxFunctions::Tbox_shift_value
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "shiftTime",
            {TBOX(), LogicalType::INTERVAL},
            TBOX(),
            TboxFunctions::Tbox_shift_time
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "scaleValue",
            {TBOX(), LogicalType::INTEGER},
            TBOX(),
            TboxFunctions::Tbox_scale_value
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "scaleValue",
            {TBOX(), LogicalType::DOUBLE},
            TBOX(),
            TboxFunctions::Tbox_scale_value
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "scaleTime",
            {TBOX(), LogicalType::INTERVAL},
            TBOX(),
            TboxFunctions::Tbox_scale_time
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "shiftScaleValue",
            {TBOX(), LogicalType::INTEGER, LogicalType::INTEGER},
            TBOX(),
            TboxFunctions::Tbox_shift_scale_value
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "shiftScaleValue",
            {TBOX(), LogicalType::DOUBLE, LogicalType::DOUBLE},
            TBOX(),
            TboxFunctions::Tbox_shift_scale_value
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "shiftScaleTime",
            {TBOX(), LogicalType::INTERVAL, LogicalType::INTERVAL},
            TBOX(),
            TboxFunctions::Tbox_shift_scale_time
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "expandValue",
            {TBOX(), LogicalType::INTEGER},
            TBOX(),
            TboxFunctions::Tbox_expand_value
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "expandValue",
            {TBOX(), LogicalType::DOUBLE},
            TBOX(),
            TboxFunctions::Tbox_expand_value
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "expandTime",
            {TBOX(), LogicalType::INTERVAL},
            TBOX(),
            TboxFunctions::Tbox_expand_time
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "round",
            {TBOX()},
            TBOX(),
            TboxFunctions::Tbox_round
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "round",
            {TBOX(), LogicalType::INTEGER},
            TBOX(),
            TboxFunctions::Tbox_round
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_contains",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Contains_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "@>",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Contains_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_contained",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Contained_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "<@",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Contained_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_overlaps",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overlaps_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "&&",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overlaps_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_same",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Same_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "~=",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Same_tbox_tbox   
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_adjacent",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Adjacent_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "-|-",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Adjacent_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_left",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Left_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "<<",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Left_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_overleft",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overleft_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "&<",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overleft_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_right",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Right_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            ">>",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Right_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_overright",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overright_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "&>",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overright_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_before",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Before_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "<<#",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Before_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_overbefore",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overbefore_tbox_tbox
        )
    );

    // Error with #, DuckDB's lexer defines op_chars without #

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "&<#",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overbefore_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_after",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::After_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "#>>",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::After_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_overafter",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overafter_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "#&>",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overafter_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_union",
            {TBOX(), TBOX()},
            TBOX(),
            TboxFunctions::Union_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_intersection",
            {TBOX(), TBOX()},
            TBOX(),
            TboxFunctions::Intersection_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "+",
            {TBOX(), TBOX()},
            TBOX(),
            TboxFunctions::Union_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "*",
            {TBOX(), TBOX()},
            TBOX(),
            TboxFunctions::Intersection_tbox_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_eq",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_eq
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_ne",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_ne
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_lt",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_lt
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_le",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_le
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_ge",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_ge
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_gt",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_gt
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox_cmp",
            {TBOX(), TBOX()},
            LogicalType::INTEGER,
            TboxFunctions::Tbox_cmp
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "=",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_eq
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "<>",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_ne
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "<",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_lt
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "<=",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_le
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            ">=",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_ge
        )
    );
    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            ">",
            {TBOX(), TBOX()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_gt
        )
    );
}

} // namespace duckdb