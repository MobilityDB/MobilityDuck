#include "meos_wrapper_simple.hpp"

#include "common.hpp"
#include "temporal/temporal.hpp"
#include "temporal/temporal_functions.hpp"
#include "temporal/spanset.hpp"
#include "temporal/tbox.hpp"
#include "temporal/set.hpp"
#include "temporal/span.hpp"
#include "geo/stbox.hpp"
#include "geo/tgeompoint.hpp"

#include "duckdb/common/types/blob.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/common/types/data_chunk.hpp"

#include "mobilityduck/bindings.hpp"

namespace duckdb {

#define DEFINE_TEMPORAL_TYPE(NAME) \
    LogicalType TemporalTypes::NAME() { \
        LogicalType type(LogicalTypeId::BLOB); \
        type.SetAlias(#NAME); \
        return type; \
    }

DEFINE_TEMPORAL_TYPE(TINT)
DEFINE_TEMPORAL_TYPE(TBOOL)
DEFINE_TEMPORAL_TYPE(TFLOAT)
DEFINE_TEMPORAL_TYPE(TTEXT)

#undef DEFINE_TEMPORAL_TYPE

void TemporalTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType( "TINT", TINT());
    loader.RegisterType( "TBOOL", TBOOL());
    loader.RegisterType( "TFLOAT", TFLOAT());
    loader.RegisterType( "TTEXT", TTEXT());
}

const std::vector<LogicalType> &TemporalTypes::AllTypes() {
    static std::vector<LogicalType> types = {
        TINT(),
        TBOOL(),
        TFLOAT(),
        TTEXT()
    };
    return types;
}

LogicalType TemporalTypes::GetBaseTypeFromAlias(const char *alias) {
    for (size_t i = 0; i < sizeof(BASE_TYPES) / sizeof(BASE_TYPES[0]); i++) {
        if (strcmp(alias, BASE_TYPES[i].alias) == 0) {
            return BASE_TYPES[i].basetype;
        }
    }
    throw InternalException("Invalid temporal type alias: %s", alias);
}

void TemporalTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    for (auto &type : TemporalTypes::AllTypes()) {
        loader.RegisterCastFunction(
            LogicalType::VARCHAR,
            type,
            TemporalFunctions::Temporal_in
        );

        loader.RegisterCastFunction(
            type,
            LogicalType::VARCHAR,
            TemporalFunctions::Temporal_out
        );

    //     ExtensionUtil::RegisterCastFunction(
    //         instance,
    //         type,
    //         type,
    //         TemporalFunctions::Temporal_enforce_typmod_cast,
    //         100
    //     );
    // }

    loader.RegisterCastFunction(
        LogicalType::BLOB,
        SpansetTypes::tstzspanset(),
        TemporalFunctions::Blob_to_tstzspanset
    );

    loader.RegisterCastFunction(
        TemporalTypes::TBOOL(),
        TemporalTypes::TINT(),
        TemporalFunctions::Tbool_to_tint_cast
    );

    loader.RegisterCastFunction(
        TemporalTypes::TINT(),
        TemporalTypes::TFLOAT(),
        TemporalFunctions::Tint_to_tfloat_cast
    );

    loader.RegisterCastFunction(
        TemporalTypes::TFLOAT(),
        TemporalTypes::TINT(),
        TemporalFunctions::Tfloat_to_tint_cast
    );

    loader.RegisterCastFunction(
        TemporalTypes::TINT(),
        TboxType::TBOX(),
        TemporalFunctions::Tnumber_to_tbox_cast
    );

    loader.RegisterCastFunction(
        TemporalTypes::TFLOAT(),
        TboxType::TBOX(),
        TemporalFunctions::Tnumber_to_tbox_cast
    );

}
}

