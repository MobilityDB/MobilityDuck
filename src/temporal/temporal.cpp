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
#include "mobilityduck/meos_exec_serial.hpp"

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
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), LogicalType::TIMESTAMP_TZ},
                type,
                TemporalFunctions::Tinstant_constructor
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {type, LogicalType::INTEGER},
                type,
                TemporalFunctions::Temporal_enforce_typmod
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SetTypes::tstzset()},
                type,
                TemporalFunctions::Tsequence_from_base_tstzset
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SpanTypes::TSTZSPAN()},
                type,
                TemporalFunctions::Tsequence_from_base_tstzspan
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SpanTypes::TSTZSPAN(), LogicalType::VARCHAR},
                type,
                TemporalFunctions::Tsequence_from_base_tstzspan
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SpansetTypes::tstzspanset()},
                type,
                TemporalFunctions::Tsequenceset_from_base_tstzspanset
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SpansetTypes::tstzspanset(), LogicalType::VARCHAR},
                type,
                TemporalFunctions::Tsequenceset_from_base_tstzspanset
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "tempSubtype",
                {type},
                LogicalType::VARCHAR,
                TemporalFunctions::Temporal_subtype
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
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
            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "minInstant",
                    {type},
                    type,
                    TemporalFunctions::Temporal_min_instant
                )
            );
    
            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "maxInstant",
                    {type},
                    type,
                    TemporalFunctions::Temporal_max_instant
                )
            );

            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "atMin",
                    {type},
                    type,
                    TemporalFunctions::Temporal_at_min
                )
            );

            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "minusMin",
                    {type},
                    type,
                    TemporalFunctions::Temporal_minus_min
                )
            );

            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "atMax",
                    {type},
                    type,
                    TemporalFunctions::Temporal_at_max
                )
            );

            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "minusMax",
                    {type},
                    type,
                    TemporalFunctions::Temporal_minus_max
                )
            );
        }

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "valueN",
                {type, LogicalType::BIGINT},
                TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()),
                TemporalFunctions::Temporal_value_n
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "getTimestamp",
                {type},
                LogicalType::TIMESTAMP_TZ,
                TemporalFunctions::Tinstant_timestamptz
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "getTime",
                {type},
                SpansetTypes::tstzspanset(),
                TemporalFunctions::Temporal_time
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "duration",
                {type},
                LogicalType::INTERVAL,
                TemporalFunctions::Temporal_duration
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "duration",
                {type, LogicalType::BOOLEAN},
                LogicalType::INTERVAL,
                TemporalFunctions::Temporal_duration
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "Seq",
                {LogicalType::LIST(type)},
                type,
                TemporalFunctions::Tsequence_constructor
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "Seq",
                {LogicalType::LIST(type), LogicalType::VARCHAR},
                type,
                TemporalFunctions::Tsequence_constructor
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "Seq",
                {LogicalType::LIST(type), LogicalType::VARCHAR, LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Tsequence_constructor
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "Inst",
                {type},
                type,
                TemporalFunctions::Temporal_to_tinstant
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "Seq",
                {LogicalType::LIST(type), LogicalType::VARCHAR, LogicalType::BOOLEAN, LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Tsequence_constructor
            )
        );
        
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "Seq",
                {type, LogicalType::VARCHAR},
                type,
                TemporalFunctions::Temporal_to_tsequence
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "Seq",
                {type},
                type,
                TemporalFunctions::Temporal_to_tsequence
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "SeqSet",
                {LogicalType::LIST(type)},
                type,
                TemporalFunctions::Tsequenceset_constructor
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "SeqSet",
                {type},
                type,
                TemporalFunctions::Temporal_to_tsequenceset
            )
        );

        if (type.GetAlias() == "TFLOAT") {
            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    StringUtil::Lower(type.GetAlias()) + "SeqSet",
                    {type, LogicalType::VARCHAR},
                    type,
                    TemporalFunctions::Temporal_to_tsequenceset
                )
            );
        }

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "setInterp",
                {type, LogicalType::VARCHAR},
                type,
                TemporalFunctions::Temporal_set_interp
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "appendInstant",
                {type, type},
                type,
                TemporalFunctions::Temporal_append_tinstant
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "appendInstant",
                {type, type, LogicalType::VARCHAR},
                type,
                TemporalFunctions::Temporal_append_tinstant
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "appendSequence",
                {type, type},
                type,
                TemporalFunctions::Temporal_append_tsequence
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "merge",
                {type, type},
                type,
                TemporalFunctions::Temporal_merge
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "merge",
                {LogicalType::LIST(type)},
                type,
                TemporalFunctions::Temporal_merge_array
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "timeSpan",
                {type},
                SpanTypes::TSTZSPAN(),
                TemporalFunctions::Temporal_to_tstzspan
            )
        );

        if (type.GetAlias() == "TINT") {
            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "valueSpan",
                    {type},
                    SpanTypes::INTSPAN(),
                    TemporalFunctions::Tnumber_to_span
                )
            );

            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "valueSet",
                    {type},
                    SetTypes::intset(),
                    TemporalFunctions::Temporal_valueset
                )
            );
        } else if (type.GetAlias() == "TFLOAT") {
            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "valueSpan",
                    {type},
                    SpanTypes::FLOATSPAN(),
                    TemporalFunctions::Tnumber_to_span
                )
            );

            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "valueSet",
                    {type},
                    SetTypes::floatset(),
                    TemporalFunctions::Temporal_valueset
                )
            );
        }

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "sequences",
                {type},
                LogicalType::LIST(type),
                TemporalFunctions::Temporal_sequences
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "segments",
                {type},
                LogicalType::LIST(type),
                TemporalFunctions::Temporal_segments
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "startTimestamp",
                {type},
                LogicalType::TIMESTAMP_TZ,
                TemporalFunctions::Temporal_start_timestamptz
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "endTimestamp",
                {type},
                LogicalType::TIMESTAMP_TZ,
                TemporalFunctions::Temporal_end_timestamptz
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "timestamps",
                {type},
                LogicalType::LIST(LogicalType::TIMESTAMP_TZ),
                TemporalFunctions::Temporal_timestamps
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "instants",
                {type},
                LogicalType::LIST(type),
                TemporalFunctions::Temporal_instants
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "atTime",
                {type, LogicalType::TIMESTAMP_TZ},
                type,
                TemporalFunctions::Temporal_at_timestamptz
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "atTime",
                {type, SpanTypes::TSTZSPAN()},
                type,
                TemporalFunctions::Temporal_at_tstzspan
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "atTime",
                {type, SpansetTypes::tstzspanset()},
                type,
                TemporalFunctions::Temporal_at_tstzspanset
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "minusTime",
                {type, LogicalType::TIMESTAMP_TZ},
                type,
                TemporalFunctions::Temporal_minus_timestamptz
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "atTime",
                {type, SetTypes::tstzset()},
                type,
                TemporalFunctions::Temporal_at_tstzset
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "minusTime",
                {type, SetTypes::tstzset()},
                type,
                TemporalFunctions::Temporal_minus_tstzset
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "minusTime",
                {type, SpanTypes::TSTZSPAN()},
                type,
                TemporalFunctions::Temporal_minus_tstzspan
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "minusTime",
                {type, SpansetTypes::tstzspanset()},
                type,
                TemporalFunctions::Temporal_minus_tstzspanset
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "valueAtTimestamp",
                {type, LogicalType::TIMESTAMP_TZ},
                TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()),
                TemporalFunctions::Temporal_value_at_timestamptz
            )
        );

        if (type.GetAlias() == "TINT" || type.GetAlias() == "TFLOAT") {
            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "shiftValue",
                    {type, LogicalType::BIGINT},
                    type,
                    TemporalFunctions::Tnumber_shift_value
                )
            );

            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "scaleValue",
                    {type, LogicalType::BIGINT},
                    type,
                    TemporalFunctions::Tnumber_scale_value
                )
            );

            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "shiftScaleValue",
                    {type, LogicalType::BIGINT, LogicalType::BIGINT},
                    type,
                    TemporalFunctions::Tnumber_shift_scale_value
                )
            );

            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "integral",
                    {type},
                    LogicalType::DOUBLE,
                    TemporalFunctions::Tnumber_integral
                )
            );

            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "twAvg",
                    {type},
                    LogicalType::DOUBLE,
                    TemporalFunctions::Tnumber_twavg
                )
            );
        }
        if (type.GetAlias() != "TBOOL") {
            duckdb::RegisterSerializedScalarFunction(loader, 
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
        
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "atValues",
                {type, TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str())},
                type,
                TemporalFunctions::Temporal_at_value
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "minusValues",
                {type, TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str())},
                type,
                TemporalFunctions::Temporal_minus_value
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "beforeTimestamp",
                {type, LogicalType::TIMESTAMP_TZ},
                type,
                TemporalFunctions::Temporal_before_timestamptz
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "beforeTimestamp",
                {type, LogicalType::TIMESTAMP_TZ, LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Temporal_before_timestamptz
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "afterTimestamp",
                {type, LogicalType::TIMESTAMP_TZ},
                type,
                TemporalFunctions::Temporal_after_timestamptz
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "afterTimestamp",
                {type, LogicalType::TIMESTAMP_TZ, LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Temporal_after_timestamptz
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "insert",
                {type, type},
                type,
                TemporalFunctions::Temporal_insert
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "insert",
                {type, type, LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Temporal_insert
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "update",
                {type, type},
                type,
                TemporalFunctions::Temporal_update
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "update",
                {type, type, LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Temporal_update
            )
        );
        
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "deleteTime",
                {type, LogicalType::TIMESTAMP_TZ},
                type,
                TemporalFunctions::Temporal_delete_timestamptz
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "deleteTime",
                {type, LogicalType::TIMESTAMP_TZ, LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Temporal_delete_timestamptz
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "deleteTime",
                {type, SetTypes::tstzset()},
                type,
                TemporalFunctions::Temporal_delete_tstzset
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "deleteTime",
                {type, SetTypes::tstzset(), LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Temporal_delete_tstzset
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "deleteTime",
                {type, SpanTypes::TSTZSPAN()},
                type,
                TemporalFunctions::Temporal_delete_tstzspan
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "deleteTime",
                {type, SpanTypes::TSTZSPAN(), LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Temporal_delete_tstzspan
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "deleteTime",
                {type, SpansetTypes::tstzspanset()},
                type,
                TemporalFunctions::Temporal_delete_tstzspanset
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "deleteTime",
                {type, SpansetTypes::tstzspanset(), LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Temporal_delete_tstzspanset
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "segmentMinDuration",
                {type, LogicalType::INTERVAL},
                type,
                TemporalFunctions::Temporal_segm_min_duration
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "segmentMinDuration",
                {type, LogicalType::INTERVAL, LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Temporal_segm_min_duration
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "segmentMaxDuration",
                {type, LogicalType::INTERVAL},
                type,
                TemporalFunctions::Temporal_segm_max_duration
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "segmentMaxDuration",
                {type, LogicalType::INTERVAL, LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Temporal_segm_max_duration
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "temporal_eq",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_eq
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "=",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_eq
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "temporal_ne",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_ne
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "<>",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_ne
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "temporal_le",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_le
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "<=",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_le
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "temporal_lt",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_lt
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "<",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_lt
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "temporal_ge",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_ge
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                ">=",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_ge
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "temporal_gt",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_gt
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                ">",
                {type, type},
                LogicalType::BOOLEAN,
                TemporalFunctions::Temporal_gt
            )
        );

        duckdb::RegisterSerializedScalarFunction(loader, 
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

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atValues",
            {TemporalTypes::TINT(), SetTypes::intset()},
            TemporalTypes::TINT(),
            TemporalFunctions::Temporal_at_values
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atValues",
            {TemporalTypes::TFLOAT(), SetTypes::floatset()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Temporal_at_values
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atValues",
            {TemporalTypes::TTEXT(), SetTypes::textset()},
            TemporalTypes::TTEXT(),
            TemporalFunctions::Temporal_at_values
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusValues",
            {TemporalTypes::TINT(), SetTypes::intset()},
            TemporalTypes::TINT(),
            TemporalFunctions::Temporal_minus_value
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusValues",
            {TemporalTypes::TFLOAT(), SetTypes::floatset()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Temporal_minus_value
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusValues",
            {TemporalTypes::TTEXT(), SetTypes::textset()},
            TemporalTypes::TTEXT(),
            TemporalFunctions::Temporal_minus_value
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "whenTrue",
            {TemporalTypes::TBOOL()},
            SpansetTypes::tstzspanset(),
            TemporalFunctions::Tbool_when_true
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atValues",
            {TemporalTypes::TINT(), SpanTypes::INTSPAN()},
            TemporalTypes::TINT(),
            TemporalFunctions::Tnumber_at_span
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atValues",
            {TemporalTypes::TFLOAT(), SpanTypes::FLOATSPAN()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Tnumber_at_span
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusValues",
            {TemporalTypes::TINT(), SpanTypes::INTSPAN()},
            TemporalTypes::TINT(),
            TemporalFunctions::Tnumber_minus_span
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusValues",
            {TemporalTypes::TFLOAT(), SpanTypes::FLOATSPAN()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Tnumber_minus_span
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atValues",
            {TemporalTypes::TINT(), SpansetTypes::intspanset()},
            TemporalTypes::TINT(),
            TemporalFunctions::Tnumber_at_spanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atValues",
            {TemporalTypes::TFLOAT(), SpansetTypes::floatspanset()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Tnumber_at_spanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusValues",
            {TemporalTypes::TINT(), SpansetTypes::intspanset()},
            TemporalTypes::TINT(),
            TemporalFunctions::Tnumber_minus_spanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusValues",
            {TemporalTypes::TFLOAT(), SpansetTypes::floatspanset()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Tnumber_minus_spanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atTbox",
            {TemporalTypes::TINT(), TboxType::TBOX()},
            TemporalTypes::TINT(),
            TemporalFunctions::Tnumber_at_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atTbox",
            {TemporalTypes::TFLOAT(), TboxType::TBOX()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Tnumber_at_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusTbox",
            {TemporalTypes::TINT(), TboxType::TBOX()},
            TemporalTypes::TINT(),
            TemporalFunctions::Tnumber_minus_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusTbox",
            {TemporalTypes::TFLOAT(), TboxType::TBOX()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Tnumber_minus_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "round",
            {TemporalTypes::TFLOAT()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Temporal_round
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "round",
            {TemporalTypes::TFLOAT(), LogicalType::INTEGER},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Temporal_round
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tint",
            {TemporalTypes::TBOOL()},
            TemporalTypes::TINT(),
            TemporalFunctions::Tbool_to_tint
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tfloat",
            {TemporalTypes::TINT()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Tint_to_tfloat
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tint",
            {TemporalTypes::TFLOAT()},
            TemporalTypes::TINT(),
            TemporalFunctions::Tfloat_to_tint
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {TemporalTypes::TINT()},
            TboxType::TBOX(),
            TemporalFunctions::Tnumber_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {TemporalTypes::TFLOAT()},
            TboxType::TBOX(),
            TemporalFunctions::Tnumber_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "getValues",
            {TemporalTypes::TINT()},
            SpansetTypes::intspanset(),
            TemporalFunctions::Tnumber_valuespans
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "getValues",
            {TemporalTypes::TFLOAT()},
            SpansetTypes::floatspanset(),
            TemporalFunctions::Tnumber_valuespans
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "avgValue",
            {TemporalTypes::TINT()},
            LogicalType::DOUBLE,
            TemporalFunctions::Tnumber_avg_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "avgValue",
            {TemporalTypes::TFLOAT()},
            LogicalType::DOUBLE,
            TemporalFunctions::Tnumber_avg_value
        )
    );

    // tbool boolean operators
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&", {TemporalTypes::TBOOL(), LogicalType::BOOLEAN}, TemporalTypes::TBOOL(), TemporalFunctions::Tand_tbool_bool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&", {LogicalType::BOOLEAN, TemporalTypes::TBOOL()}, TemporalTypes::TBOOL(), TemporalFunctions::Tand_bool_tbool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&", {TemporalTypes::TBOOL(), TemporalTypes::TBOOL()}, TemporalTypes::TBOOL(), TemporalFunctions::Tand_tbool_tbool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("|", {TemporalTypes::TBOOL(), LogicalType::BOOLEAN}, TemporalTypes::TBOOL(), TemporalFunctions::Tor_tbool_bool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("|", {LogicalType::BOOLEAN, TemporalTypes::TBOOL()}, TemporalTypes::TBOOL(), TemporalFunctions::Tor_bool_tbool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("|", {TemporalTypes::TBOOL(), TemporalTypes::TBOOL()}, TemporalTypes::TBOOL(), TemporalFunctions::Tor_tbool_tbool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("~", {TemporalTypes::TBOOL()}, TemporalTypes::TBOOL(), TemporalFunctions::Tnot_tbool));

    // tnumber arithmetic operators
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("+", {LogicalType::INTEGER, TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Add_int_tint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("+", {TemporalTypes::TINT(), LogicalType::INTEGER}, TemporalTypes::TINT(), TemporalFunctions::Add_tint_int));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("+", {LogicalType::DOUBLE, TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Add_float_tfloat));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("+", {TemporalTypes::TFLOAT(), LogicalType::DOUBLE}, TemporalTypes::TFLOAT(), TemporalFunctions::Add_tfloat_float));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("+", {TemporalTypes::TINT(), TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Add_tnumber_tnumber));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("+", {TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Add_tnumber_tnumber));

    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-", {LogicalType::INTEGER, TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Sub_int_tint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-", {TemporalTypes::TINT(), LogicalType::INTEGER}, TemporalTypes::TINT(), TemporalFunctions::Sub_tint_int));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-", {LogicalType::DOUBLE, TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Sub_float_tfloat));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-", {TemporalTypes::TFLOAT(), LogicalType::DOUBLE}, TemporalTypes::TFLOAT(), TemporalFunctions::Sub_tfloat_float));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-", {TemporalTypes::TINT(), TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Sub_tnumber_tnumber));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-", {TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Sub_tnumber_tnumber));

    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("*", {LogicalType::INTEGER, TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Mult_int_tint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("*", {TemporalTypes::TINT(), LogicalType::INTEGER}, TemporalTypes::TINT(), TemporalFunctions::Mult_tint_int));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("*", {LogicalType::DOUBLE, TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Mult_float_tfloat));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("*", {TemporalTypes::TFLOAT(), LogicalType::DOUBLE}, TemporalTypes::TFLOAT(), TemporalFunctions::Mult_tfloat_float));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("*", {TemporalTypes::TINT(), TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Mult_tnumber_tnumber));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("*", {TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Mult_tnumber_tnumber));

    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("/", {LogicalType::INTEGER, TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Div_int_tint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("/", {TemporalTypes::TINT(), LogicalType::INTEGER}, TemporalTypes::TINT(), TemporalFunctions::Div_tint_int));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("/", {LogicalType::DOUBLE, TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Div_float_tfloat));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("/", {TemporalTypes::TFLOAT(), LogicalType::DOUBLE}, TemporalTypes::TFLOAT(), TemporalFunctions::Div_tfloat_float));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("/", {TemporalTypes::TINT(), TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Div_tnumber_tnumber));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("/", {TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Div_tnumber_tnumber));

    // Unary tnumber functions
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("abs", {TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Tnumber_abs));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("abs", {TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Tnumber_abs));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("derivative", {TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Temporal_derivative));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("degrees", {TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Tfloat_degrees));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("degrees", {TemporalTypes::TFLOAT(), LogicalType::BOOLEAN}, TemporalTypes::TFLOAT(), TemporalFunctions::Tfloat_degrees));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("radians", {TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Tfloat_radians));

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
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<->", {TemporalTypes::TINT(), TemporalTypes::TINT()}, TemporalTypes::TINT(), TemporalFunctions::Tdistance_tnumber_tnumber));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<->", {TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Tdistance_tnumber_tnumber));

    // nearestApproachDistance / nad — scalar return
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nad", {TemporalTypes::TINT(), LogicalType::INTEGER}, LogicalType::INTEGER, TemporalFunctions::Nad_tint_int));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nad", {TemporalTypes::TINT(), TemporalTypes::TINT()}, LogicalType::INTEGER, TemporalFunctions::Nad_tint_tint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nad", {TemporalTypes::TFLOAT(), LogicalType::DOUBLE}, LogicalType::DOUBLE, TemporalFunctions::Nad_tfloat_float));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nad", {TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()}, LogicalType::DOUBLE, TemporalFunctions::Nad_tfloat_tfloat));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nearestApproachDistance", {TemporalTypes::TINT(), LogicalType::INTEGER}, LogicalType::INTEGER, TemporalFunctions::Nad_tint_int));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nearestApproachDistance", {TemporalTypes::TINT(), TemporalTypes::TINT()}, LogicalType::INTEGER, TemporalFunctions::Nad_tint_tint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nearestApproachDistance", {TemporalTypes::TFLOAT(), LogicalType::DOUBLE}, LogicalType::DOUBLE, TemporalFunctions::Nad_tfloat_float));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nearestApproachDistance", {TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()}, LogicalType::DOUBLE, TemporalFunctions::Nad_tfloat_tfloat));

    // Temporal topological predicates: temporal × temporal (5 ops × 4 type pairs).
    // Each operator also registers the matching `temporal_*` named alias.
    for (auto &t1 : TemporalTypes::AllTypes()) {
        for (auto &t2 : TemporalTypes::AllTypes()) {
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("@>",            {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Contains_temporal_temporal));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<@",            {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Contained_temporal_temporal));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&&",            {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_temporal_temporal));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("~=",            {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Same_temporal_temporal));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_same", {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Same_temporal_temporal));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-|-",           {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_temporal_temporal));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contains",  {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Contains_temporal_temporal));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contained", {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Contained_temporal_temporal));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overlaps",  {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_temporal_temporal));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_adjacent",  {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_temporal_temporal));
        }
    }
    // Temporal × tstzspan (and the reverse direction)
    for (auto &t : TemporalTypes::AllTypes()) {
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("@>",            {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Contains_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<@",            {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Contained_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&&",            {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("~=",            {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Same_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_same", {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Same_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-|-",           {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contains",  {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Contains_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contained", {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Contained_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overlaps",  {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_adjacent",  {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_temporal_tstzspan));

        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("@>",            {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Contains_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<@",            {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Contained_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&&",            {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("~=",            {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Same_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_same", {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Same_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-|-",           {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contains",  {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Contains_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contained", {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Contained_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overlaps",  {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_adjacent",  {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_tstzspan_temporal));
    }

    // Temporal time-position predicates registered as named functions:
    // DuckDB's parser does not accept `#` as an operator-name character,
    // so the upstream MobilityDB operators `<<#`, `#>>`, `&<#`, `#&>`
    // are unreachable from SQL. The named-function forms `before`,
    // `after`, `overbefore`, `overafter` provide equivalent behaviour.
    for (auto &t1 : TemporalTypes::AllTypes()) {
        for (auto &t2 : TemporalTypes::AllTypes()) {
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("before",             {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Before_temporal_temporal));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("after",              {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::After_temporal_temporal));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overbefore",         {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Overbefore_temporal_temporal));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overafter",          {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Overafter_temporal_temporal));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_before",    {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Before_temporal_temporal));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_after",     {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::After_temporal_temporal));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overbefore",{t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Overbefore_temporal_temporal));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overafter", {t1, t2}, LogicalType::BOOLEAN, TemporalFunctions::Overafter_temporal_temporal));
        }
    }
    for (auto &t : TemporalTypes::AllTypes()) {
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("before",             {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Before_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("after",              {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::After_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overbefore",         {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Overbefore_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overafter",          {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Overafter_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_before",    {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Before_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_after",     {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::After_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overbefore",{t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Overbefore_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overafter", {t, SpanTypes::TSTZSPAN()}, LogicalType::BOOLEAN, TemporalFunctions::Overafter_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("before",             {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Before_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("after",              {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::After_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overbefore",         {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Overbefore_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overafter",          {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Overafter_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_before",    {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Before_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_after",     {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::After_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overbefore",{SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Overbefore_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overafter", {SpanTypes::TSTZSPAN(), t}, LogicalType::BOOLEAN, TemporalFunctions::Overafter_tstzspan_temporal));
    }

    // Ever / always equality and inequality (named functions; DuckDB
    // parser does not accept ?= / #= operator names).
#define REG_EA(NAME, FN)                                                                                                                                                          \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {LogicalType::BOOLEAN,        TemporalTypes::TBOOL()},   LogicalType::BOOLEAN, TemporalFunctions::FN##_bool_tbool));             \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {TemporalTypes::TBOOL(),      LogicalType::BOOLEAN},     LogicalType::BOOLEAN, TemporalFunctions::FN##_tbool_bool));             \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {LogicalType::INTEGER,        TemporalTypes::TINT()},    LogicalType::BOOLEAN, TemporalFunctions::FN##_int_tint));               \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {TemporalTypes::TINT(),       LogicalType::INTEGER},     LogicalType::BOOLEAN, TemporalFunctions::FN##_tint_int));               \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {LogicalType::DOUBLE,         TemporalTypes::TFLOAT()},  LogicalType::BOOLEAN, TemporalFunctions::FN##_float_tfloat));           \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {TemporalTypes::TFLOAT(),     LogicalType::DOUBLE},      LogicalType::BOOLEAN, TemporalFunctions::FN##_tfloat_float));           \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {TemporalTypes::TINT(),       TemporalTypes::TINT()},    LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal));      \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {TemporalTypes::TFLOAT(),     TemporalTypes::TFLOAT()},  LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal));      \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {TemporalTypes::TBOOL(),      TemporalTypes::TBOOL()},   LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal));

    REG_EA("ever_eq",   Ever_eq)
    REG_EA("always_eq", Always_eq)
    REG_EA("ever_ne",   Ever_ne)
    REG_EA("always_ne", Always_ne)
#undef REG_EA

    // Ordering ever/always — no tbool variant (booleans have no ordering)
#define REG_EA_ORD(NAME, FN)                                                                                                                                          \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {LogicalType::INTEGER,    TemporalTypes::TINT()},   LogicalType::BOOLEAN, TemporalFunctions::FN##_int_tint));        \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {TemporalTypes::TINT(),   LogicalType::INTEGER},    LogicalType::BOOLEAN, TemporalFunctions::FN##_tint_int));        \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {LogicalType::DOUBLE,     TemporalTypes::TFLOAT()}, LogicalType::BOOLEAN, TemporalFunctions::FN##_float_tfloat));    \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {TemporalTypes::TFLOAT(), LogicalType::DOUBLE},     LogicalType::BOOLEAN, TemporalFunctions::FN##_tfloat_float));    \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {TemporalTypes::TINT(),   TemporalTypes::TINT()},   LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal)); \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()}, LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal));

    REG_EA_ORD("ever_lt",   Ever_lt)
    REG_EA_ORD("always_lt", Always_lt)
    REG_EA_ORD("ever_le",   Ever_le)
    REG_EA_ORD("always_le", Always_le)
    REG_EA_ORD("ever_gt",   Ever_gt)
    REG_EA_ORD("always_gt", Always_gt)
    REG_EA_ORD("ever_ge",   Ever_ge)
    REG_EA_ORD("always_ge", Always_ge)
#undef REG_EA_ORD

    // Similarity measures (tnumber × tnumber)
    for (auto &t : {TemporalTypes::TINT(), TemporalTypes::TFLOAT()}) {
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("frechetDistance", {t, t}, LogicalType::DOUBLE, TemporalFunctions::Temporal_frechet_distance));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("discreteFrechet", {t, t}, LogicalType::DOUBLE, TemporalFunctions::Temporal_frechet_distance));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("dynTimeWarp",     {t, t}, LogicalType::DOUBLE, TemporalFunctions::Temporal_dyntimewarp_distance));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("hausdorffDistance", {t, t}, LogicalType::DOUBLE, TemporalFunctions::Temporal_hausdorff_distance));
    }

    // tnumber × {numspan, tbox} topological predicates (4 ops × 8 shape pairs)
    {
        auto tint  = TemporalTypes::TINT();
        auto tflt  = TemporalTypes::TFLOAT();
        auto ispan = SpanTypes::INTSPAN();
        auto fspan = SpanTypes::FLOATSPAN();
        auto tbox  = TboxType::TBOX();

        // tnumber × numspan (5 ops each: @>, <@, &&, ~=, -|-)
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("@>",             {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Contains_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<@",             {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Contained_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&&",             {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("~=",             {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Same_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_same",  {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Same_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-|-",            {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contains",  {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Contains_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contained", {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Contained_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overlaps",  {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_adjacent",  {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("@>",             {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Contains_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<@",             {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Contained_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&&",             {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("~=",             {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Same_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_same",  {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Same_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-|-",            {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contains",  {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Contains_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contained", {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Contained_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overlaps",  {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_adjacent",  {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_tnumber_numspan));
        // numspan × tnumber
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("@>",             {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Contains_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<@",             {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Contained_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&&",             {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("~=",             {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Same_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_same",  {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Same_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-|-",            {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contains",  {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Contains_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contained", {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Contained_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overlaps",  {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_adjacent",  {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("@>",             {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Contains_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<@",             {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Contained_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&&",             {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("~=",             {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Same_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_same",  {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Same_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-|-",            {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contains",  {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Contains_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contained", {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Contained_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overlaps",  {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_adjacent",  {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_numspan_tnumber));
        // tnumber × tbox
        for (auto &t : {tint, tflt}) {
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("@>",             {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Contains_tnumber_tbox));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<@",             {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Contained_tnumber_tbox));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&&",             {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_tnumber_tbox));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("~=",             {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Same_tnumber_tbox));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_same",  {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Same_tnumber_tbox));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-|-",            {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_tnumber_tbox));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contains",  {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Contains_tnumber_tbox));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contained", {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Contained_tnumber_tbox));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overlaps",  {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_tnumber_tbox));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_adjacent",  {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_tnumber_tbox));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("@>",             {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Contains_tbox_tnumber));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<@",             {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Contained_tbox_tnumber));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&&",             {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_tbox_tnumber));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("~=",             {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Same_tbox_tnumber));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_same",  {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Same_tbox_tnumber));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-|-",            {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_tbox_tnumber));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contains",  {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Contains_tbox_tnumber));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contained", {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Contained_tbox_tnumber));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overlaps",  {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_tbox_tnumber));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_adjacent",  {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_tbox_tnumber));
        }

        // Position ops (<<, >>, &<, &>) — same surface
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<<",  {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Left_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(">>",  {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Right_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&<",  {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&>",  {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Overright_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<<",  {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Left_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(">>",  {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Right_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&<",  {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&>",  {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Overright_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<<",  {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Left_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(">>",  {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Right_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&<",  {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&>",  {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Overright_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<<",  {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Left_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(">>",  {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Right_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&<",  {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&>",  {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Overright_numspan_tnumber));
        for (auto &t : {tint, tflt}) {
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<<", {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Left_tnumber_tbox));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(">>", {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Right_tnumber_tbox));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&<", {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_tnumber_tbox));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&>", {t, tbox}, LogicalType::BOOLEAN, TemporalFunctions::Overright_tnumber_tbox));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<<", {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Left_tbox_tnumber));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(">>", {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Right_tbox_tnumber));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&<", {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_tbox_tnumber));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&>", {tbox, t}, LogicalType::BOOLEAN, TemporalFunctions::Overright_tbox_tnumber));
        }
        // tnumber × tnumber (same base type)
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<<", {tint, tint}, LogicalType::BOOLEAN, TemporalFunctions::Left_tnumber_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(">>", {tint, tint}, LogicalType::BOOLEAN, TemporalFunctions::Right_tnumber_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&<", {tint, tint}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_tnumber_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&>", {tint, tint}, LogicalType::BOOLEAN, TemporalFunctions::Overright_tnumber_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<<", {tflt, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Left_tnumber_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(">>", {tflt, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Right_tnumber_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&<", {tflt, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_tnumber_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&>", {tflt, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Overright_tnumber_tnumber));

        // Named aliases for numeric-axis position predicates
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_left",      {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Left_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_right",     {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Right_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overleft",  {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overright", {tint, ispan}, LogicalType::BOOLEAN, TemporalFunctions::Overright_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_left",      {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Left_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_right",     {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Right_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overleft",  {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overright", {tflt, fspan}, LogicalType::BOOLEAN, TemporalFunctions::Overright_tnumber_numspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_left",      {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Left_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_right",     {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Right_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overleft",  {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overright", {ispan, tint}, LogicalType::BOOLEAN, TemporalFunctions::Overright_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_left",      {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Left_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_right",     {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Right_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overleft",  {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_numspan_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overright", {fspan, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Overright_numspan_tnumber));
        for (auto &t : {tint, tflt}) {
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_left",      {t,    tbox}, LogicalType::BOOLEAN, TemporalFunctions::Left_tnumber_tbox));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_right",     {t,    tbox}, LogicalType::BOOLEAN, TemporalFunctions::Right_tnumber_tbox));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overleft",  {t,    tbox}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_tnumber_tbox));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overright", {t,    tbox}, LogicalType::BOOLEAN, TemporalFunctions::Overright_tnumber_tbox));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_left",      {tbox, t},    LogicalType::BOOLEAN, TemporalFunctions::Left_tbox_tnumber));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_right",     {tbox, t},    LogicalType::BOOLEAN, TemporalFunctions::Right_tbox_tnumber));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overleft",  {tbox, t},    LogicalType::BOOLEAN, TemporalFunctions::Overleft_tbox_tnumber));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overright", {tbox, t},    LogicalType::BOOLEAN, TemporalFunctions::Overright_tbox_tnumber));
        }
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_left",      {tint, tint}, LogicalType::BOOLEAN, TemporalFunctions::Left_tnumber_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_right",     {tint, tint}, LogicalType::BOOLEAN, TemporalFunctions::Right_tnumber_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overleft",  {tint, tint}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_tnumber_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overright", {tint, tint}, LogicalType::BOOLEAN, TemporalFunctions::Overright_tnumber_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_left",      {tflt, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Left_tnumber_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_right",     {tflt, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Right_tnumber_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overleft",  {tflt, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_tnumber_tnumber));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overright", {tflt, tflt}, LogicalType::BOOLEAN, TemporalFunctions::Overright_tnumber_tnumber));
    }

    // Temporal comparison predicates returning tbool (temporal_teq/tne/tlt/tle/tgt/tge)
    {
        auto tbool = TemporalTypes::TBOOL();
        auto tint  = TemporalTypes::TINT();
        auto tflt  = TemporalTypes::TFLOAT();
        auto ttext = TemporalTypes::TTEXT();
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_teq", {LogicalType::BOOLEAN,    tbool}, tbool, TemporalFunctions::Teq_bool_tbool));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_teq", {tbool,  LogicalType::BOOLEAN},   tbool, TemporalFunctions::Teq_tbool_bool));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_teq", {LogicalType::INTEGER,    tint},  tbool, TemporalFunctions::Teq_int_tint));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_teq", {tint,   LogicalType::INTEGER},   tbool, TemporalFunctions::Teq_tint_int));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_teq", {LogicalType::DOUBLE,     tflt},  tbool, TemporalFunctions::Teq_float_tfloat));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_teq", {tflt,   LogicalType::DOUBLE},    tbool, TemporalFunctions::Teq_tfloat_float));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_teq", {LogicalType::VARCHAR,    ttext}, tbool, TemporalFunctions::Teq_text_ttext));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_teq", {ttext,  LogicalType::VARCHAR},   tbool, TemporalFunctions::Teq_ttext_text));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_teq", {tint,   tint},                  tbool, TemporalFunctions::Teq_temporal_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_teq", {tflt,   tflt},                  tbool, TemporalFunctions::Teq_temporal_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_teq", {tbool,  tbool},                 tbool, TemporalFunctions::Teq_temporal_temporal));

        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tne", {LogicalType::BOOLEAN,    tbool}, tbool, TemporalFunctions::Tne_bool_tbool));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tne", {tbool,  LogicalType::BOOLEAN},   tbool, TemporalFunctions::Tne_tbool_bool));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tne", {LogicalType::INTEGER,    tint},  tbool, TemporalFunctions::Tne_int_tint));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tne", {tint,   LogicalType::INTEGER},   tbool, TemporalFunctions::Tne_tint_int));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tne", {LogicalType::DOUBLE,     tflt},  tbool, TemporalFunctions::Tne_float_tfloat));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tne", {tflt,   LogicalType::DOUBLE},    tbool, TemporalFunctions::Tne_tfloat_float));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tne", {LogicalType::VARCHAR,    ttext}, tbool, TemporalFunctions::Tne_text_ttext));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tne", {ttext,  LogicalType::VARCHAR},   tbool, TemporalFunctions::Tne_ttext_text));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tne", {tint,   tint},                  tbool, TemporalFunctions::Tne_temporal_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tne", {tflt,   tflt},                  tbool, TemporalFunctions::Tne_temporal_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tne", {tbool,  tbool},                 tbool, TemporalFunctions::Tne_temporal_temporal));

        // temporal_tlt/tle/tgt/tge: ordered types only (int, float, text)
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tlt", {LogicalType::INTEGER,    tint},  tbool, TemporalFunctions::Tlt_int_tint));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tlt", {tint,   LogicalType::INTEGER},   tbool, TemporalFunctions::Tlt_tint_int));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tlt", {LogicalType::DOUBLE,     tflt},  tbool, TemporalFunctions::Tlt_float_tfloat));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tlt", {tflt,   LogicalType::DOUBLE},    tbool, TemporalFunctions::Tlt_tfloat_float));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tlt", {LogicalType::VARCHAR,    ttext}, tbool, TemporalFunctions::Tlt_text_ttext));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tlt", {ttext,  LogicalType::VARCHAR},   tbool, TemporalFunctions::Tlt_ttext_text));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tlt", {tint,   tint},                  tbool, TemporalFunctions::Tlt_temporal_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tlt", {tflt,   tflt},                  tbool, TemporalFunctions::Tlt_temporal_temporal));

        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tle", {LogicalType::INTEGER,    tint},  tbool, TemporalFunctions::Tle_int_tint));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tle", {tint,   LogicalType::INTEGER},   tbool, TemporalFunctions::Tle_tint_int));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tle", {LogicalType::DOUBLE,     tflt},  tbool, TemporalFunctions::Tle_float_tfloat));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tle", {tflt,   LogicalType::DOUBLE},    tbool, TemporalFunctions::Tle_tfloat_float));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tle", {LogicalType::VARCHAR,    ttext}, tbool, TemporalFunctions::Tle_text_ttext));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tle", {ttext,  LogicalType::VARCHAR},   tbool, TemporalFunctions::Tle_ttext_text));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tle", {tint,   tint},                  tbool, TemporalFunctions::Tle_temporal_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tle", {tflt,   tflt},                  tbool, TemporalFunctions::Tle_temporal_temporal));

        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tgt", {LogicalType::INTEGER,    tint},  tbool, TemporalFunctions::Tgt_int_tint));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tgt", {tint,   LogicalType::INTEGER},   tbool, TemporalFunctions::Tgt_tint_int));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tgt", {LogicalType::DOUBLE,     tflt},  tbool, TemporalFunctions::Tgt_float_tfloat));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tgt", {tflt,   LogicalType::DOUBLE},    tbool, TemporalFunctions::Tgt_tfloat_float));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tgt", {LogicalType::VARCHAR,    ttext}, tbool, TemporalFunctions::Tgt_text_ttext));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tgt", {ttext,  LogicalType::VARCHAR},   tbool, TemporalFunctions::Tgt_ttext_text));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tgt", {tint,   tint},                  tbool, TemporalFunctions::Tgt_temporal_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tgt", {tflt,   tflt},                  tbool, TemporalFunctions::Tgt_temporal_temporal));

        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tge", {LogicalType::INTEGER,    tint},  tbool, TemporalFunctions::Tge_int_tint));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tge", {tint,   LogicalType::INTEGER},   tbool, TemporalFunctions::Tge_tint_int));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tge", {LogicalType::DOUBLE,     tflt},  tbool, TemporalFunctions::Tge_float_tfloat));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tge", {tflt,   LogicalType::DOUBLE},    tbool, TemporalFunctions::Tge_tfloat_float));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tge", {LogicalType::VARCHAR,    ttext}, tbool, TemporalFunctions::Tge_text_ttext));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tge", {ttext,  LogicalType::VARCHAR},   tbool, TemporalFunctions::Tge_ttext_text));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tge", {tint,   tint},                  tbool, TemporalFunctions::Tge_temporal_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_tge", {tflt,   tflt},                  tbool, TemporalFunctions::Tge_temporal_temporal));
    }

    // tprecision and tsample — time-domain rebinning
    for (auto &t : TemporalTypes::AllTypes()) {
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tprecision", {t, LogicalType::INTERVAL}, t, TemporalFunctions::Temporal_tprecision));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tprecision", {t, LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ}, t, TemporalFunctions::Temporal_tprecision));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tsample",    {t, LogicalType::INTERVAL}, t, TemporalFunctions::Temporal_tsample));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tsample",    {t, LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ}, t, TemporalFunctions::Temporal_tsample));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tsample",    {t, LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ, LogicalType::VARCHAR}, t, TemporalFunctions::Temporal_tsample));
    }

    // tboxes / splitNTboxes / splitEachNTboxes — tnumber → LIST(TBOX)
    {
        auto tbox_list = LogicalType::LIST(TboxType::TBOX());
        for (auto &t : {TemporalTypes::TINT(), TemporalTypes::TFLOAT()}) {
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tboxes",           {t},                      tbox_list, TemporalFunctions::Tnumber_tboxes));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("splitNTboxes",     {t, LogicalType::INTEGER}, tbox_list, TemporalFunctions::Tnumber_split_n_tboxes));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("splitEachNTboxes", {t, LogicalType::INTEGER}, tbox_list, TemporalFunctions::Tnumber_split_each_n_tboxes));
        }
    }

    // tspatial × {stbox, tspatial} position predicates
    {
        auto tg    = TgeompointType::TGEOMPOINT();
        auto stbox = StboxType::STBOX();

        // L/R operators (DuckDB parser-friendly)
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<<", {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Left_tspatial_stbox));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(">>", {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Right_tspatial_stbox));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&<", {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_tspatial_stbox));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&>", {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Overright_tspatial_stbox));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<<", {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Left_stbox_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(">>", {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Right_stbox_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&<", {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Overleft_stbox_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&>", {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Overright_stbox_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<<", {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Left_tspatial_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(">>", {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Right_tspatial_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&<", {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Overleft_tspatial_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&>", {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Overright_tspatial_tspatial));

        // Above/below/front/back as named functions (DuckDB parser rejects |>>, <<|, etc.)
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("below",     {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Below_tspatial_stbox));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("above",     {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Above_tspatial_stbox));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("front",     {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Front_tspatial_stbox));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("back",      {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Back_tspatial_stbox));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overbelow", {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Overbelow_tspatial_stbox));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overabove", {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Overabove_tspatial_stbox));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overfront", {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Overfront_tspatial_stbox));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overback",  {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::Overback_tspatial_stbox));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("below",     {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Below_stbox_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("above",     {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Above_stbox_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("front",     {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Front_stbox_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("back",      {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Back_stbox_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overbelow", {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Overbelow_stbox_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overabove", {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Overabove_stbox_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overfront", {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Overfront_stbox_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overback",  {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::Overback_stbox_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("below",     {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Below_tspatial_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("above",     {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Above_tspatial_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("front",     {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Front_tspatial_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("back",      {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Back_tspatial_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overbelow", {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Overbelow_tspatial_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overabove", {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Overabove_tspatial_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overfront", {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Overfront_tspatial_tspatial));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overback",  {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::Overback_tspatial_tspatial));
    }

    // ttext text functions
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("lower", {TemporalTypes::TTEXT()}, TemporalTypes::TTEXT(), TemporalFunctions::Ttext_lower));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("upper", {TemporalTypes::TTEXT()}, TemporalTypes::TTEXT(), TemporalFunctions::Ttext_upper));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("initcap", {TemporalTypes::TTEXT()}, TemporalTypes::TTEXT(), TemporalFunctions::Ttext_initcap));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("||", {LogicalType::VARCHAR, TemporalTypes::TTEXT()}, TemporalTypes::TTEXT(), TemporalFunctions::Textcat_text_ttext));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("||", {TemporalTypes::TTEXT(), LogicalType::VARCHAR}, TemporalTypes::TTEXT(), TemporalFunctions::Textcat_ttext_text));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("||", {TemporalTypes::TTEXT(), TemporalTypes::TTEXT()}, TemporalTypes::TTEXT(), TemporalFunctions::Textcat_ttext_ttext));
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
            loader.RegisterFunction(fn);
        }
    }
}

// ─── portable WKB I/O for scalar temporal types ──────────────────────────────
// Uses temporal_as_wkb / temporal_from_wkb (type-agnostic MEOS functions) to
// produce the same MEOS-WKB bytes that tgeompoint's asBinary/tgeompointFromBinary
// already use.  Adding these overloads gives TINT / TFLOAT / TBOOL / TTEXT the
// same Parquet-round-trip story as spatial temporals.

namespace {

inline Temporal *ScalarBlobToTemp(const string_t &b) {
    size_t sz = b.GetSize();
    uint8_t *copy = (uint8_t *)malloc(sz);
    if (!copy) throw InternalException("asBinary: malloc failed");
    memcpy(copy, b.GetData(), sz);
    return reinterpret_cast<Temporal *>(copy);
}

void TemporalScalarAsWkbExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            Temporal *t = ScalarBlobToTemp(input);
            size_t sz = 0;
            uint8_t *wkb = temporal_as_wkb(t, WKB_EXTENDED, &sz);
            free(t);
            if (!wkb || sz == 0) {
                if (wkb) free(wkb);
                throw InternalException("temporal_as_wkb returned null");
            }
            string_t stored = StringVector::AddStringOrBlob(
                result, string_t(reinterpret_cast<const char *>(wkb), sz));
            free(wkb);
            return stored;
        });
}

void TemporalScalarFromWkbExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            if (input.GetSize() == 0)
                throw InvalidInputException("fromBinary: empty WKB input");
            uint8_t *wkb = (uint8_t *)malloc(input.GetSize());
            if (!wkb) throw InternalException("fromBinary: malloc failed");
            memcpy(wkb, input.GetData(), input.GetSize());
            Temporal *t = temporal_from_wkb(wkb, input.GetSize());
            free(wkb);
            if (!t) throw InvalidInputException("fromBinary: invalid MEOS-WKB");
            size_t sz = temporal_mem_size(t);
            string_t stored = StringVector::AddStringOrBlob(
                result, string_t(reinterpret_cast<const char *>(t), sz));
            free(t);
            return stored;
        });
}

} // anonymous namespace

void TemporalTypes::RegisterWkbFunctions(ExtensionLoader &loader) {
    const auto B = LogicalType::BLOB;
    const struct { LogicalType type; const char *from_name; } types[] = {
        { TINT(),   "tintFromBinary"   },
        { TFLOAT(), "tfloatFromBinary" },
        { TBOOL(),  "tboolFromBinary"  },
        { TTEXT(),  "ttextFromBinary"  },
    };
    for (auto &e : types) {
        loader.RegisterFunction(
            ScalarFunction("asBinary", {e.type}, B, TemporalScalarAsWkbExec));
        duckdb::RegisterSerializedScalarFunction(
            loader,
            ScalarFunction(e.from_name, {B}, e.type, TemporalScalarFromWkbExec));
    }
}

} // namespace duckdb