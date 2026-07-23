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
            "tbox",
            {LogicalType::INTEGER},
            tbox(),
            TboxFunctions::Number_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {LogicalType::DOUBLE},
            tbox(),
            TboxFunctions::Number_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {LogicalType::TIMESTAMP_TZ},
            tbox(),
            TboxFunctions::Timestamptz_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SetTypes::intset()},
            tbox(),
            TboxFunctions::Set_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SetTypes::floatset()},
            tbox(),
            TboxFunctions::Set_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SetTypes::tstzset()},
            tbox(),
            TboxFunctions::Set_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SpanTypes::intspan()},
            tbox(),
            TboxFunctions::Span_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SpanTypes::floatspan()},
            tbox(),
            TboxFunctions::Span_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SpanTypes::tstzspan()},
            tbox(),
            TboxFunctions::Span_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "intspan",
            {tbox()},
            SpanTypes::intspan(),
            TboxFunctions::Tbox_to_intspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "floatspan",
            {tbox()},
            SpanTypes::floatspan(),
            TboxFunctions::Tbox_to_floatspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "timeSpan",
            {tbox()},
            SpanTypes::tstzspan(),
            TboxFunctions::Tbox_to_tstzspan
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SpansetTypes::intspanset()},
            tbox(),
            TboxFunctions::Spanset_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SpansetTypes::floatspanset()},
            tbox(),
            TboxFunctions::Spanset_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {SpansetTypes::tstzspanset()},
            tbox(),
            TboxFunctions::Spanset_to_tbox
        )
    );

    // hasX/hasT are generated from the catalog (box unary scalar accessors).

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "Xmin",
            {tbox()},
            LogicalType::DOUBLE,
            TboxFunctions::Tbox_xmin
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "XminInc",
            {tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_xmin_inc
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Xmax",
            {tbox()},
            LogicalType::DOUBLE,
            TboxFunctions::Tbox_xmax
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "XmaxInc",
            {tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_xmax_inc
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Tmin",
            {tbox()},
            LogicalType::TIMESTAMP_TZ,
            TboxFunctions::Tbox_tmin
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "TminInc",
            {tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_tmin_inc
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "Tmax",
            {tbox()},
            LogicalType::TIMESTAMP_TZ,
            TboxFunctions::Tbox_tmax
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "TmaxInc",
            {tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_tmax_inc
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "shiftValue",
            {tbox(), LogicalType::INTEGER},
            tbox(),
            TboxFunctions::Tbox_shift_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "shiftValue",
            {tbox(), LogicalType::DOUBLE},
            tbox(),
            TboxFunctions::Tbox_shift_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "shiftTime",
            {tbox(), LogicalType::INTERVAL},
            tbox(),
            TboxFunctions::Tbox_shift_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "scaleValue",
            {tbox(), LogicalType::INTEGER},
            tbox(),
            TboxFunctions::Tbox_scale_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "scaleValue",
            {tbox(), LogicalType::DOUBLE},
            tbox(),
            TboxFunctions::Tbox_scale_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "scaleTime",
            {tbox(), LogicalType::INTERVAL},
            tbox(),
            TboxFunctions::Tbox_scale_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "shiftScaleValue",
            {tbox(), LogicalType::INTEGER, LogicalType::INTEGER},
            tbox(),
            TboxFunctions::Tbox_shift_scale_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "shiftScaleValue",
            {tbox(), LogicalType::DOUBLE, LogicalType::DOUBLE},
            tbox(),
            TboxFunctions::Tbox_shift_scale_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "shiftScaleTime",
            {tbox(), LogicalType::INTERVAL, LogicalType::INTERVAL},
            tbox(),
            TboxFunctions::Tbox_shift_scale_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "expandValue",
            {tbox(), LogicalType::INTEGER},
            tbox(),
            TboxFunctions::Tbox_expand_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "expandValue",
            {tbox(), LogicalType::DOUBLE},
            tbox(),
            TboxFunctions::Tbox_expand_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "expandTime",
            {tbox(), LogicalType::INTERVAL},
            tbox(),
            TboxFunctions::Tbox_expand_time
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "round",
            {tbox()},
            tbox(),
            TboxFunctions::Tbox_round
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "round",
            {tbox(), LogicalType::INTEGER},
            tbox(),
            TboxFunctions::Tbox_round
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_contains",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Contains_tbox_tbox
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
            "tbox_contained",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Contained_tbox_tbox
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
            "tbox_overlaps",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overlaps_tbox_tbox
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
            "tbox_same",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Same_tbox_tbox
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
            "tbox_adjacent",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Adjacent_tbox_tbox
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
            "tbox_left",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Left_tbox_tbox
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
            "tbox_overleft",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overleft_tbox_tbox
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
            "tbox_right",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Right_tbox_tbox
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
            "tbox_overright",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overright_tbox_tbox
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
            "tbox_before",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Before_tbox_tbox
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

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_overbefore",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overbefore_tbox_tbox
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
            "tbox_after",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::After_tbox_tbox
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
            "tbox_overafter",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Overafter_tbox_tbox
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
            "tbox_union",
            {tbox(), tbox()},
            tbox(),
            TboxFunctions::Union_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_intersection",
            {tbox(), tbox()},
            tbox(),
            TboxFunctions::Intersection_tbox_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "+",
            {tbox(), tbox()},
            tbox(),
            TboxFunctions::Union_tbox_tbox
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
            "tbox_eq",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_eq
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_ne",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_ne
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_lt",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_lt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_le",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_le
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_ge",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_ge
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_gt",
            {tbox(), tbox()},
            LogicalType::BOOLEAN,
            TboxFunctions::Tbox_gt
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox_cmp",
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

    /* ***************************************************
     * WKB / hex-WKB serialization + hashing
     ****************************************************/
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "asBinary", {tbox()}, LogicalType::BLOB,
        TboxFunctions::Tbox_as_wkb));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "asHexWKB", {tbox()}, LogicalType::VARCHAR,
        TboxFunctions::Tbox_as_hexwkb));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "tboxFromBinary", {LogicalType::BLOB}, tbox(),
        TboxFunctions::Tbox_from_wkb));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "tboxFromHexWKB", {LogicalType::VARCHAR}, tbox(),
        TboxFunctions::Tbox_from_hexwkb));
    // tbox_hash / tbox_hash_extended are generated from the catalog (box unary
    // scalar accessors); the generated surface uses the faithful native-unsigned
    // UINTEGER / UBIGINT return, matching temporal/span/set hash generation.
}

} // namespace duckdb