void TemporalTypes::RegisterScalarFunctions(ExtensionLoader &loader) {
    for (auto &type : TemporalTypes::AllTypes()) {
        loader.RegisterFunction(
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), LogicalType::TIMESTAMP_TZ},
                type,
                TemporalFunctions::Tinstant_constructor
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {type, LogicalType::INTEGER},
                type,
                TemporalFunctions::Temporal_enforce_typmod
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SetTypes::tstzset()},
                type,
                TemporalFunctions::Tsequence_from_base_tstzset
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SpanTypes::TSTZSPAN()},
                type,
                TemporalFunctions::Tsequence_from_base_tstzspan
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SpanTypes::TSTZSPAN(), LogicalType::VARCHAR},
                type,
                TemporalFunctions::Tsequence_from_base_tstzspan
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SpansetTypes::tstzspanset()},
                type,
                TemporalFunctions::Tsequenceset_from_base_tstzspanset
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SpansetTypes::tstzspanset(), LogicalType::VARCHAR},
                type,
                TemporalFunctions::Tsequenceset_from_base_tstzspanset
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "tempSubtype",
                {type},
                LogicalType::VARCHAR,
                TemporalFunctions::Temporal_subtype
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "interp",
                {type},
                LogicalType::VARCHAR,
                TemporalFunctions::Temporal_interp
            )
        );

        // getValue / startValue / endValue / minValue / maxValue on
        // temporal types are now registered below via typed overloads
        // (mobilityduck::RegisterTemporalDatumAccessor) so that each
        // overload's result Vector type matches the base type of the
        // incoming alias. See src/include/mobilityduck/bindings.hpp for
        // the helper and the explanation of the 1.4 type-check bug it
        // fixes.

        if (type.GetAlias() != "TBOOL") {
            loader.RegisterFunction(
                ScalarFunction(
                    "minInstant",
                    {type},
                    type,
                    TemporalFunctions::Temporal_min_instant
                )
            );
    
            loader.RegisterFunction(
                ScalarFunction(
                    "maxInstant",
                    {type},
                    type,
                    TemporalFunctions::Temporal_max_instant
                )
            );

            loader.RegisterFunction(
                ScalarFunction(
                    "atMin",
                    {type},
                    type,
                    TemporalFunctions::Temporal_at_min
                )
            );

            loader.RegisterFunction(
                ScalarFunction(
                    "minusMin",
                    {type},
                    type,
                    TemporalFunctions::Temporal_minus_min
                )
            );

            loader.RegisterFunction(
                ScalarFunction(
                    "atMax",
                    {type},
                    type,
                    TemporalFunctions::Temporal_at_max
                )
            );

            loader.RegisterFunction(
                ScalarFunction(
                    "minusMax",
                    {type},
                    type,
                    TemporalFunctions::Temporal_minus_max
                )
            );
        }

        loader.RegisterFunction(
            ScalarFunction(
                "valueN",
                {type, LogicalType::BIGINT},
                TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()),
                TemporalFunctions::Temporal_value_n
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "getTimestamp",
                {type},
                LogicalType::TIMESTAMP_TZ,
                TemporalFunctions::Tinstant_timestamptz
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "getTime",
                {type},
                SpansetTypes::tstzspanset(),
                TemporalFunctions::Temporal_time
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "duration",
                {type},
                LogicalType::INTERVAL,
                TemporalFunctions::Temporal_duration
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "duration",
                {type, LogicalType::BOOLEAN},
                LogicalType::INTERVAL,
                TemporalFunctions::Temporal_duration
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "Seq",
                {LogicalType::LIST(type)},
                type,
                TemporalFunctions::Tsequence_constructor
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "Seq",
                {LogicalType::LIST(type), LogicalType::VARCHAR},
                type,
                TemporalFunctions::Tsequence_constructor
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "Seq",
                {LogicalType::LIST(type), LogicalType::VARCHAR, LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Tsequence_constructor
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "Inst",
                {type},
                type,
                TemporalFunctions::Temporal_to_tinstant
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "Seq",
                {LogicalType::LIST(type), LogicalType::VARCHAR, LogicalType::BOOLEAN, LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Tsequence_constructor
            )
        );
        
        loader.RegisterFunction(
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "Seq",
                {type, LogicalType::VARCHAR},
                type,
                TemporalFunctions::Temporal_to_tsequence
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "Seq",
                {type},
                type,
                TemporalFunctions::Temporal_to_tsequence
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "SeqSet",
                {LogicalType::LIST(type)},
                type,
                TemporalFunctions::Tsequenceset_constructor
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "SeqSet",
                {type},
                type,
                TemporalFunctions::Temporal_to_tsequenceset
            )
        );

        if (type.GetAlias() == "TFLOAT") {
            loader.RegisterFunction(
                ScalarFunction(
                    StringUtil::Lower(type.GetAlias()) + "SeqSet",
                    {type, LogicalType::VARCHAR},
                    type,
                    TemporalFunctions::Temporal_to_tsequenceset
                )
            );
        }

        loader.RegisterFunction(
            ScalarFunction(
                "setInterp",
                {type, LogicalType::VARCHAR},
                type,
                TemporalFunctions::Temporal_set_interp
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "appendInstant",
                {type, type},
                type,
                TemporalFunctions::Temporal_append_tinstant
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "appendInstant",
                {type, type, LogicalType::VARCHAR},
                type,
                TemporalFunctions::Temporal_append_tinstant
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "appendSequence",
                {type, type},
                type,
                TemporalFunctions::Temporal_append_tsequence
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "merge",
                {type, type},
                type,
                TemporalFunctions::Temporal_merge
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "merge",
                {LogicalType::LIST(type)},
                type,
                TemporalFunctions::Temporal_merge_array
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "timeSpan",
                {type},
                SpanTypes::TSTZSPAN(),
                TemporalFunctions::Temporal_to_tstzspan
            )
        );

        if (type.GetAlias() == "TINT") {
            loader.RegisterFunction(
                ScalarFunction(
                    "valueSpan",
                    {type},
                    SpanTypes::INTSPAN(),
                    TemporalFunctions::Tnumber_to_span
                )
            );

            loader.RegisterFunction(
                ScalarFunction(
                    "valueSet",
                    {type},
                    SetTypes::intset(),
                    TemporalFunctions::Temporal_valueset
                )
            );
        } else if (type.GetAlias() == "TFLOAT") {
            loader.RegisterFunction(
                ScalarFunction(
                    "valueSpan",
                    {type},
                    SpanTypes::FLOATSPAN(),
                    TemporalFunctions::Tnumber_to_span
                )
            );

            loader.RegisterFunction(
                ScalarFunction(
                    "valueSet",
                    {type},
                    SetTypes::floatset(),
                    TemporalFunctions::Temporal_valueset
                )
            );
        }

        loader.RegisterFunction(
            ScalarFunction(
                "sequences",
                {type},
                LogicalType::LIST(type),
                TemporalFunctions::Temporal_sequences
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "segments",
                {type},
                LogicalType::LIST(type),
                TemporalFunctions::Temporal_segments
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "startTimestamp",
                {type},
                LogicalType::TIMESTAMP_TZ,
                TemporalFunctions::Temporal_start_timestamptz
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "endTimestamp",
                {type},
                LogicalType::TIMESTAMP_TZ,
                TemporalFunctions::Temporal_end_timestamptz
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "timestamps",
                {type},
                LogicalType::LIST(LogicalType::TIMESTAMP_TZ),
                TemporalFunctions::Temporal_timestamps
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "instants",
                {type},
                LogicalType::LIST(type),
                TemporalFunctions::Temporal_instants
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "atTime",
                {type, LogicalType::TIMESTAMP_TZ},
                type,
                TemporalFunctions::Temporal_at_timestamptz
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "atTime",
                {type, SpanTypes::TSTZSPAN()},
                type,
                TemporalFunctions::Temporal_at_tstzspan
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "atTime",
                {type, SpansetTypes::tstzspanset()},
                type,
                TemporalFunctions::Temporal_at_tstzspanset
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "minusTime",
                {type, LogicalType::TIMESTAMP_TZ},
                type,
                TemporalFunctions::Temporal_minus_timestamptz
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "atTime",
                {type, SetTypes::tstzset()},
                type,
                TemporalFunctions::Temporal_at_tstzset
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "minusTime",
                {type, SetTypes::tstzset()},
                type,
                TemporalFunctions::Temporal_minus_tstzset
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "minusTime",
                {type, SpanTypes::TSTZSPAN()},
                type,
                TemporalFunctions::Temporal_minus_tstzspan
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "minusTime",
                {type, SpansetTypes::tstzspanset()},
                type,
                TemporalFunctions::Temporal_minus_tstzspanset
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "valueAtTimestamp",
                {type, LogicalType::TIMESTAMP_TZ},
                TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()),
                TemporalFunctions::Temporal_value_at_timestamptz
            )
        );

        if (type.GetAlias() == "TINT" || type.GetAlias() == "TFLOAT") {
            loader.RegisterFunction(
                ScalarFunction(
                    "shiftValue",
                    {type, LogicalType::BIGINT},
                    type,
                    TemporalFunctions::Tnumber_shift_value
                )
            );

            loader.RegisterFunction(
                ScalarFunction(
                    "scaleValue",
                    {type, LogicalType::BIGINT},
                    type,
                    TemporalFunctions::Tnumber_scale_value
                )
            );

            loader.RegisterFunction(
                ScalarFunction(
                    "shiftScaleValue",
                    {type, LogicalType::BIGINT, LogicalType::BIGINT},
                    type,
                    TemporalFunctions::Tnumber_shift_scale_value
                )
            );

            loader.RegisterFunction(
                ScalarFunction(
                    "integral",
                    {type},
                    LogicalType::DOUBLE,
                    TemporalFunctions::Tnumber_integral
                )
            );

            loader.RegisterFunction(
                ScalarFunction(
                    "twAvg",
                    {type},
                    LogicalType::DOUBLE,
                    TemporalFunctions::Tnumber_twavg
                )
            );
        }
        if (type.GetAlias() != "TBOOL") {
            loader.RegisterFunction(
                ScalarFunction(
                    "tempDump",
                    {type},
                    LogicalType::LIST(
                        LogicalType::STRUCT(
                            {{"value", TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str())},
                            {"time", SpansetTypes::tstzspanset()}}
                        )
                    ),
                    TemporalFunctions::Temporal_dump
                )
            );
        }
        
        loader.RegisterFunction(
            ScalarFunction(
                "atValues",
                {type, TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str())},
                type,
                TemporalFunctions::Temporal_at_value
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "minusValues",
                {type, TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str())},
                type,
                TemporalFunctions::Temporal_minus_value
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "beforeTimestamp",
                {type, LogicalType::TIMESTAMP_TZ},
                type,
                TemporalFunctions::Temporal_before_timestamptz
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "beforeTimestamp",
                {type, LogicalType::TIMESTAMP_TZ, LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Temporal_before_timestamptz
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "afterTimestamp",
                {type, LogicalType::TIMESTAMP_TZ},
                type,
                TemporalFunctions::Temporal_after_timestamptz
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "afterTimestamp",
                {type, LogicalType::TIMESTAMP_TZ, LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Temporal_after_timestamptz
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "insert",
                {type, type},
                type,
                TemporalFunctions::Temporal_insert
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "insert",
                {type, type, LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Temporal_insert
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "update",
                {type, type},
                type,
                TemporalFunctions::Temporal_update
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "update",
                {type, type, LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Temporal_update
            )
        );
        
        loader.RegisterFunction(
            ScalarFunction(
                "deleteTime",
                {type, LogicalType::TIMESTAMP_TZ},
                type,
                TemporalFunctions::Temporal_delete_timestamptz
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "deleteTime",
                {type, LogicalType::TIMESTAMP_TZ, LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Temporal_delete_timestamptz
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "deleteTime",
                {type, SetTypes::tstzset()},
                type,
                TemporalFunctions::Temporal_delete_tstzset
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "deleteTime",
                {type, SetTypes::tstzset(), LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Temporal_delete_tstzset
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "deleteTime",
                {type, SpanTypes::TSTZSPAN()},
                type,
                TemporalFunctions::Temporal_delete_tstzspan
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "deleteTime",
                {type, SpanTypes::TSTZSPAN(), LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Temporal_delete_tstzspan
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "deleteTime",
                {type, SpansetTypes::tstzspanset()},
                type,
                TemporalFunctions::Temporal_delete_tstzspanset
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "deleteTime",
                {type, SpansetTypes::tstzspanset(), LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Temporal_delete_tstzspanset
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "segmentMinDuration",
                {type, LogicalType::INTERVAL},
                type,
                TemporalFunctions::Temporal_segm_min_duration
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "segmentMinDuration",
                {type, LogicalType::INTERVAL, LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Temporal_segm_min_duration
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "segmentMaxDuration",
                {type, LogicalType::INTERVAL},
                type,
                TemporalFunctions::Temporal_segm_max_duration
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "segmentMaxDuration",
                {type, LogicalType::INTERVAL, LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Temporal_segm_max_duration
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "temporal_eq",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_eq
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "=",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_eq
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "temporal_ne",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_ne
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "<>",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_ne
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "temporal_le",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_le
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "<=",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_le
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "temporal_lt",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_lt
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "<",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_lt
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "temporal_ge",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_ge
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                ">=",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_ge
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                "temporal_gt",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_gt
            )
        );
        loader.RegisterFunction(
            ScalarFunction(
                ">",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_gt
            )
        );

        loader.RegisterFunction(
            ScalarFunction(
                "temporal_cmp",
                {type, type},
                LogicalType::INTEGER,
                TemporalFunctions::Temporal_cmp
            )
        );
    }

    // Typed `getValue` / `startValue` / `endValue` / `minValue` / `maxValue`
    // overloads for TINT, TBOOL, TFLOAT. Each registration pairs the input
    // temporal-type alias with the C++ scalar result type so the generated
    // DuckDB result Vector type matches what the MEOS accessor actually
    // writes, which DuckDB 1.4's UnaryExecutor asserts strictly. See the
    // comment in src/include/mobilityduck/bindings.hpp for the full rationale.
    auto tinstant_value_temporal = [](const Temporal *t) -> uintptr_t {
        return tinstant_value(reinterpret_cast<const TInstant *>(t));
    };

    // getValue(tint / tbool / tfloat) — instant-level accessor
    mobilityduck::RegisterTemporalDatumAccessor<int64_t>(
        loader, "getValue", TemporalTypes::TINT(),   LogicalType::BIGINT,  tinstant_value_temporal);
    mobilityduck::RegisterTemporalDatumAccessor<bool>(
        loader, "getValue", TemporalTypes::TBOOL(),  LogicalType::BOOLEAN, tinstant_value_temporal);
    mobilityduck::RegisterTemporalDatumAccessor<double>(
        loader, "getValue", TemporalTypes::TFLOAT(), LogicalType::DOUBLE,  tinstant_value_temporal);

    // startValue / endValue on TINT / TBOOL / TFLOAT
    mobilityduck::RegisterTemporalDatumAccessor<int64_t>(
        loader, "startValue", TemporalTypes::TINT(),   LogicalType::BIGINT,  temporal_start_value);
    mobilityduck::RegisterTemporalDatumAccessor<bool>(
        loader, "startValue", TemporalTypes::TBOOL(),  LogicalType::BOOLEAN, temporal_start_value);
    mobilityduck::RegisterTemporalDatumAccessor<double>(
        loader, "startValue", TemporalTypes::TFLOAT(), LogicalType::DOUBLE,  temporal_start_value);

    mobilityduck::RegisterTemporalDatumAccessor<int64_t>(
        loader, "endValue", TemporalTypes::TINT(),   LogicalType::BIGINT,  temporal_end_value);
    mobilityduck::RegisterTemporalDatumAccessor<bool>(
        loader, "endValue", TemporalTypes::TBOOL(),  LogicalType::BOOLEAN, temporal_end_value);
    mobilityduck::RegisterTemporalDatumAccessor<double>(
        loader, "endValue", TemporalTypes::TFLOAT(), LogicalType::DOUBLE,  temporal_end_value);

    // minValue / maxValue on TINT / TFLOAT (TBOOL omitted — min/max on a
    // boolean is meaningless and the existing API does not expose it)
    mobilityduck::RegisterTemporalDatumAccessor<int64_t>(
        loader, "minValue", TemporalTypes::TINT(),   LogicalType::BIGINT, temporal_min_value);
    mobilityduck::RegisterTemporalDatumAccessor<double>(
        loader, "minValue", TemporalTypes::TFLOAT(), LogicalType::DOUBLE, temporal_min_value);

    mobilityduck::RegisterTemporalDatumAccessor<int64_t>(
        loader, "maxValue", TemporalTypes::TINT(),   LogicalType::BIGINT, temporal_max_value);
    mobilityduck::RegisterTemporalDatumAccessor<double>(
        loader, "maxValue", TemporalTypes::TFLOAT(), LogicalType::DOUBLE, temporal_max_value);

    loader.RegisterFunction(
        ScalarFunction(
            "atValues",
            {TemporalTypes::TINT(), SetTypes::intset()},
            TemporalTypes::TINT(),
            TemporalFunctions::Temporal_at_values
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "atValues",
            {TemporalTypes::TFLOAT(), SetTypes::floatset()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Temporal_at_values
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "atValues",
            {TemporalTypes::TTEXT(), SetTypes::textset()},
            TemporalTypes::TTEXT(),
            TemporalFunctions::Temporal_at_values
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "minusValues",
            {TemporalTypes::TINT(), SetTypes::intset()},
            TemporalTypes::TINT(),
            TemporalFunctions::Temporal_minus_value
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "minusValues",
            {TemporalTypes::TFLOAT(), SetTypes::floatset()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Temporal_minus_value
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "minusValues",
            {TemporalTypes::TTEXT(), SetTypes::textset()},
            TemporalTypes::TTEXT(),
            TemporalFunctions::Temporal_minus_value
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "whenTrue",
            {TemporalTypes::TBOOL()},
            SpansetTypes::tstzspanset(),
            TemporalFunctions::Tbool_when_true
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "atValues",
            {TemporalTypes::TINT(), SpanTypes::INTSPAN()},
            TemporalTypes::TINT(),
            TemporalFunctions::Tnumber_at_span
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "atValues",
            {TemporalTypes::TFLOAT(), SpanTypes::FLOATSPAN()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Tnumber_at_span
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "minusValues",
            {TemporalTypes::TINT(), SpanTypes::INTSPAN()},
            TemporalTypes::TINT(),
            TemporalFunctions::Tnumber_minus_span
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "minusValues",
            {TemporalTypes::TFLOAT(), SpanTypes::FLOATSPAN()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Tnumber_minus_span
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "atValues",
            {TemporalTypes::TINT(), SpansetTypes::intspanset()},
            TemporalTypes::TINT(),
            TemporalFunctions::Tnumber_at_spanset
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "atValues",
            {TemporalTypes::TFLOAT(), SpansetTypes::floatspanset()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Tnumber_at_spanset
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "minusValues",
            {TemporalTypes::TINT(), SpansetTypes::intspanset()},
            TemporalTypes::TINT(),
            TemporalFunctions::Tnumber_minus_spanset
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "minusValues",
            {TemporalTypes::TFLOAT(), SpansetTypes::floatspanset()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Tnumber_minus_spanset
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "atTbox",
            {TemporalTypes::TINT(), TboxType::TBOX()},
            TemporalTypes::TINT(),
            TemporalFunctions::Tnumber_at_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "atTbox",
            {TemporalTypes::TFLOAT(), TboxType::TBOX()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Tnumber_at_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "minusTbox",
            {TemporalTypes::TINT(), TboxType::TBOX()},
            TemporalTypes::TINT(),
            TemporalFunctions::Tnumber_minus_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "minusTbox",
            {TemporalTypes::TFLOAT(), TboxType::TBOX()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Tnumber_minus_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "round",
            {TemporalTypes::TFLOAT()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Temporal_round
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "round",
            {TemporalTypes::TFLOAT(), LogicalType::INTEGER},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Temporal_round
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tint",
            {TemporalTypes::TBOOL()},
            TemporalTypes::TINT(),
            TemporalFunctions::Tbool_to_tint
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tfloat",
            {TemporalTypes::TINT()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Tint_to_tfloat
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tint",
            {TemporalTypes::TFLOAT()},
            TemporalTypes::TINT(),
            TemporalFunctions::Tfloat_to_tint
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {TemporalTypes::TINT()},
            TboxType::TBOX(),
            TemporalFunctions::Tnumber_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "tbox",
            {TemporalTypes::TFLOAT()},
            TboxType::TBOX(),
            TemporalFunctions::Tnumber_to_tbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "getValues",
            {TemporalTypes::TINT()},
            SpansetTypes::intspanset(),
            TemporalFunctions::Tnumber_valuespans
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "getValues",
            {TemporalTypes::TFLOAT()},
            SpansetTypes::floatspanset(),
            TemporalFunctions::Tnumber_valuespans
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "avgValue",
            {TemporalTypes::TINT()},
            LogicalType::DOUBLE,
            TemporalFunctions::Tnumber_avg_value
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "avgValue",
            {TemporalTypes::TFLOAT()},
            LogicalType::DOUBLE,
            TemporalFunctions::Tnumber_avg_value
        )
    );

    // tbool boolean operators
    loader.RegisterFunction(ScalarFunction("&", {TemporalTypes::TBOOL(), LogicalType::BOOLEAN}, TemporalTypes::TBOOL(), TemporalFunctions::Tand_tbool_bool));
    loader.RegisterFunction(ScalarFunction("&", {LogicalType::BOOLEAN, TemporalTypes::TBOOL()}, TemporalTypes::TBOOL(), TemporalFunctions::Tand_bool_tbool));
    loader.RegisterFunction(ScalarFunction("&", {TemporalTypes::TBOOL(), TemporalTypes::TBOOL()}, TemporalTypes::TBOOL(), TemporalFunctions::Tand_tbool_tbool));
    loader.RegisterFunction(ScalarFunction("|", {TemporalTypes::TBOOL(), LogicalType::BOOLEAN}, TemporalTypes::TBOOL(), TemporalFunctions::Tor_tbool_bool));
    loader.RegisterFunction(ScalarFunction("|", {LogicalType::BOOLEAN, TemporalTypes::TBOOL()}, TemporalTypes::TBOOL(), TemporalFunctions::Tor_bool_tbool));
    loader.RegisterFunction(ScalarFunction("|", {TemporalTypes::TBOOL(), TemporalTypes::TBOOL()}, TemporalTypes::TBOOL(), TemporalFunctions::Tor_tbool_tbool));
    loader.RegisterFunction(ScalarFunction("~", {TemporalTypes::TBOOL()}, TemporalTypes::TBOOL(), TemporalFunctions::Tnot_tbool));

    // tnumber arithmetic operators
    loader.RegisterFunction(ScalarFunction("+", {LogicalType::INTEGER, TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Add_int_tint));
    loader.RegisterFunction(ScalarFunction("+", {TemporalTypes::TINT(), LogicalType::INTEGER}, TemporalTypes::TINT(), TemporalFunctions::Add_tint_int));
    loader.RegisterFunction(ScalarFunction("+", {LogicalType::DOUBLE, TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Add_float_tfloat));
    loader.RegisterFunction(ScalarFunction("+", {TemporalTypes::TFLOAT(), LogicalType::DOUBLE}, TemporalTypes::TFLOAT(), TemporalFunctions::Add_tfloat_float));
    loader.RegisterFunction(ScalarFunction("+", {TemporalTypes::TINT(), TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Add_tnumber_tnumber));
    loader.RegisterFunction(ScalarFunction("+", {TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Add_tnumber_tnumber));

    loader.RegisterFunction(ScalarFunction("-", {LogicalType::INTEGER, TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Sub_int_tint));
    loader.RegisterFunction(ScalarFunction("-", {TemporalTypes::TINT(), LogicalType::INTEGER}, TemporalTypes::TINT(), TemporalFunctions::Sub_tint_int));
    loader.RegisterFunction(ScalarFunction("-", {LogicalType::DOUBLE, TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Sub_float_tfloat));
    loader.RegisterFunction(ScalarFunction("-", {TemporalTypes::TFLOAT(), LogicalType::DOUBLE}, TemporalTypes::TFLOAT(), TemporalFunctions::Sub_tfloat_float));
    loader.RegisterFunction(ScalarFunction("-", {TemporalTypes::TINT(), TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Sub_tnumber_tnumber));
    loader.RegisterFunction(ScalarFunction("-", {TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Sub_tnumber_tnumber));

    loader.RegisterFunction(ScalarFunction("*", {LogicalType::INTEGER, TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Mult_int_tint));
    loader.RegisterFunction(ScalarFunction("*", {TemporalTypes::TINT(), LogicalType::INTEGER}, TemporalTypes::TINT(), TemporalFunctions::Mult_tint_int));
    loader.RegisterFunction(ScalarFunction("*", {LogicalType::DOUBLE, TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Mult_float_tfloat));
    loader.RegisterFunction(ScalarFunction("*", {TemporalTypes::TFLOAT(), LogicalType::DOUBLE}, TemporalTypes::TFLOAT(), TemporalFunctions::Mult_tfloat_float));
    loader.RegisterFunction(ScalarFunction("*", {TemporalTypes::TINT(), TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Mult_tnumber_tnumber));
    loader.RegisterFunction(ScalarFunction("*", {TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Mult_tnumber_tnumber));

    loader.RegisterFunction(ScalarFunction("/", {LogicalType::INTEGER, TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Div_int_tint));
    loader.RegisterFunction(ScalarFunction("/", {TemporalTypes::TINT(), LogicalType::INTEGER}, TemporalTypes::TINT(), TemporalFunctions::Div_tint_int));
    loader.RegisterFunction(ScalarFunction("/", {LogicalType::DOUBLE, TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Div_float_tfloat));
    loader.RegisterFunction(ScalarFunction("/", {TemporalTypes::TFLOAT(), LogicalType::DOUBLE}, TemporalTypes::TFLOAT(), TemporalFunctions::Div_tfloat_float));
    loader.RegisterFunction(ScalarFunction("/", {TemporalTypes::TINT(), TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Div_tnumber_tnumber));
    loader.RegisterFunction(ScalarFunction("/", {TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Div_tnumber_tnumber));

    // Unary tnumber functions
    loader.RegisterFunction(ScalarFunction("abs", {TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Tnumber_abs));
    loader.RegisterFunction(ScalarFunction("abs", {TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Tnumber_abs));
    loader.RegisterFunction(ScalarFunction("derivative", {TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Temporal_derivative));
    loader.RegisterFunction(ScalarFunction("degrees", {TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Tfloat_degrees));
    loader.RegisterFunction(ScalarFunction("degrees", {TemporalTypes::TFLOAT(), LogicalType::BOOLEAN}, TemporalTypes::TFLOAT(), TemporalFunctions::Tfloat_degrees));
    loader.RegisterFunction(ScalarFunction("radians", {TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Tfloat_radians));

    // tnumber distance and nearest-approach-distance.
    //
    // Value-distance variants `<-> ` for (tint, INTEGER), (INTEGER, tint),
    // (tfloat, DOUBLE), (DOUBLE, tfloat) are intentionally NOT registered
    // here: in the installed MEOS library, tdistance_tfloat_float / tint_int
    // return the temporal's own value at each instant rather than the
    // |t.value - v| absolute difference. Verified by smoke test:
    //   SELECT 5.0::DOUBLE <-> tfloat '5.0@2000-01-01';   -- returns 5.0, expected 0.0
    //   SELECT 100.0::DOUBLE <-> tfloat '2.5@2000-01-01'; -- returns 2.5, expected 97.5
    // The temporal-temporal variant DOES work correctly, and so does nad_*.
    // Restore the value-distance registrations once the MEOS issue is resolved.
    loader.RegisterFunction(ScalarFunction("<->", {TemporalTypes::TINT(), TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Tdistance_tnumber_tnumber));
    loader.RegisterFunction(ScalarFunction("<->", {TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Tdistance_tnumber_tnumber));
    // Named form of the same function for SQL portability.
    loader.RegisterFunction(ScalarFunction("tdistance", {TemporalTypes::TINT(), TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Tdistance_tnumber_tnumber));
    loader.RegisterFunction(ScalarFunction("tdistance", {TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Tdistance_tnumber_tnumber));

    // nearestApproachDistance / nad — scalar return
    loader.RegisterFunction(ScalarFunction("nad", {TemporalTypes::TINT(), LogicalType::INTEGER}, LogicalType::INTEGER, TemporalFunctions::Nad_tint_int));
    loader.RegisterFunction(ScalarFunction("nad", {TemporalTypes::TINT(), TemporalTypes::TINT()}, LogicalType::INTEGER, TemporalFunctions::Nad_tint_tint));
    loader.RegisterFunction(ScalarFunction("nad", {TemporalTypes::TFLOAT(), LogicalType::DOUBLE}, LogicalType::DOUBLE, TemporalFunctions::Nad_tfloat_float));
    loader.RegisterFunction(ScalarFunction("nad", {TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()}, LogicalType::DOUBLE, TemporalFunctions::Nad_tfloat_tfloat));
    loader.RegisterFunction(ScalarFunction("nearestApproachDistance", {TemporalTypes::TINT(), LogicalType::INTEGER}, LogicalType::INTEGER, TemporalFunctions::Nad_tint_int));
    loader.RegisterFunction(ScalarFunction("nearestApproachDistance", {TemporalTypes::TINT(), TemporalTypes::TINT()}, LogicalType::INTEGER, TemporalFunctions::Nad_tint_tint));
    loader.RegisterFunction(ScalarFunction("nearestApproachDistance", {TemporalTypes::TFLOAT(), LogicalType::DOUBLE}, LogicalType::DOUBLE, TemporalFunctions::Nad_tfloat_float));
    loader.RegisterFunction(ScalarFunction("nearestApproachDistance", {TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()}, LogicalType::DOUBLE, TemporalFunctions::Nad_tfloat_tfloat));

    // Temporal topological predicates: temporal × temporal (4 ops × 4 type pairs)
    for (auto &t1 : TemporalTypes::AllTypes()) {
        for (auto &t2 : TemporalTypes::AllTypes()) {
            loader.RegisterFunction(ScalarFunction("@>", {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Contains_temporal_temporal));
            loader.RegisterFunction(ScalarFunction("<@", {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Contained_temporal_temporal));
            loader.RegisterFunction(ScalarFunction("&&", {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_temporal_temporal));
            loader.RegisterFunction(ScalarFunction("-|-", {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_temporal_temporal));
        }
    }
    // Temporal × tstzspan (and the reverse direction)
    for (auto &t : TemporalTypes::AllTypes()) {
        loader.RegisterFunction(ScalarFunction("@>", {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Contains_temporal_tstzspan));
        loader.RegisterFunction(ScalarFunction("<@", {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Contained_temporal_tstzspan));
        loader.RegisterFunction(ScalarFunction("&&", {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_temporal_tstzspan));
        loader.RegisterFunction(ScalarFunction("-|-", {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_temporal_tstzspan));
        loader.RegisterFunction(ScalarFunction("@>", {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Contains_tstzspan_temporal));
        loader.RegisterFunction(ScalarFunction("<@", {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Contained_tstzspan_temporal));
        loader.RegisterFunction(ScalarFunction("&&", {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_tstzspan_temporal));
        loader.RegisterFunction(ScalarFunction("-|-", {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_tstzspan_temporal));
    }

    // Temporal time-position predicates registered as named functions:
    // DuckDB's parser does not accept `#` as an operator-name character,
    // so the upstream MobilityDB operators `<<#`, `#>>`, `&<#`, `#&>`
    // are unreachable from SQL. The named-function forms `before`,
    // `after`, `overbefore`, `overafter` provide equivalent behaviour.
    for (auto &t1 : TemporalTypes::AllTypes()) {
        for (auto &t2 : TemporalTypes::AllTypes()) {
            loader.RegisterFunction(ScalarFunction("before",     {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Before_temporal_temporal));
            loader.RegisterFunction(ScalarFunction("after",      {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::After_temporal_temporal));
            loader.RegisterFunction(ScalarFunction("overbefore", {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Overbefore_temporal_temporal));
            loader.RegisterFunction(ScalarFunction("overafter",  {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Overafter_temporal_temporal));
        }
    }
    for (auto &t : TemporalTypes::AllTypes()) {
        loader.RegisterFunction(ScalarFunction("before",     {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Before_temporal_tstzspan));
        loader.RegisterFunction(ScalarFunction("after",      {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::After_temporal_tstzspan));
        loader.RegisterFunction(ScalarFunction("overbefore", {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Overbefore_temporal_tstzspan));
        loader.RegisterFunction(ScalarFunction("overafter",  {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Overafter_temporal_tstzspan));
        loader.RegisterFunction(ScalarFunction("before",     {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Before_tstzspan_temporal));
        loader.RegisterFunction(ScalarFunction("after",      {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::After_tstzspan_temporal));
        loader.RegisterFunction(ScalarFunction("overbefore", {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Overbefore_tstzspan_temporal));
        loader.RegisterFunction(ScalarFunction("overafter",  {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Overafter_tstzspan_temporal));
    }

    // Same time-position predicates extended to tgeompoint (× tstzspan and
    // × tgeompoint). MEOS dispatches by Temporal* so the same C wrappers work.
    {
        auto tg = TgeompointType::TGEOMPOINT();
        auto tspan = SpanTypes::TSTZSPAN();
        loader.RegisterFunction(ScalarFunction("before",     {tg, tspan}, LogicalType::BOOLEAN, TemporalFunctions::Before_temporal_tstzspan));
        loader.RegisterFunction(ScalarFunction("after",      {tg, tspan}, LogicalType::BOOLEAN, TemporalFunctions::After_temporal_tstzspan));
        loader.RegisterFunction(ScalarFunction("overbefore", {tg, tspan}, LogicalType::BOOLEAN, TemporalFunctions::Overbefore_temporal_tstzspan));
        loader.RegisterFunction(ScalarFunction("overafter",  {tg, tspan}, LogicalType::BOOLEAN, TemporalFunctions::Overafter_temporal_tstzspan));
        loader.RegisterFunction(ScalarFunction("before",     {tspan, tg}, LogicalType::BOOLEAN, TemporalFunctions::Before_tstzspan_temporal));
        loader.RegisterFunction(ScalarFunction("after",      {tspan, tg}, LogicalType::BOOLEAN, TemporalFunctions::After_tstzspan_temporal));
        loader.RegisterFunction(ScalarFunction("overbefore", {tspan, tg}, LogicalType::BOOLEAN, TemporalFunctions::Overbefore_tstzspan_temporal));
        loader.RegisterFunction(ScalarFunction("overafter",  {tspan, tg}, LogicalType::BOOLEAN, TemporalFunctions::Overafter_tstzspan_temporal));
        loader.RegisterFunction(ScalarFunction("before",     {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Before_temporal_temporal));
        loader.RegisterFunction(ScalarFunction("after",      {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::After_temporal_temporal));
        loader.RegisterFunction(ScalarFunction("overbefore", {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Overbefore_temporal_temporal));
        loader.RegisterFunction(ScalarFunction("overafter",  {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Overafter_temporal_temporal));
    }

    // Ever / always equality and inequality (named functions; DuckDB
    // parser does not accept ?= / #= operator names).
#define REG_EA(NAME, FN)                                                                                                                                                          \
    loader.RegisterFunction(ScalarFunction(NAME, {LogicalType::BOOLEAN,        TemporalTypes::TBOOL()},   LogicalType::BOOLEAN, TemporalFunctions::FN##_bool_tbool));             \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TBOOL(),      LogicalType::BOOLEAN},     LogicalType::BOOLEAN, TemporalFunctions::FN##_tbool_bool));             \
    loader.RegisterFunction(ScalarFunction(NAME, {LogicalType::INTEGER,        TemporalTypes::TINT()},    LogicalType::BOOLEAN, TemporalFunctions::FN##_int_tint));               \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TINT(),       LogicalType::INTEGER},     LogicalType::BOOLEAN, TemporalFunctions::FN##_tint_int));               \
    loader.RegisterFunction(ScalarFunction(NAME, {LogicalType::DOUBLE,         TemporalTypes::TFLOAT()},  LogicalType::BOOLEAN, TemporalFunctions::FN##_float_tfloat));           \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TFLOAT(),     LogicalType::DOUBLE},      LogicalType::BOOLEAN, TemporalFunctions::FN##_tfloat_float));           \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TINT(),       TemporalTypes::TINT()},    LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal));      \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TFLOAT(),     TemporalTypes::TFLOAT()},  LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal));      \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TBOOL(),      TemporalTypes::TBOOL()},   LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal));

    REG_EA("ever_eq",   Ever_eq)
    REG_EA("always_eq", Always_eq)
    REG_EA("ever_ne",   Ever_ne)
    REG_EA("always_ne", Always_ne)
#undef REG_EA

    // Ordering ever/always — no tbool variant (booleans have no ordering)
#define REG_EA_ORD(NAME, FN)                                                                                                                                          \
    loader.RegisterFunction(ScalarFunction(NAME, {LogicalType::INTEGER,    TemporalTypes::TINT()},   LogicalType::BOOLEAN, TemporalFunctions::FN##_int_tint));        \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TINT(),   LogicalType::INTEGER},    LogicalType::BOOLEAN, TemporalFunctions::FN##_tint_int));        \
    loader.RegisterFunction(ScalarFunction(NAME, {LogicalType::DOUBLE,     TemporalTypes::TFLOAT()}, LogicalType::BOOLEAN, TemporalFunctions::FN##_float_tfloat));    \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TFLOAT(), LogicalType::DOUBLE},     LogicalType::BOOLEAN, TemporalFunctions::FN##_tfloat_float));    \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TINT(),   TemporalTypes::TINT()},   LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal)); \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()}, LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal));

    REG_EA_ORD("ever_lt",   Ever_lt)
    REG_EA_ORD("always_lt", Always_lt)
    REG_EA_ORD("ever_le",   Ever_le)
    REG_EA_ORD("always_le", Always_le)
    REG_EA_ORD("ever_gt",   Ever_gt)
    REG_EA_ORD("always_gt", Always_gt)
    REG_EA_ORD("ever_ge",   Ever_ge)
    REG_EA_ORD("always_ge", Always_ge)
#undef REG_EA_ORD

    // Similarity measures (tnumber × tnumber, tgeompoint × tgeompoint)
    {
        auto tg = TgeompointType::TGEOMPOINT();
        std::vector<LogicalType> sim_types = {
            TemporalTypes::TINT(), TemporalTypes::TFLOAT(), tg
        };
        for (auto &t : sim_types) {
            loader.RegisterFunction(ScalarFunction("frechetDistance", {t, t}, LogicalType::DOUBLE, TemporalFunctions::Temporal_frechet_distance));
            loader.RegisterFunction(ScalarFunction("discreteFrechet", {t, t}, LogicalType::DOUBLE, TemporalFunctions::Temporal_frechet_distance));
            loader.RegisterFunction(ScalarFunction("dynTimeWarp",     {t, t}, LogicalType::DOUBLE, TemporalFunctions::Temporal_dyntimewarp_distance));
            loader.RegisterFunction(ScalarFunction("hausdorffDistance", {t, t}, LogicalType::DOUBLE, TemporalFunctions::Temporal_hausdorff_distance));
        }
    }

    // tnumber × {numspan, tbox} topological predicates (4 ops × 8 shape pairs)
    {
        auto tint  = TemporalTypes::TINT();
        auto tflt  = TemporalTypes::TFLOAT();
        auto ispan = SpanTypes::INTSPAN();
        auto fspan = SpanTypes::FLOATSPAN();
        auto tbox  = TboxType::TBOX();

        // tnumber × numspan
        loader.RegisterFunction(ScalarFunction("@>",  {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Contains_tnumber_numspan));
        loader.RegisterFunction(ScalarFunction("<@",  {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Contained_tnumber_numspan));
        loader.RegisterFunction(ScalarFunction("&&",  {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_tnumber_numspan));
        loader.RegisterFunction(ScalarFunction("-|-", {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_tnumber_numspan));
        loader.RegisterFunction(ScalarFunction("@>",  {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Contains_tnumber_numspan));
        loader.RegisterFunction(ScalarFunction("<@",  {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Contained_tnumber_numspan));
        loader.RegisterFunction(ScalarFunction("&&",  {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_tnumber_numspan));
        loader.RegisterFunction(ScalarFunction("-|-", {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_tnumber_numspan));
        // numspan × tnumber
        loader.RegisterFunction(ScalarFunction("@>",  {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Contains_numspan_tnumber));
        loader.RegisterFunction(ScalarFunction("<@",  {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Contained_numspan_tnumber));
        loader.RegisterFunction(ScalarFunction("&&",  {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_numspan_tnumber));
        loader.RegisterFunction(ScalarFunction("-|-", {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_numspan_tnumber));
        loader.RegisterFunction(ScalarFunction("@>",  {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Contains_numspan_tnumber));
        loader.RegisterFunction(ScalarFunction("<@",  {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Contained_numspan_tnumber));
        loader.RegisterFunction(ScalarFunction("&&",  {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_numspan_tnumber));
        loader.RegisterFunction(ScalarFunction("-|-", {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_numspan_tnumber));
        // tnumber × tbox
        for (auto &t : {tint, tflt}) {
            loader.RegisterFunction(ScalarFunction("@>",  {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Contains_tnumber_tbox));
            loader.RegisterFunction(ScalarFunction("<@",  {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Contained_tnumber_tbox));
            loader.RegisterFunction(ScalarFunction("&&",  {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_tnumber_tbox));
            loader.RegisterFunction(ScalarFunction("-|-", {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_tnumber_tbox));
            loader.RegisterFunction(ScalarFunction("@>",  {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Contains_tbox_tnumber));
            loader.RegisterFunction(ScalarFunction("<@",  {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Contained_tbox_tnumber));
            loader.RegisterFunction(ScalarFunction("&&",  {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_tbox_tnumber));
            loader.RegisterFunction(ScalarFunction("-|-", {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_tbox_tnumber));
        }

        // Position ops (<<, >>, &<, &>) — same surface
        loader.RegisterFunction(ScalarFunction("<<",  {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Left_tnumber_numspan));
        loader.RegisterFunction(ScalarFunction(">>",  {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Right_tnumber_numspan));
        loader.RegisterFunction(ScalarFunction("&<",  {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_tnumber_numspan));
        loader.RegisterFunction(ScalarFunction("&>",  {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Overright_tnumber_numspan));
        loader.RegisterFunction(ScalarFunction("<<",  {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Left_tnumber_numspan));
        loader.RegisterFunction(ScalarFunction(">>",  {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Right_tnumber_numspan));
        loader.RegisterFunction(ScalarFunction("&<",  {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_tnumber_numspan));
        loader.RegisterFunction(ScalarFunction("&>",  {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Overright_tnumber_numspan));
        loader.RegisterFunction(ScalarFunction("<<",  {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Left_numspan_tnumber));
        loader.RegisterFunction(ScalarFunction(">>",  {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Right_numspan_tnumber));
        loader.RegisterFunction(ScalarFunction("&<",  {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_numspan_tnumber));
        loader.RegisterFunction(ScalarFunction("&>",  {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Overright_numspan_tnumber));
        loader.RegisterFunction(ScalarFunction("<<",  {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Left_numspan_tnumber));
        loader.RegisterFunction(ScalarFunction(">>",  {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Right_numspan_tnumber));
        loader.RegisterFunction(ScalarFunction("&<",  {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_numspan_tnumber));
        loader.RegisterFunction(ScalarFunction("&>",  {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Overright_numspan_tnumber));
        for (auto &t : {tint, tflt}) {
            loader.RegisterFunction(ScalarFunction("<<", {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Left_tnumber_tbox));
            loader.RegisterFunction(ScalarFunction(">>", {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Right_tnumber_tbox));
            loader.RegisterFunction(ScalarFunction("&<", {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_tnumber_tbox));
            loader.RegisterFunction(ScalarFunction("&>", {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Overright_tnumber_tbox));
            loader.RegisterFunction(ScalarFunction("<<", {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Left_tbox_tnumber));
            loader.RegisterFunction(ScalarFunction(">>", {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Right_tbox_tnumber));
            loader.RegisterFunction(ScalarFunction("&<", {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_tbox_tnumber));
            loader.RegisterFunction(ScalarFunction("&>", {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Overright_tbox_tnumber));
        }
        // tnumber × tnumber (same base type)
        loader.RegisterFunction(ScalarFunction("<<", {tint, tint}, LogicalType::BOOLEAN, TemporalFunctions::Left_tnumber_tnumber));
        loader.RegisterFunction(ScalarFunction(">>", {tint, tint}, LogicalType::BOOLEAN, TemporalFunctions::Right_tnumber_tnumber));
        loader.RegisterFunction(ScalarFunction("&<", {tint, tint}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_tnumber_tnumber));
        loader.RegisterFunction(ScalarFunction("&>", {tint, tint}, LogicalType::BOOLEAN, TemporalFunctions::Overright_tnumber_tnumber));
        loader.RegisterFunction(ScalarFunction("<<", {tflt, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Left_tnumber_tnumber));
        loader.RegisterFunction(ScalarFunction(">>", {tflt, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Right_tnumber_tnumber));
        loader.RegisterFunction(ScalarFunction("&<", {tflt, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_tnumber_tnumber));
        loader.RegisterFunction(ScalarFunction("&>", {tflt, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Overright_tnumber_tnumber));
    }

    // tspatial × {stbox, tspatial} position predicates
    {
        auto tg    = TgeompointType::TGEOMPOINT();
        auto stbox = StboxType::STBOX();

        // L/R operators (DuckDB parser-friendly)
        loader.RegisterFunction(ScalarFunction("<<", {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Left_tspatial_stbox));
        loader.RegisterFunction(ScalarFunction(">>", {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Right_tspatial_stbox));
        loader.RegisterFunction(ScalarFunction("&<", {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_tspatial_stbox));
        loader.RegisterFunction(ScalarFunction("&>", {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Overright_tspatial_stbox));
        loader.RegisterFunction(ScalarFunction("<<", {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Left_stbox_tspatial));
        loader.RegisterFunction(ScalarFunction(">>", {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Right_stbox_tspatial));
        loader.RegisterFunction(ScalarFunction("&<", {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_stbox_tspatial));
        loader.RegisterFunction(ScalarFunction("&>", {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Overright_stbox_tspatial));
        loader.RegisterFunction(ScalarFunction("<<", {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Left_tspatial_tspatial));
        loader.RegisterFunction(ScalarFunction(">>", {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Right_tspatial_tspatial));
        loader.RegisterFunction(ScalarFunction("&<", {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Overleft_tspatial_tspatial));
        loader.RegisterFunction(ScalarFunction("&>", {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Overright_tspatial_tspatial));

        // Above/below/front/back as named functions: DuckDB's parser rejects
        // multi-character tokens like |>>, <<|, /&>, etc. that MobilityDB uses
        // for these predicates. The named-function forms below cover the same
        // surface; see docs/DuckDB-Parity-Gaps.md for the full mapping.
        loader.RegisterFunction(ScalarFunction("below",     {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Below_tspatial_stbox));
        loader.RegisterFunction(ScalarFunction("above",     {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Above_tspatial_stbox));
        loader.RegisterFunction(ScalarFunction("front",     {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Front_tspatial_stbox));
        loader.RegisterFunction(ScalarFunction("back",      {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Back_tspatial_stbox));
        loader.RegisterFunction(ScalarFunction("overbelow", {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Overbelow_tspatial_stbox));
        loader.RegisterFunction(ScalarFunction("overabove", {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Overabove_tspatial_stbox));
        loader.RegisterFunction(ScalarFunction("overfront", {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Overfront_tspatial_stbox));
        loader.RegisterFunction(ScalarFunction("overback",  {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Overback_tspatial_stbox));
        loader.RegisterFunction(ScalarFunction("below",     {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Below_stbox_tspatial));
        loader.RegisterFunction(ScalarFunction("above",     {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Above_stbox_tspatial));
        loader.RegisterFunction(ScalarFunction("front",     {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Front_stbox_tspatial));
        loader.RegisterFunction(ScalarFunction("back",      {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Back_stbox_tspatial));
        loader.RegisterFunction(ScalarFunction("overbelow", {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Overbelow_stbox_tspatial));
        loader.RegisterFunction(ScalarFunction("overabove", {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Overabove_stbox_tspatial));
        loader.RegisterFunction(ScalarFunction("overfront", {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Overfront_stbox_tspatial));
        loader.RegisterFunction(ScalarFunction("overback",  {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Overback_stbox_tspatial));
        loader.RegisterFunction(ScalarFunction("below",     {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Below_tspatial_tspatial));
        loader.RegisterFunction(ScalarFunction("above",     {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Above_tspatial_tspatial));
        loader.RegisterFunction(ScalarFunction("front",     {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Front_tspatial_tspatial));
        loader.RegisterFunction(ScalarFunction("back",      {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Back_tspatial_tspatial));
        loader.RegisterFunction(ScalarFunction("overbelow", {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Overbelow_tspatial_tspatial));
        loader.RegisterFunction(ScalarFunction("overabove", {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Overabove_tspatial_tspatial));
        loader.RegisterFunction(ScalarFunction("overfront", {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Overfront_tspatial_tspatial));
        loader.RegisterFunction(ScalarFunction("overback",  {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Overback_tspatial_tspatial));
    }

    // ttext text functions
    loader.RegisterFunction(ScalarFunction("lower", {TemporalTypes::TTEXT()}, TemporalTypes::TTEXT(), TemporalFunctions::Ttext_lower));
    loader.RegisterFunction(ScalarFunction("upper", {TemporalTypes::TTEXT()}, TemporalTypes::TTEXT(), TemporalFunctions::Ttext_upper));
    loader.RegisterFunction(ScalarFunction("initcap", {TemporalTypes::TTEXT()}, TemporalTypes::TTEXT(), TemporalFunctions::Ttext_initcap));
    loader.RegisterFunction(ScalarFunction("||", {LogicalType::VARCHAR, TemporalTypes::TTEXT()}, TemporalTypes::TTEXT(), TemporalFunctions::Textcat_text_ttext));
    loader.RegisterFunction(ScalarFunction("||", {TemporalTypes::TTEXT(), LogicalType::VARCHAR}, TemporalTypes::TTEXT(), TemporalFunctions::Textcat_ttext_text));
    loader.RegisterFunction(ScalarFunction("||", {TemporalTypes::TTEXT(), TemporalTypes::TTEXT()}, TemporalTypes::TTEXT(), TemporalFunctions::Textcat_ttext_ttext));

    // asBinary / asHexWKB for every temporal type
    loader.RegisterFunction(ScalarFunction("asBinary", {TemporalTypes::TBOOL()}, LogicalType::BLOB, TemporalFunctions::Temporal_as_wkb));
    loader.RegisterFunction(ScalarFunction("asBinary", {TemporalTypes::TBOOL(), LogicalType::VARCHAR}, LogicalType::BLOB, TemporalFunctions::Temporal_as_wkb));
    loader.RegisterFunction(ScalarFunction("asBinary", {TemporalTypes::TINT()}, LogicalType::BLOB, TemporalFunctions::Temporal_as_wkb));
    loader.RegisterFunction(ScalarFunction("asBinary", {TemporalTypes::TINT(), LogicalType::VARCHAR}, LogicalType::BLOB, TemporalFunctions::Temporal_as_wkb));
    loader.RegisterFunction(ScalarFunction("asBinary", {TemporalTypes::TFLOAT()}, LogicalType::BLOB, TemporalFunctions::Temporal_as_wkb));
    loader.RegisterFunction(ScalarFunction("asBinary", {TemporalTypes::TFLOAT(), LogicalType::VARCHAR}, LogicalType::BLOB, TemporalFunctions::Temporal_as_wkb));
    loader.RegisterFunction(ScalarFunction("asBinary", {TemporalTypes::TTEXT()}, LogicalType::BLOB, TemporalFunctions::Temporal_as_wkb));
    loader.RegisterFunction(ScalarFunction("asBinary", {TemporalTypes::TTEXT(), LogicalType::VARCHAR}, LogicalType::BLOB, TemporalFunctions::Temporal_as_wkb));
    loader.RegisterFunction(ScalarFunction("asBinary", {TgeompointType::TGEOMPOINT()}, LogicalType::BLOB, TemporalFunctions::Temporal_as_wkb));
    loader.RegisterFunction(ScalarFunction("asBinary", {TgeompointType::TGEOMPOINT(), LogicalType::VARCHAR}, LogicalType::BLOB, TemporalFunctions::Temporal_as_wkb));

    loader.RegisterFunction(ScalarFunction("asHexWKB", {TemporalTypes::TBOOL()}, LogicalType::VARCHAR, TemporalFunctions::Temporal_as_hexwkb));
    loader.RegisterFunction(ScalarFunction("asHexWKB", {TemporalTypes::TBOOL(), LogicalType::VARCHAR}, LogicalType::VARCHAR, TemporalFunctions::Temporal_as_hexwkb));
    loader.RegisterFunction(ScalarFunction("asHexWKB", {TemporalTypes::TINT()}, LogicalType::VARCHAR, TemporalFunctions::Temporal_as_hexwkb));
    loader.RegisterFunction(ScalarFunction("asHexWKB", {TemporalTypes::TINT(), LogicalType::VARCHAR}, LogicalType::VARCHAR, TemporalFunctions::Temporal_as_hexwkb));
    loader.RegisterFunction(ScalarFunction("asHexWKB", {TemporalTypes::TFLOAT()}, LogicalType::VARCHAR, TemporalFunctions::Temporal_as_hexwkb));
    loader.RegisterFunction(ScalarFunction("asHexWKB", {TemporalTypes::TFLOAT(), LogicalType::VARCHAR}, LogicalType::VARCHAR, TemporalFunctions::Temporal_as_hexwkb));
    loader.RegisterFunction(ScalarFunction("asHexWKB", {TemporalTypes::TTEXT()}, LogicalType::VARCHAR, TemporalFunctions::Temporal_as_hexwkb));
    loader.RegisterFunction(ScalarFunction("asHexWKB", {TemporalTypes::TTEXT(), LogicalType::VARCHAR}, LogicalType::VARCHAR, TemporalFunctions::Temporal_as_hexwkb));
    loader.RegisterFunction(ScalarFunction("asHexWKB", {TgeompointType::TGEOMPOINT()}, LogicalType::VARCHAR, TemporalFunctions::Temporal_as_hexwkb));
    loader.RegisterFunction(ScalarFunction("asHexWKB", {TgeompointType::TGEOMPOINT(), LogicalType::VARCHAR}, LogicalType::VARCHAR, TemporalFunctions::Temporal_as_hexwkb));
}

struct TemporalUnnestBindData : public TableFunctionData {
    string_t blob;
    meosType temptype;
    LogicalType returnType;

    TemporalUnnestBindData(string_t blob, meosType temptype, LogicalType returnType)
        : blob(std::move(blob)), temptype(temptype), returnType(std::move(returnType)) {}
};

struct TemporalUnnestGlobalState : public GlobalTableFunctionState {
    idx_t idx = 0;
    std::vector<std::pair<Value, Value>> values;
};

static unique_ptr<FunctionData> TemporalUnnestBind(ClientContext &context,
                                                   TableFunctionBindInput &input,
                                                   vector<LogicalType> &return_types,
                                                   vector<string> &names) {
    if (input.inputs.size() != 1 || input.inputs[0].IsNull()) {
        throw BinderException("Temporal unnest: expects a non-null blob input");
    }

    auto in_val = input.inputs[0];
    if (in_val.type().id() != LogicalTypeId::BLOB) {
        throw BinderException("Temporal unnest: expected BLOB as input");
    }

    string_t blob = StringValue::Get(in_val);

    auto duck_type = TemporalTypes::GetBaseTypeFromAlias(in_val.type().GetAlias().c_str());
    auto meos_type = TemporalHelpers::GetTemptypeFromAlias(in_val.type().GetAlias().c_str());

    return_types = {duck_type, SpansetTypes::tstzspanset()};
    names = {"value", "time"};

    return make_uniq<TemporalUnnestBindData>(blob, meos_type, duck_type);
}

static unique_ptr<GlobalTableFunctionState> TemporalUnnestInit(ClientContext &context,
                                                               TableFunctionInitInput &input) {
    auto &bind = input.bind_data->Cast<TemporalUnnestBindData>();
    auto &blob = bind.blob;

    const uint8_t *data = (const uint8_t *)blob.GetData();
    size_t size = blob.GetSize();

    Temporal *temp = (Temporal *)malloc(size);
    memcpy(temp, data, size);

    auto state = make_uniq<TemporalUnnestGlobalState>();
    int count;
    Datum *state_values = temporal_values(temp, &count);
    Temporal *state_temp = temporal_copy(temp);

    for (int i = 0; i < count; ++i) {
        Datum values[2];
        values[0] = state_values[i];
        Temporal *rest = temporal_restrict_value(state_temp, state_values[i], true);
        SpanSet *time_spanset = temporal_time(rest);
        values[1] = PointerGetDatum(time_spanset);

        size_t spanset_size = spanset_mem_size(time_spanset);
        uint8_t * spanset_data = (uint8_t *)malloc(spanset_size);
        memcpy(spanset_data, time_spanset, spanset_size);
        Value spanset_blob = Value::BLOB(reinterpret_cast<const unsigned char *>(spanset_data), spanset_size);
        Value spanset_value = spanset_blob.CastAs(context, SpansetTypes::tstzspanset());

        switch (temptype_basetype(bind.temptype)) {
            case T_INT4: {
                int32_t actual_value = DatumGetInt32(values[0]);
                state->values.emplace_back(Value::INTEGER(actual_value), spanset_value);
                break;
            }
            case T_INT8: {
                int64_t actual_value = DatumGetInt64(values[0]);
                state->values.emplace_back(Value::BIGINT(actual_value), spanset_value);
                break;
            }
            case T_FLOAT8: {
                double actual_value = DatumGetFloat8(values[0]);
                state->values.emplace_back(Value::DOUBLE(actual_value), spanset_value);
                break;
            }
            case T_TEXT: {
                string_t actual_value = DatumGetCString(values[0]);
                state->values.emplace_back(Value(actual_value), spanset_value);
                break;
            }
            default:
                free(temp);
                throw NotImplementedException("Temporal unnest: unsupported base type");
        }
    }

    free(temp);
    return std::move(state);
}

static void TemporalUnnestExec(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
    auto &state = input.global_state->Cast<TemporalUnnestGlobalState>();
    auto count = MinValue<idx_t>(STANDARD_VECTOR_SIZE, state.values.size() - state.idx);

    for (idx_t i = 0; i < count; i++) {
        output.SetValue(0, i, state.values[state.idx].first);
        output.SetValue(1, i, state.values[state.idx].second);
        state.idx++;
    }

    output.SetCardinality(count);
}

void TemporalTypes::RegisterTemporalUnnestFunction(ExtensionLoader &loader) {
    for (auto &type : TemporalTypes::AllTypes()) {
        if (type.GetAlias() != "TBOOL") {
            TableFunction fn("tempUnnest",
                            {type},
                            TemporalUnnestExec,
                            TemporalUnnestBind,
                            TemporalUnnestInit);
            loader.RegisterFunction( fn);
        }
    }
}

/* ***************************************************
 * valueSplit(tint|tfloat, size, origin) → SETOF (number, tnumber)
 * ---------------------------------------------------
 * Wraps MEOS tint_value_split / tfloat_value_split. Each row is a (bin-start
 * value, sub-temporal) pair where the sub-temporal is the slice of the input
 * whose value fell into that bin.
 ****************************************************/

struct TnumberValueSplitBindData : public TableFunctionData {
    string_t blob;
    meosType temptype;
    LogicalType base_type;     // BIGINT for tint, DOUBLE for tfloat
    LogicalType temporal_type; // TINT or TFLOAT
    double size;
    double origin;
};

struct TnumberValueSplitGlobalState : public GlobalTableFunctionState {
    idx_t idx = 0;
    std::vector<std::pair<Value, Value>> rows;
};

static unique_ptr<FunctionData> TnumberValueSplitBind(ClientContext &context,
                                                      TableFunctionBindInput &input,
                                                      vector<LogicalType> &return_types,
                                                      vector<string> &names) {
    if (input.inputs.size() < 2 || input.inputs[0].IsNull()) {
        throw BinderException("valueSplit: expects (tint|tfloat, size [, origin])");
    }

    auto in_val = input.inputs[0];
    if (in_val.type().id() != LogicalTypeId::BLOB) {
        throw BinderException("valueSplit: expected a temporal number as first argument");
    }

    auto alias = in_val.type().GetAlias();
    auto bind = make_uniq<TnumberValueSplitBindData>();
    bind->blob = StringValue::Get(in_val);
    bind->temptype = TemporalHelpers::GetTemptypeFromAlias(alias.c_str());
    if (alias == "TINT") {
        bind->base_type = LogicalType::BIGINT;
        bind->temporal_type = TemporalTypes::TINT();
    } else if (alias == "TFLOAT") {
        bind->base_type = LogicalType::DOUBLE;
        bind->temporal_type = TemporalTypes::TFLOAT();
    } else {
        throw BinderException("valueSplit: only tint and tfloat are supported, got %s", alias);
    }

    bind->size = input.inputs[1].GetValue<double>();
    bind->origin = (input.inputs.size() >= 3 && !input.inputs[2].IsNull())
                       ? input.inputs[2].GetValue<double>()
                       : 0.0;

    return_types = {bind->base_type, bind->temporal_type};
    names = {"number", "tnumber"};
    return std::move(bind);
}

static unique_ptr<GlobalTableFunctionState> TnumberValueSplitInit(ClientContext &context,
                                                                  TableFunctionInitInput &input) {
    auto &bind = input.bind_data->Cast<TnumberValueSplitBindData>();
    auto state = make_uniq<TnumberValueSplitGlobalState>();

    const uint8_t *data = (const uint8_t *)bind.blob.GetData();
    size_t size = bind.blob.GetSize();
    Temporal *temp = (Temporal *)malloc(size);
    memcpy(temp, data, size);

    auto make_slice_value = [&](Temporal *slice) {
        size_t slice_size = temporal_mem_size(slice);
        uint8_t *slice_buf = (uint8_t *)malloc(slice_size);
        memcpy(slice_buf, slice, slice_size);
        Value slice_blob = Value::BLOB(slice_buf, slice_size);
        // Carry the BLOB bytes through under the TINT/TFLOAT alias. There's no
        // BLOB → tint/tfloat cast registered (the temporal value is already in
        // its native serialized form), so reinterpret instead of CastAs.
        slice_blob.Reinterpret(bind.temporal_type);
        free(slice_buf);
        return slice_blob;
    };

    int count = 0;
    Temporal **slices = nullptr;
    if (bind.temptype == T_TINT) {
        int *bins_int = nullptr;
        slices = tint_value_split(temp, (int)bind.size, (int)bind.origin, &bins_int, &count);
        for (int i = 0; i < count; ++i) {
            state->rows.emplace_back(Value::BIGINT((int64_t)bins_int[i]), make_slice_value(slices[i]));
            free(slices[i]);
        }
        free(slices);
        free(bins_int);
    } else if (bind.temptype == T_TFLOAT) {
        double *bins_dbl = nullptr;
        slices = tfloat_value_split(temp, bind.size, bind.origin, &bins_dbl, &count);
        for (int i = 0; i < count; ++i) {
            state->rows.emplace_back(Value::DOUBLE(bins_dbl[i]), make_slice_value(slices[i]));
            free(slices[i]);
        }
        free(slices);
        free(bins_dbl);
    }

    free(temp);
    return std::move(state);
}

static void TnumberValueSplitExec(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
    auto &state = input.global_state->Cast<TnumberValueSplitGlobalState>();
    auto count = MinValue<idx_t>(STANDARD_VECTOR_SIZE, state.rows.size() - state.idx);
    for (idx_t i = 0; i < count; ++i) {
        output.SetValue(0, i, state.rows[state.idx].first);
        output.SetValue(1, i, state.rows[state.idx].second);
        state.idx++;
    }
    output.SetCardinality(count);
}

/* ***************************************************
 * frechetDistancePath / dynTimeWarpPath table functions
 * ---------------------------------------------------
 * Wraps MEOS temporal_frechet_path / temporal_dyntimewarp_path. Returns the
 * (i, j) alignment pairs of the two input temporal sequences. Same shape on
 * tnumber × tnumber and tgeompoint × tgeompoint.
 ****************************************************/

enum class SimilarityPathKind { Frechet, DynTimeWarp };

struct SimilarityPathBindData : public TableFunctionData {
    string_t blob1;
    string_t blob2;
    SimilarityPathKind kind;
};

struct SimilarityPathGlobalState : public GlobalTableFunctionState {
    idx_t idx = 0;
    std::vector<std::pair<int32_t, int32_t>> rows;
};

template <SimilarityPathKind KIND>
static unique_ptr<FunctionData> SimilarityPathBind(ClientContext &context,
                                                   TableFunctionBindInput &input,
                                                   vector<LogicalType> &return_types,
                                                   vector<string> &names) {
    if (input.inputs.size() != 2 || input.inputs[0].IsNull() || input.inputs[1].IsNull()) {
        throw BinderException("similarity path: expects two non-null temporal arguments");
    }
    auto bind = make_uniq<SimilarityPathBindData>();
    bind->blob1 = StringValue::Get(input.inputs[0]);
    bind->blob2 = StringValue::Get(input.inputs[1]);
    bind->kind = KIND;
    return_types = {LogicalType::INTEGER, LogicalType::INTEGER};
    names = {"i", "j"};
    return std::move(bind);
}

static unique_ptr<GlobalTableFunctionState> SimilarityPathInit(ClientContext &context,
                                                               TableFunctionInitInput &input) {
    auto &bind = input.bind_data->Cast<SimilarityPathBindData>();
    auto state = make_uniq<SimilarityPathGlobalState>();

    auto load = [](const string_t &blob) -> Temporal * {
        const uint8_t *src = (const uint8_t *)blob.GetData();
        size_t sz = blob.GetSize();
        Temporal *t = (Temporal *)malloc(sz);
        memcpy(t, src, sz);
        return t;
    };
    Temporal *t1 = load(bind.blob1);
    Temporal *t2 = load(bind.blob2);

    int count = 0;
    Match *path = (bind.kind == SimilarityPathKind::Frechet)
                      ? temporal_frechet_path(t1, t2, &count)
                      : temporal_dyntimewarp_path(t1, t2, &count);
    if (path) {
        state->rows.reserve(count);
        for (int i = 0; i < count; ++i) {
            state->rows.emplace_back((int32_t)path[i].i, (int32_t)path[i].j);
        }
        free(path);
    }
    free(t1);
    free(t2);
    return std::move(state);
}

static void SimilarityPathExec(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
    auto &state = input.global_state->Cast<SimilarityPathGlobalState>();
    auto count = MinValue<idx_t>(STANDARD_VECTOR_SIZE, state.rows.size() - state.idx);
    auto i_data = FlatVector::GetData<int32_t>(output.data[0]);
    auto j_data = FlatVector::GetData<int32_t>(output.data[1]);
    for (idx_t k = 0; k < count; ++k) {
        i_data[k] = state.rows[state.idx].first;
        j_data[k] = state.rows[state.idx].second;
        state.idx++;
    }
    output.SetCardinality(count);
}

void TemporalTypes::RegisterSimilarityPath(ExtensionLoader &loader) {
    auto reg = [&](const char *name, const LogicalType &t1, const LogicalType &t2,
                   SimilarityPathKind kind) {
        TableFunction fn(name, {t1, t2}, SimilarityPathExec,
                         (kind == SimilarityPathKind::Frechet)
                             ? SimilarityPathBind<SimilarityPathKind::Frechet>
                             : SimilarityPathBind<SimilarityPathKind::DynTimeWarp>,
                         SimilarityPathInit);
        loader.RegisterFunction(fn);
    };

    reg("frechetDistancePath", TemporalTypes::TINT(),   TemporalTypes::TINT(),   SimilarityPathKind::Frechet);
    reg("frechetDistancePath", TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT(), SimilarityPathKind::Frechet);
    reg("frechetDistancePath", TgeompointType::TGEOMPOINT(), TgeompointType::TGEOMPOINT(), SimilarityPathKind::Frechet);

    reg("dynTimeWarpPath", TemporalTypes::TINT(),   TemporalTypes::TINT(),   SimilarityPathKind::DynTimeWarp);
    reg("dynTimeWarpPath", TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT(), SimilarityPathKind::DynTimeWarp);
    reg("dynTimeWarpPath", TgeompointType::TGEOMPOINT(), TgeompointType::TGEOMPOINT(), SimilarityPathKind::DynTimeWarp);
}

void TemporalTypes::RegisterTnumberValueSplit(ExtensionLoader &loader) {
    // tint variant
    {
        TableFunction fn("valueSplit",
                         {TemporalTypes::TINT(), LogicalType::INTEGER},
                         TnumberValueSplitExec, TnumberValueSplitBind, TnumberValueSplitInit);
        loader.RegisterFunction(fn);
    }
    {
        TableFunction fn("valueSplit",
                         {TemporalTypes::TINT(), LogicalType::INTEGER, LogicalType::INTEGER},
                         TnumberValueSplitExec, TnumberValueSplitBind, TnumberValueSplitInit);
        loader.RegisterFunction(fn);
    }
    // tfloat variant
    {
        TableFunction fn("valueSplit",
                         {TemporalTypes::TFLOAT(), LogicalType::DOUBLE},
                         TnumberValueSplitExec, TnumberValueSplitBind, TnumberValueSplitInit);
        loader.RegisterFunction(fn);
    }
    {
        TableFunction fn("valueSplit",
                         {TemporalTypes::TFLOAT(), LogicalType::DOUBLE, LogicalType::DOUBLE},
                         TnumberValueSplitExec, TnumberValueSplitBind, TnumberValueSplitInit);
        loader.RegisterFunction(fn);
    }
}

} // namespace duckdb