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
#include "geo/tgeometry.hpp"

#include "duckdb/common/types/blob.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/common/types/data_chunk.hpp"

#include "time_util.hpp"
#include "mobilityduck/bindings.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

namespace duckdb {

#define DEFINE_TEMPORAL_TYPE(NAME) \
    LogicalType TemporalTypes::NAME() { \
        LogicalType type(LogicalTypeId::BLOB); \
        type.SetAlias(#NAME); \
        return type; \
    }

DEFINE_TEMPORAL_TYPE(tint)
DEFINE_TEMPORAL_TYPE(tbigint)
DEFINE_TEMPORAL_TYPE(tbool)
DEFINE_TEMPORAL_TYPE(tfloat)
DEFINE_TEMPORAL_TYPE(ttext)

#undef DEFINE_TEMPORAL_TYPE

void TemporalTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType( "tint", tint());
    loader.RegisterType( "tbigint", tbigint());
    loader.RegisterType( "tbool", tbool());
    loader.RegisterType( "tfloat", tfloat());
    loader.RegisterType( "ttext", ttext());
}

const std::vector<LogicalType> &TemporalTypes::AllTypes() {
    static std::vector<LogicalType> types = {
        tint(),
        tbigint(),
        tbool(),
        tfloat(),
        ttext()
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
        RegisterMeosCastFunction(loader, 
            LogicalType::VARCHAR,
            type,
            TemporalFunctions::Temporal_in
        );

        RegisterMeosCastFunction(loader, 
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

    RegisterMeosCastFunction(loader, 
        LogicalType::BLOB,
        SpansetTypes::tstzspanset(),
        TemporalFunctions::Blob_to_tstzspanset
    );

    RegisterMeosCastFunction(loader, 
        TemporalTypes::tbool(),
        TemporalTypes::tint(),
        TemporalFunctions::Tbool_to_tint_cast
    );

    RegisterMeosCastFunction(loader, 
        TemporalTypes::tint(),
        TemporalTypes::tfloat(),
        TemporalFunctions::Tint_to_tfloat_cast
    );

    RegisterMeosCastFunction(loader, 
        TemporalTypes::tfloat(),
        TemporalTypes::tint(),
        TemporalFunctions::Tfloat_to_tint_cast
    );

    RegisterMeosCastFunction(loader, 
        TemporalTypes::tint(),
        TboxType::tbox(),
        TemporalFunctions::Tnumber_to_tbox_cast
    );

    RegisterMeosCastFunction(loader, 
        TemporalTypes::tfloat(),
        TboxType::tbox(),
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
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SpanTypes::tstzspan()},
                type,
                TemporalFunctions::Tsequence_from_base_tstzspan
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SpanTypes::tstzspan(), LogicalType::VARCHAR},
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

        if (type.GetAlias() != "tbool") {
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

        if (type.GetAlias() == "tfloat") {
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
                SpanTypes::tstzspan(),
                TemporalFunctions::Temporal_to_tstzspan
            )
        );

        if (type.GetAlias() == "tint") {
            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "valueSpan",
                    {type},
                    SpanTypes::intspan(),
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
        } else if (type.GetAlias() == "tfloat") {
            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "valueSpan",
                    {type},
                    SpanTypes::floatspan(),
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
                {type, SpanTypes::tstzspan()},
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
                {type, SpanTypes::tstzspan()},
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

        if (type.GetAlias() == "tint" || type.GetAlias() == "tfloat") {
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
        if (type.GetAlias() != "tbool") {
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
                {type, SpanTypes::tstzspan()},
                type,
                TemporalFunctions::Temporal_delete_tstzspan
            )
        );
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                "deleteTime",
                {type, SpanTypes::tstzspan(), LogicalType::BOOLEAN},
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
    // overloads for tint, tbool, tfloat. Each registration pairs the input
    // temporal-type alias with the C++ scalar result type so the generated
    // DuckDB result Vector type matches what the MEOS accessor actually
    // writes, which DuckDB 1.4's UnaryExecutor asserts strictly. See the
    // comment in src/include/mobilityduck/bindings.hpp for the full rationale.
    auto tinstant_value_temporal = [](const Temporal *t) -> uintptr_t {
        return tinstant_value(reinterpret_cast<const TInstant *>(t));
    };

    // getValue(tint / tbool / tfloat) — instant-level accessor
    mobilityduck::RegisterTemporalDatumAccessor<int64_t>(
        loader, "getValue", TemporalTypes::tint(),   LogicalType::BIGINT,  tinstant_value_temporal);
    mobilityduck::RegisterTemporalDatumAccessor<bool>(
        loader, "getValue", TemporalTypes::tbool(),  LogicalType::BOOLEAN, tinstant_value_temporal);
    mobilityduck::RegisterTemporalDatumAccessor<double>(
        loader, "getValue", TemporalTypes::tfloat(), LogicalType::DOUBLE,  tinstant_value_temporal);

    // startValue / endValue on tbool / tfloat. The tint overloads are now owned by
    // the generated surface (canonical INTEGER return, per the catalog `int` — not
    // the legacy hand BIGINT); kept here only for the types the generated marshalling
    // matches identically (tbool->BOOLEAN, tfloat->DOUBLE).
    mobilityduck::RegisterTemporalDatumAccessor<bool>(
        loader, "startValue", TemporalTypes::tbool(),  LogicalType::BOOLEAN, temporal_start_value);
    mobilityduck::RegisterTemporalDatumAccessor<double>(
        loader, "startValue", TemporalTypes::tfloat(), LogicalType::DOUBLE,  temporal_start_value);

    mobilityduck::RegisterTemporalDatumAccessor<bool>(
        loader, "endValue", TemporalTypes::tbool(),  LogicalType::BOOLEAN, temporal_end_value);
    mobilityduck::RegisterTemporalDatumAccessor<double>(
        loader, "endValue", TemporalTypes::tfloat(), LogicalType::DOUBLE,  temporal_end_value);

    // minValue / maxValue on tfloat (tint owned by the generated INTEGER surface;
    // tbool omitted — min/max on a boolean is meaningless and the API omits it)
    mobilityduck::RegisterTemporalDatumAccessor<double>(
        loader, "minValue", TemporalTypes::tfloat(), LogicalType::DOUBLE, temporal_min_value);

    mobilityduck::RegisterTemporalDatumAccessor<double>(
        loader, "maxValue", TemporalTypes::tfloat(), LogicalType::DOUBLE, temporal_max_value);

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atValues",
            {TemporalTypes::tint(), SetTypes::intset()},
            TemporalTypes::tint(),
            TemporalFunctions::Temporal_at_values
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atValues",
            {TemporalTypes::tfloat(), SetTypes::floatset()},
            TemporalTypes::tfloat(),
            TemporalFunctions::Temporal_at_values
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atValues",
            {TemporalTypes::ttext(), SetTypes::textset()},
            TemporalTypes::ttext(),
            TemporalFunctions::Temporal_at_values
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusValues",
            {TemporalTypes::tint(), SetTypes::intset()},
            TemporalTypes::tint(),
            TemporalFunctions::Temporal_minus_value
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusValues",
            {TemporalTypes::tfloat(), SetTypes::floatset()},
            TemporalTypes::tfloat(),
            TemporalFunctions::Temporal_minus_value
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusValues",
            {TemporalTypes::ttext(), SetTypes::textset()},
            TemporalTypes::ttext(),
            TemporalFunctions::Temporal_minus_value
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "whenTrue",
            {TemporalTypes::tbool()},
            SpansetTypes::tstzspanset(),
            TemporalFunctions::Tbool_when_true
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atValues",
            {TemporalTypes::tint(), SpanTypes::intspan()},
            TemporalTypes::tint(),
            TemporalFunctions::Tnumber_at_span
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atValues",
            {TemporalTypes::tfloat(), SpanTypes::floatspan()},
            TemporalTypes::tfloat(),
            TemporalFunctions::Tnumber_at_span
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusValues",
            {TemporalTypes::tint(), SpanTypes::intspan()},
            TemporalTypes::tint(),
            TemporalFunctions::Tnumber_minus_span
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusValues",
            {TemporalTypes::tfloat(), SpanTypes::floatspan()},
            TemporalTypes::tfloat(),
            TemporalFunctions::Tnumber_minus_span
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atValues",
            {TemporalTypes::tint(), SpansetTypes::intspanset()},
            TemporalTypes::tint(),
            TemporalFunctions::Tnumber_at_spanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atValues",
            {TemporalTypes::tfloat(), SpansetTypes::floatspanset()},
            TemporalTypes::tfloat(),
            TemporalFunctions::Tnumber_at_spanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusValues",
            {TemporalTypes::tint(), SpansetTypes::intspanset()},
            TemporalTypes::tint(),
            TemporalFunctions::Tnumber_minus_spanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusValues",
            {TemporalTypes::tfloat(), SpansetTypes::floatspanset()},
            TemporalTypes::tfloat(),
            TemporalFunctions::Tnumber_minus_spanset
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atTbox",
            {TemporalTypes::tint(), TboxType::tbox()},
            TemporalTypes::tint(),
            TemporalFunctions::Tnumber_at_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "atTbox",
            {TemporalTypes::tfloat(), TboxType::tbox()},
            TemporalTypes::tfloat(),
            TemporalFunctions::Tnumber_at_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusTbox",
            {TemporalTypes::tint(), TboxType::tbox()},
            TemporalTypes::tint(),
            TemporalFunctions::Tnumber_minus_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "minusTbox",
            {TemporalTypes::tfloat(), TboxType::tbox()},
            TemporalTypes::tfloat(),
            TemporalFunctions::Tnumber_minus_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "round",
            {TemporalTypes::tfloat()},
            TemporalTypes::tfloat(),
            TemporalFunctions::Temporal_round
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "round",
            {TemporalTypes::tfloat(), LogicalType::INTEGER},
            TemporalTypes::tfloat(),
            TemporalFunctions::Temporal_round
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tint",
            {TemporalTypes::tbool()},
            TemporalTypes::tint(),
            TemporalFunctions::Tbool_to_tint
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tfloat",
            {TemporalTypes::tint()},
            TemporalTypes::tfloat(),
            TemporalFunctions::Tint_to_tfloat
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tint",
            {TemporalTypes::tfloat()},
            TemporalTypes::tint(),
            TemporalFunctions::Tfloat_to_tint
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {TemporalTypes::tint()},
            TboxType::tbox(),
            TemporalFunctions::Tnumber_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "tbox",
            {TemporalTypes::tfloat()},
            TboxType::tbox(),
            TemporalFunctions::Tnumber_to_tbox
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "getValues",
            {TemporalTypes::tint()},
            SpansetTypes::intspanset(),
            TemporalFunctions::Tnumber_valuespans
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "getValues",
            {TemporalTypes::tfloat()},
            SpansetTypes::floatspanset(),
            TemporalFunctions::Tnumber_valuespans
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "avgValue",
            {TemporalTypes::tint()},
            LogicalType::DOUBLE,
            TemporalFunctions::Tnumber_avg_value
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "avgValue",
            {TemporalTypes::tfloat()},
            LogicalType::DOUBLE,
            TemporalFunctions::Tnumber_avg_value
        )
    );

    // tbool boolean operators
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&", {TemporalTypes::tbool(), LogicalType::BOOLEAN}, TemporalTypes::tbool(), TemporalFunctions::Tand_tbool_bool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&", {LogicalType::BOOLEAN, TemporalTypes::tbool()}, TemporalTypes::tbool(), TemporalFunctions::Tand_bool_tbool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&", {TemporalTypes::tbool(), TemporalTypes::tbool()}, TemporalTypes::tbool(), TemporalFunctions::Tand_tbool_tbool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("|", {TemporalTypes::tbool(), LogicalType::BOOLEAN}, TemporalTypes::tbool(), TemporalFunctions::Tor_tbool_bool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("|", {LogicalType::BOOLEAN, TemporalTypes::tbool()}, TemporalTypes::tbool(), TemporalFunctions::Tor_bool_tbool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("|", {TemporalTypes::tbool(), TemporalTypes::tbool()}, TemporalTypes::tbool(), TemporalFunctions::Tor_tbool_tbool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("~", {TemporalTypes::tbool()}, TemporalTypes::tbool(), TemporalFunctions::Tnot_tbool));
    // Portable-SQL aliases (MobilityDB names): tbool_and / tbool_or / tbool_not
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tbool_and", {TemporalTypes::tbool(), LogicalType::BOOLEAN}, TemporalTypes::tbool(), TemporalFunctions::Tand_tbool_bool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tbool_and", {LogicalType::BOOLEAN, TemporalTypes::tbool()}, TemporalTypes::tbool(), TemporalFunctions::Tand_bool_tbool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tbool_and", {TemporalTypes::tbool(), TemporalTypes::tbool()}, TemporalTypes::tbool(), TemporalFunctions::Tand_tbool_tbool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tbool_or", {TemporalTypes::tbool(), LogicalType::BOOLEAN}, TemporalTypes::tbool(), TemporalFunctions::Tor_tbool_bool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tbool_or", {LogicalType::BOOLEAN, TemporalTypes::tbool()}, TemporalTypes::tbool(), TemporalFunctions::Tor_bool_tbool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tbool_or", {TemporalTypes::tbool(), TemporalTypes::tbool()}, TemporalTypes::tbool(), TemporalFunctions::Tor_tbool_tbool));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tbool_not", {TemporalTypes::tbool()}, TemporalTypes::tbool(), TemporalFunctions::Tnot_tbool));

    // tnumber arithmetic operators
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("+", {LogicalType::INTEGER, TemporalTypes::tint()}, TemporalTypes::tint(), TemporalFunctions::Add_int_tint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("+", {TemporalTypes::tint(), LogicalType::INTEGER}, TemporalTypes::tint(), TemporalFunctions::Add_tint_int));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("+", {LogicalType::DOUBLE, TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Add_float_tfloat));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("+", {TemporalTypes::tfloat(), LogicalType::DOUBLE}, TemporalTypes::tfloat(), TemporalFunctions::Add_tfloat_float));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("+", {TemporalTypes::tint(), TemporalTypes::tint()}, TemporalTypes::tint(), TemporalFunctions::Add_tnumber_tnumber));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("+", {TemporalTypes::tfloat(), TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Add_tnumber_tnumber));

    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-", {LogicalType::INTEGER, TemporalTypes::tint()}, TemporalTypes::tint(), TemporalFunctions::Sub_int_tint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-", {TemporalTypes::tint(), LogicalType::INTEGER}, TemporalTypes::tint(), TemporalFunctions::Sub_tint_int));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-", {LogicalType::DOUBLE, TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Sub_float_tfloat));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-", {TemporalTypes::tfloat(), LogicalType::DOUBLE}, TemporalTypes::tfloat(), TemporalFunctions::Sub_tfloat_float));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-", {TemporalTypes::tint(), TemporalTypes::tint()}, TemporalTypes::tint(), TemporalFunctions::Sub_tnumber_tnumber));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-", {TemporalTypes::tfloat(), TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Sub_tnumber_tnumber));

    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("*", {LogicalType::INTEGER, TemporalTypes::tint()}, TemporalTypes::tint(), TemporalFunctions::Mult_int_tint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("*", {TemporalTypes::tint(), LogicalType::INTEGER}, TemporalTypes::tint(), TemporalFunctions::Mult_tint_int));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("*", {LogicalType::DOUBLE, TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Mult_float_tfloat));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("*", {TemporalTypes::tfloat(), LogicalType::DOUBLE}, TemporalTypes::tfloat(), TemporalFunctions::Mult_tfloat_float));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("*", {TemporalTypes::tint(), TemporalTypes::tint()}, TemporalTypes::tint(), TemporalFunctions::Mult_tnumber_tnumber));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("*", {TemporalTypes::tfloat(), TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Mult_tnumber_tnumber));

    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("/", {LogicalType::INTEGER, TemporalTypes::tint()}, TemporalTypes::tint(), TemporalFunctions::Div_int_tint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("/", {TemporalTypes::tint(), LogicalType::INTEGER}, TemporalTypes::tint(), TemporalFunctions::Div_tint_int));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("/", {LogicalType::DOUBLE, TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Div_float_tfloat));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("/", {TemporalTypes::tfloat(), LogicalType::DOUBLE}, TemporalTypes::tfloat(), TemporalFunctions::Div_tfloat_float));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("/", {TemporalTypes::tint(), TemporalTypes::tint()}, TemporalTypes::tint(), TemporalFunctions::Div_tnumber_tnumber));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("/", {TemporalTypes::tfloat(), TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Div_tnumber_tnumber));

    // Unary tnumber functions
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("abs", {TemporalTypes::tint()}, TemporalTypes::tint(), TemporalFunctions::Tnumber_abs));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("abs", {TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Tnumber_abs));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("derivative", {TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Temporal_derivative));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("degrees", {TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Tfloat_degrees));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("degrees", {TemporalTypes::tfloat(), LogicalType::BOOLEAN}, TemporalTypes::tfloat(), TemporalFunctions::Tfloat_degrees));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("radians", {TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Tfloat_radians));

    // Unary tfloat math functions
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("exp",   {TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Tfloat_exp));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("ln",    {TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Tfloat_ln));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("log10", {TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Tfloat_log10));

    // deltaValue / trend on tnumber
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("deltaValue", {TemporalTypes::tint()},   TemporalTypes::tint(),   TemporalFunctions::Tnumber_delta_value));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("deltaValue", {TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Tnumber_delta_value));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("trend",      {TemporalTypes::tint()},   TemporalTypes::tint(),   TemporalFunctions::Tnumber_trend));
    // trend(tfloat) is owned by the generated surface, which returns a tfloat
    // (consistent with deltaValue above); the hand line wrongly returned tint.

    // Named-function aliases for the arithmetic operators (MobilityDB exposes
    // both `+`/`-`/`*`/`/` and `tnumber_add`/`sub`/`mult`/`div`).
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_add",  {LogicalType::INTEGER,    TemporalTypes::tint()},   TemporalTypes::tint(),   TemporalFunctions::Add_int_tint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_add",  {TemporalTypes::tint(),   LogicalType::INTEGER},    TemporalTypes::tint(),   TemporalFunctions::Add_tint_int));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_add",  {LogicalType::DOUBLE,     TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Add_float_tfloat));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_add",  {TemporalTypes::tfloat(), LogicalType::DOUBLE},     TemporalTypes::tfloat(), TemporalFunctions::Add_tfloat_float));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_add",  {TemporalTypes::tint(),   TemporalTypes::tint()},   TemporalTypes::tint(),   TemporalFunctions::Add_tnumber_tnumber));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_add",  {TemporalTypes::tfloat(), TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Add_tnumber_tnumber));

    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_sub",  {LogicalType::INTEGER,    TemporalTypes::tint()},   TemporalTypes::tint(),   TemporalFunctions::Sub_int_tint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_sub",  {TemporalTypes::tint(),   LogicalType::INTEGER},    TemporalTypes::tint(),   TemporalFunctions::Sub_tint_int));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_sub",  {LogicalType::DOUBLE,     TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Sub_float_tfloat));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_sub",  {TemporalTypes::tfloat(), LogicalType::DOUBLE},     TemporalTypes::tfloat(), TemporalFunctions::Sub_tfloat_float));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_sub",  {TemporalTypes::tint(),   TemporalTypes::tint()},   TemporalTypes::tint(),   TemporalFunctions::Sub_tnumber_tnumber));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_sub",  {TemporalTypes::tfloat(), TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Sub_tnumber_tnumber));

    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_mult", {LogicalType::INTEGER,    TemporalTypes::tint()},   TemporalTypes::tint(),   TemporalFunctions::Mult_int_tint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_mult", {TemporalTypes::tint(),   LogicalType::INTEGER},    TemporalTypes::tint(),   TemporalFunctions::Mult_tint_int));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_mult", {LogicalType::DOUBLE,     TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Mult_float_tfloat));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_mult", {TemporalTypes::tfloat(), LogicalType::DOUBLE},     TemporalTypes::tfloat(), TemporalFunctions::Mult_tfloat_float));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_mult", {TemporalTypes::tint(),   TemporalTypes::tint()},   TemporalTypes::tint(),   TemporalFunctions::Mult_tnumber_tnumber));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_mult", {TemporalTypes::tfloat(), TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Mult_tnumber_tnumber));

    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_div",  {LogicalType::INTEGER,    TemporalTypes::tint()},   TemporalTypes::tint(),   TemporalFunctions::Div_int_tint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_div",  {TemporalTypes::tint(),   LogicalType::INTEGER},    TemporalTypes::tint(),   TemporalFunctions::Div_tint_int));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_div",  {LogicalType::DOUBLE,     TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Div_float_tfloat));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_div",  {TemporalTypes::tfloat(), LogicalType::DOUBLE},     TemporalTypes::tfloat(), TemporalFunctions::Div_tfloat_float));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_div",  {TemporalTypes::tint(),   TemporalTypes::tint()},   TemporalTypes::tint(),   TemporalFunctions::Div_tnumber_tnumber));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tnumber_div",  {TemporalTypes::tfloat(), TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Div_tnumber_tnumber));

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
    // The (tint,tint) temporal distance is owned by the generated surface, which
    // returns a tfloat (the temporal distance is always a tfloat — the hand line
    // wrongly typed the result BLOB as tint). The tfloat,tfloat variant matches the
    // generated return and is kept.
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<->", {TemporalTypes::tfloat(), TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Tdistance_tnumber_tnumber));
    // Named form of the same function for SQL portability.
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tdistance", {TemporalTypes::tfloat(), TemporalTypes::tfloat()}, TemporalTypes::tfloat(), TemporalFunctions::Tdistance_tnumber_tnumber));

    // nearestApproachDistance / nad — scalar return
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nad", {TemporalTypes::tint(), LogicalType::INTEGER}, LogicalType::INTEGER, TemporalFunctions::Nad_tint_int));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nad", {TemporalTypes::tint(), TemporalTypes::tint()}, LogicalType::INTEGER, TemporalFunctions::Nad_tint_tint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nad", {TemporalTypes::tfloat(), LogicalType::DOUBLE}, LogicalType::DOUBLE, TemporalFunctions::Nad_tfloat_float));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nad", {TemporalTypes::tfloat(), TemporalTypes::tfloat()}, LogicalType::DOUBLE, TemporalFunctions::Nad_tfloat_tfloat));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nearestApproachDistance", {TemporalTypes::tint(), LogicalType::INTEGER}, LogicalType::INTEGER, TemporalFunctions::Nad_tint_int));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nearestApproachDistance", {TemporalTypes::tint(), TemporalTypes::tint()}, LogicalType::INTEGER, TemporalFunctions::Nad_tint_tint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nearestApproachDistance", {TemporalTypes::tfloat(), LogicalType::DOUBLE}, LogicalType::DOUBLE, TemporalFunctions::Nad_tfloat_float));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("nearestApproachDistance", {TemporalTypes::tfloat(), TemporalTypes::tfloat()}, LogicalType::DOUBLE, TemporalFunctions::Nad_tfloat_tfloat));

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
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("@>",            {t, SpanTypes::tstzspan()}, LogicalType::BOOLEAN, TemporalFunctions::Contains_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<@",            {t, SpanTypes::tstzspan()}, LogicalType::BOOLEAN, TemporalFunctions::Contained_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&&",            {t, SpanTypes::tstzspan()}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("~=",            {t, SpanTypes::tstzspan()}, LogicalType::BOOLEAN, TemporalFunctions::Same_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_same", {t, SpanTypes::tstzspan()}, LogicalType::BOOLEAN, TemporalFunctions::Same_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-|-",           {t, SpanTypes::tstzspan()}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contains",  {t, SpanTypes::tstzspan()}, LogicalType::BOOLEAN, TemporalFunctions::Contains_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contained", {t, SpanTypes::tstzspan()}, LogicalType::BOOLEAN, TemporalFunctions::Contained_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overlaps",  {t, SpanTypes::tstzspan()}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_adjacent",  {t, SpanTypes::tstzspan()}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_temporal_tstzspan));

        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("@>",            {SpanTypes::tstzspan(), t}, LogicalType::BOOLEAN, TemporalFunctions::Contains_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("<@",            {SpanTypes::tstzspan(), t}, LogicalType::BOOLEAN, TemporalFunctions::Contained_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("&&",            {SpanTypes::tstzspan(), t}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("~=",            {SpanTypes::tstzspan(), t}, LogicalType::BOOLEAN, TemporalFunctions::Same_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_same", {SpanTypes::tstzspan(), t}, LogicalType::BOOLEAN, TemporalFunctions::Same_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("-|-",           {SpanTypes::tstzspan(), t}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contains",  {SpanTypes::tstzspan(), t}, LogicalType::BOOLEAN, TemporalFunctions::Contains_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_contained", {SpanTypes::tstzspan(), t}, LogicalType::BOOLEAN, TemporalFunctions::Contained_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overlaps",  {SpanTypes::tstzspan(), t}, LogicalType::BOOLEAN, TemporalFunctions::Overlaps_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_adjacent",  {SpanTypes::tstzspan(), t}, LogicalType::BOOLEAN, TemporalFunctions::Adjacent_tstzspan_temporal));
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
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("before",             {t, SpanTypes::tstzspan()}, LogicalType::BOOLEAN, TemporalFunctions::Before_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("after",              {t, SpanTypes::tstzspan()}, LogicalType::BOOLEAN, TemporalFunctions::After_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overbefore",         {t, SpanTypes::tstzspan()}, LogicalType::BOOLEAN, TemporalFunctions::Overbefore_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overafter",          {t, SpanTypes::tstzspan()}, LogicalType::BOOLEAN, TemporalFunctions::Overafter_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_before",    {t, SpanTypes::tstzspan()}, LogicalType::BOOLEAN, TemporalFunctions::Before_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_after",     {t, SpanTypes::tstzspan()}, LogicalType::BOOLEAN, TemporalFunctions::After_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overbefore",{t, SpanTypes::tstzspan()}, LogicalType::BOOLEAN, TemporalFunctions::Overbefore_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overafter", {t, SpanTypes::tstzspan()}, LogicalType::BOOLEAN, TemporalFunctions::Overafter_temporal_tstzspan));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("before",             {SpanTypes::tstzspan(), t}, LogicalType::BOOLEAN, TemporalFunctions::Before_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("after",              {SpanTypes::tstzspan(), t}, LogicalType::BOOLEAN, TemporalFunctions::After_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overbefore",         {SpanTypes::tstzspan(), t}, LogicalType::BOOLEAN, TemporalFunctions::Overbefore_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("overafter",          {SpanTypes::tstzspan(), t}, LogicalType::BOOLEAN, TemporalFunctions::Overafter_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_before",    {SpanTypes::tstzspan(), t}, LogicalType::BOOLEAN, TemporalFunctions::Before_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_after",     {SpanTypes::tstzspan(), t}, LogicalType::BOOLEAN, TemporalFunctions::After_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overbefore",{SpanTypes::tstzspan(), t}, LogicalType::BOOLEAN, TemporalFunctions::Overbefore_tstzspan_temporal));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("temporal_overafter", {SpanTypes::tstzspan(), t}, LogicalType::BOOLEAN, TemporalFunctions::Overafter_tstzspan_temporal));
    }

    // Same time-position predicates extended to tgeompoint (× tstzspan and
    // × tgeompoint). MEOS dispatches by Temporal* so the same C wrappers work.
    {
        auto tg = TgeompointType::tgeompoint();
        auto tspan = SpanTypes::tstzspan();
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
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {LogicalType::BOOLEAN,        TemporalTypes::tbool()},   LogicalType::BOOLEAN, TemporalFunctions::FN##_bool_tbool));             \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {TemporalTypes::tbool(),      LogicalType::BOOLEAN},     LogicalType::BOOLEAN, TemporalFunctions::FN##_tbool_bool));             \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {LogicalType::INTEGER,        TemporalTypes::tint()},    LogicalType::BOOLEAN, TemporalFunctions::FN##_int_tint));               \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {TemporalTypes::tint(),       LogicalType::INTEGER},     LogicalType::BOOLEAN, TemporalFunctions::FN##_tint_int));               \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {LogicalType::DOUBLE,         TemporalTypes::tfloat()},  LogicalType::BOOLEAN, TemporalFunctions::FN##_float_tfloat));           \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {TemporalTypes::tfloat(),     LogicalType::DOUBLE},      LogicalType::BOOLEAN, TemporalFunctions::FN##_tfloat_float));           \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {TemporalTypes::tint(),       TemporalTypes::tint()},    LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal));      \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {TemporalTypes::tfloat(),     TemporalTypes::tfloat()},  LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal));      \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {TemporalTypes::tbool(),      TemporalTypes::tbool()},   LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal));

    REG_EA("ever_eq",   Ever_eq)
    REG_EA("always_eq", Always_eq)
    REG_EA("ever_ne",   Ever_ne)
    REG_EA("always_ne", Always_ne)
#undef REG_EA

    // Ordering ever/always — no tbool variant (booleans have no ordering)
#define REG_EA_ORD(NAME, FN)                                                                                                                                          \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {LogicalType::INTEGER,    TemporalTypes::tint()},   LogicalType::BOOLEAN, TemporalFunctions::FN##_int_tint));        \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {TemporalTypes::tint(),   LogicalType::INTEGER},    LogicalType::BOOLEAN, TemporalFunctions::FN##_tint_int));        \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {LogicalType::DOUBLE,     TemporalTypes::tfloat()}, LogicalType::BOOLEAN, TemporalFunctions::FN##_float_tfloat));    \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {TemporalTypes::tfloat(), LogicalType::DOUBLE},     LogicalType::BOOLEAN, TemporalFunctions::FN##_tfloat_float));    \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {TemporalTypes::tint(),   TemporalTypes::tint()},   LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal)); \
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(NAME, {TemporalTypes::tfloat(), TemporalTypes::tfloat()}, LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal));

    REG_EA_ORD("ever_lt",   Ever_lt)
    REG_EA_ORD("always_lt", Always_lt)
    REG_EA_ORD("ever_le",   Ever_le)
    REG_EA_ORD("always_le", Always_le)
    REG_EA_ORD("ever_gt",   Ever_gt)
    REG_EA_ORD("always_gt", Always_gt)
    REG_EA_ORD("ever_ge",   Ever_ge)
    REG_EA_ORD("always_ge", Always_ge)
#undef REG_EA_ORD

    // Similarity: frechetDistance/dynTimeWarpDistance/hausdorffDistance (scalar) and
    // frechetDistancePath/dynTimeWarpPath (SETOF -> DuckDB table function) are all GENERATED
    // from the catalog now. The non-canonical discreteFrechet/dynTimeWarp hand aliases were
    // retired (MobilityDB canonical = frechetDistance/dynTimeWarpDistance).

    // simplify family (subtype-agnostic but only meaningful on linear temporal types)
    for (const auto &t : {TemporalTypes::tfloat(), TgeompointType::tgeompoint(),
                          TGeometryTypes::tgeometry()}) {
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("douglasPeuckerSimplify",
            {t, LogicalType::DOUBLE}, t, TemporalFunctions::Temporal_simplify_dp));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("douglasPeuckerSimplify",
            {t, LogicalType::DOUBLE, LogicalType::BOOLEAN}, t,
            TemporalFunctions::Temporal_simplify_dp));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("maxDistSimplify",
            {t, LogicalType::DOUBLE}, t, TemporalFunctions::Temporal_simplify_max_dist));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("maxDistSimplify",
            {t, LogicalType::DOUBLE, LogicalType::BOOLEAN}, t,
            TemporalFunctions::Temporal_simplify_max_dist));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("minDistSimplify",
            {t, LogicalType::DOUBLE}, t, TemporalFunctions::Temporal_simplify_min_dist));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("minTimeDeltaSimplify",
            {t, LogicalType::INTERVAL}, t,
            TemporalFunctions::Temporal_simplify_min_tdelta));
    }

    // tnumber × {numspan, tbox} topological predicates (4 ops × 8 shape pairs)
    {
        auto tint  = TemporalTypes::tint();
        auto tflt  = TemporalTypes::tfloat();
        auto ispan = SpanTypes::intspan();
        auto fspan = SpanTypes::floatspan();
        auto tbox  = TboxType::tbox();

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

    // Temporal #= comparison (temporal_teq/tne/tlt/tle/tgt/tge) is generated
    // (group meos_temporal_comp_temp).
    // tprecision and tsample — time-domain rebinning
    for (auto &t : TemporalTypes::AllTypes()) {
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tprecision", {t, LogicalType::INTERVAL}, t, TemporalFunctions::Temporal_tprecision));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tprecision", {t, LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ}, t, TemporalFunctions::Temporal_tprecision));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tsample",    {t, LogicalType::INTERVAL}, t, TemporalFunctions::Temporal_tsample));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tsample",    {t, LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ}, t, TemporalFunctions::Temporal_tsample));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tsample",    {t, LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ, LogicalType::VARCHAR}, t, TemporalFunctions::Temporal_tsample));
    }

    // tboxes / splitNTboxes / splitEachNTboxes — tnumber → LIST(tbox)
    {
        auto tbox_list = LogicalType::LIST(TboxType::tbox());
        for (auto &t : {TemporalTypes::tint(), TemporalTypes::tfloat()}) {
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tboxes",           {t},                      tbox_list, TemporalFunctions::Tnumber_tboxes));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("splitNTboxes",     {t, LogicalType::INTEGER}, tbox_list, TemporalFunctions::Tnumber_split_n_tboxes));
            duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("splitEachNTboxes", {t, LogicalType::INTEGER}, tbox_list, TemporalFunctions::Tnumber_split_each_n_tboxes));
        }
    }

    // tspatial × {stbox, tspatial} position predicates.
    //
    // For each direction (left/right/below/above/front/back and the over*
    // variants, plus the time-axis before/after/overbefore/overafter), the
    // predicate is registered both as the operator form (where DuckDB's
    // parser accepts the token, only L/R for now: <<, >>, &<, &>) and as
    // both the bare named form (left/right/below/above/front/back/before/
    // after/overbelow/overabove/overfront/overback/overbefore/overafter)
    // and the MobilityDB-canonical `temporal_*` alias.
    {
        auto tg    = TgeompointType::tgeompoint();
        auto stbox = StboxType::stbox();

#define REG_TSPATIAL_OP(SQL_NAME, ALIAS, FN)                                                                                                  \
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(SQL_NAME, {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::FN##_tspatial_stbox));   \
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(ALIAS,    {tg, stbox}, LogicalType::BOOLEAN, TemporalFunctions::FN##_tspatial_stbox));   \
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(SQL_NAME, {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::FN##_stbox_tspatial));   \
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(ALIAS,    {stbox, tg}, LogicalType::BOOLEAN, TemporalFunctions::FN##_stbox_tspatial));   \
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(SQL_NAME, {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::FN##_tspatial_tspatial));\
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(ALIAS,    {tg, tg},    LogicalType::BOOLEAN, TemporalFunctions::FN##_tspatial_tspatial));

        // L/R as both operator and named alias (DuckDB accepts <<, >>, &<, &> tokens)
        REG_TSPATIAL_OP("<<", "temporal_left",      Left)
        REG_TSPATIAL_OP(">>", "temporal_right",     Right)
        REG_TSPATIAL_OP("&<", "temporal_overleft",  Overleft)
        REG_TSPATIAL_OP("&>", "temporal_overright", Overright)

        // Vertical / Z-axis as named only (DuckDB rejects <<|, |>>, /<<, >>/, etc.)
        REG_TSPATIAL_OP("below",     "temporal_below",     Below)
        REG_TSPATIAL_OP("above",     "temporal_above",     Above)
        REG_TSPATIAL_OP("front",     "temporal_front",     Front)
        REG_TSPATIAL_OP("back",      "temporal_back",      Back)
        REG_TSPATIAL_OP("overbelow", "temporal_overbelow", Overbelow)
        REG_TSPATIAL_OP("overabove", "temporal_overabove", Overabove)
        REG_TSPATIAL_OP("overfront", "temporal_overfront", Overfront)
        REG_TSPATIAL_OP("overback",  "temporal_overback",  Overback)

        // Time-axis on tspatial as named only (DuckDB rejects <<#, #>>, &<#, #&>)
        REG_TSPATIAL_OP("before",     "temporal_before",     Before)
        REG_TSPATIAL_OP("after",      "temporal_after",      After)
        REG_TSPATIAL_OP("overbefore", "temporal_overbefore", Overbefore)
        REG_TSPATIAL_OP("overafter",  "temporal_overafter",  Overafter)

#undef REG_TSPATIAL_OP
    }

    // ttext text functions
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("lower", {TemporalTypes::ttext()}, TemporalTypes::ttext(), TemporalFunctions::Ttext_lower));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("upper", {TemporalTypes::ttext()}, TemporalTypes::ttext(), TemporalFunctions::Ttext_upper));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("initcap", {TemporalTypes::ttext()}, TemporalTypes::ttext(), TemporalFunctions::Ttext_initcap));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("||", {LogicalType::VARCHAR, TemporalTypes::ttext()}, TemporalTypes::ttext(), TemporalFunctions::Textcat_text_ttext));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("||", {TemporalTypes::ttext(), LogicalType::VARCHAR}, TemporalTypes::ttext(), TemporalFunctions::Textcat_ttext_text));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("||", {TemporalTypes::ttext(), TemporalTypes::ttext()}, TemporalTypes::ttext(), TemporalFunctions::Textcat_ttext_ttext));
    // Portable-SQL alias (MobilityDB name): ttext_cat
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("ttext_cat", {LogicalType::VARCHAR, TemporalTypes::ttext()}, TemporalTypes::ttext(), TemporalFunctions::Textcat_text_ttext));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("ttext_cat", {TemporalTypes::ttext(), LogicalType::VARCHAR}, TemporalTypes::ttext(), TemporalFunctions::Textcat_ttext_text));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("ttext_cat", {TemporalTypes::ttext(), TemporalTypes::ttext()}, TemporalTypes::ttext(), TemporalFunctions::Textcat_ttext_ttext));
}

struct TemporalUnnestBindData : public TableFunctionData {
    string_t blob;
    MeosType temptype;
    LogicalType returnType;

    TemporalUnnestBindData(string_t blob, MeosType temptype, LogicalType returnType)
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
    EnsureMeosThreadInitialized();
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
        if (type.GetAlias() != "tbool") {
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
// already use.  Adding these overloads gives tint / tfloat / tbool / ttext the
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

void TemporalScalarFromHexWkbExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string hex(input.GetData(), input.GetSize());
            Temporal *t = temporal_from_hexwkb(hex.c_str());
            if (!t) throw InvalidInputException(
                "fromHexWKB: invalid hex-encoded MEOS-WKB");
            size_t sz = temporal_mem_size(t);
            string_t stored = StringVector::AddStringOrBlob(
                result, string_t(reinterpret_cast<const char *>(t), sz));
            free(t);
            return stored;
        });
}

template <Temporal *(*FN)(const char *)>
void TemporalScalarFromMfjsonExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string mfj(input.GetData(), input.GetSize());
            Temporal *t = FN(mfj.c_str());
            if (!t) throw InvalidInputException(
                "fromMFJSON: invalid MFJSON input");
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
    const auto V = LogicalType::VARCHAR;
    struct Entry {
        LogicalType type;
        const char *bin_name;
        const char *hex_name;
        const char *mfj_name;
        scalar_function_t mfj_exec;
    };
    const Entry types[] = {
        { tint(),   "tintFromBinary",
          "tintFromHexWKB",   "tintFromMFJSON",
          TemporalScalarFromMfjsonExec<&tint_from_mfjson> },
        { tfloat(), "tfloatFromBinary",
          "tfloatFromHexWKB", "tfloatFromMFJSON",
          TemporalScalarFromMfjsonExec<&tfloat_from_mfjson> },
        { tbool(),  "tboolFromBinary",
          "tboolFromHexWKB",  "tboolFromMFJSON",
          TemporalScalarFromMfjsonExec<&tbool_from_mfjson> },
        { ttext(),  "ttextFromBinary",
          "ttextFromHexWKB",  "ttextFromMFJSON",
          TemporalScalarFromMfjsonExec<&ttext_from_mfjson> },
    };
    for (auto &e : types) {
        loader.RegisterFunction(
            ScalarFunction("asBinary", {e.type}, B, TemporalScalarAsWkbExec));
        duckdb::RegisterSerializedScalarFunction(
            loader,
            ScalarFunction(e.bin_name, {B}, e.type, TemporalScalarFromWkbExec));
        duckdb::RegisterSerializedScalarFunction(
            loader,
            ScalarFunction(e.hex_name, {V}, e.type, TemporalScalarFromHexWkbExec));
        duckdb::RegisterSerializedScalarFunction(
            loader,
            ScalarFunction(e.mfj_name, {V}, e.type, e.mfj_exec));
    }
}

namespace {

inline string_t MallocBlobToResultLocal(Vector &result, void *buf, size_t sz) {
    string_t blob(reinterpret_cast<const char *>(buf), UnsafeNumericCast<uint32_t>(sz));
    string_t stored = StringVector::AddStringOrBlob(result, blob);
    free(buf);
    return stored;
}

// ----- getBin: single-bin getters returning spans -----
//
// MobilityDB SQL: getBin(value, size, origin) -> <type>span. We compute the
// lower bound via MEOS *_get_bin and pair it with upper = lower + size to
// emit a [lower, upper) span blob. Time/date variants stride the duration
// interval rather than a numeric size.

inline string_t SpanToBlob(Vector &result, Span *span) {
    string_t out = StringVector::AddStringOrBlob(
        result, reinterpret_cast<const char *>(span), sizeof(Span));
    free(span);
    return out;
}

void GetBinIntExec(DataChunk &args, ExpressionState &, Vector &result) {
    TernaryExecutor::Execute<int32_t, int32_t, int32_t, string_t>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](int32_t v, int32_t vsize, int32_t vorigin) {
            int lower = int_get_bin(v, vsize, vorigin);
            Span *span = intspan_make(lower, lower + vsize, true, false);
            return SpanToBlob(result, span);
        });
}

void GetBinBigintExec(DataChunk &args, ExpressionState &, Vector &result) {
    TernaryExecutor::Execute<int64_t, int64_t, int64_t, string_t>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](int64_t v, int64_t vsize, int64_t vorigin) {
            int64_t lower = bigint_get_bin(v, vsize, vorigin);
            Span *span = bigintspan_make(lower, lower + vsize, true, false);
            return SpanToBlob(result, span);
        });
}

void GetBinFloatExec(DataChunk &args, ExpressionState &, Vector &result) {
    TernaryExecutor::Execute<double, double, double, string_t>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](double v, double vsize, double vorigin) {
            double lower = float_get_bin(v, vsize, vorigin);
            Span *span = floatspan_make(lower, lower + vsize, true, false);
            return SpanToBlob(result, span);
        });
}

void GetBinTstzExec(DataChunk &args, ExpressionState &, Vector &result) {
    TernaryExecutor::Execute<timestamp_tz_t, interval_t, timestamp_tz_t, string_t>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](timestamp_tz_t t, interval_t duration, timestamp_tz_t torigin) {
            timestamp_tz_t meos_t = DuckDBToMeosTimestamp(t);
            timestamp_tz_t meos_torigin = DuckDBToMeosTimestamp(torigin);
            MeosInterval iv = IntervaltToInterval(duration);
            TimestampTz lower_meos = timestamptz_get_bin(
                (TimestampTz) meos_t.value, &iv, (TimestampTz) meos_torigin.value);
            TimestampTz upper_meos = add_timestamptz_interval(lower_meos, &iv);
            Span *span = tstzspan_make(lower_meos, upper_meos, true, false);
            return SpanToBlob(result, span);
        });
}

void GetBinDateExec(DataChunk &args, ExpressionState &, Vector &result) {
    TernaryExecutor::Execute<date_t, interval_t, date_t, string_t>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](date_t d, interval_t duration, date_t origin) {
            int32_t meos_d = ToMeosDate(d);
            int32_t meos_origin = ToMeosDate(origin);
            MeosInterval iv = IntervaltToInterval(duration);
            DateADT lower = date_get_bin((DateADT) meos_d, &iv, (DateADT) meos_origin);
            // Date bins use duration.day; months/micros are not meaningful
            // for date-aligned bins (MobilityDB rejects them upstream).
            DateADT upper = add_date_int(lower, (int32) iv.day);
            Span *span = datespan_make(lower, upper, true, false);
            return SpanToBlob(result, span);
        });
}

// ----- tbox tile emitters: LIST(tbox) outputs -----

inline void EmitTboxList(Vector &result, idx_t row_idx, TBox *tiles, int count,
                         idx_t &total_offset, list_entry_t *list_entries,
                         Vector &child_vector, ValidityMask &result_validity) {
    if (!tiles || count <= 0) {
        if (tiles) free(tiles);
        result_validity.SetInvalid(row_idx);
        return;
    }
    ListVector::SetListSize(result, total_offset + count);
    list_entries[row_idx] = list_entry_t{total_offset, static_cast<uint64_t>(count)};
    auto *child_data = FlatVector::GetData<string_t>(child_vector);
    const size_t tbox_bytes = sizeof(TBox);
    for (int j = 0; j < count; ++j) {
        child_data[total_offset + j] = StringVector::AddStringOrBlob(
            child_vector, reinterpret_cast<const char *>(&tiles[j]), tbox_bytes);
    }
    free(tiles);
    total_offset += count;
}

// valueTiles(tbox, vsize [, vorigin]) — int branch uses int xsize/xorigin, float uses double
void TboxValueTilesExec(DataChunk &args, ExpressionState &, Vector &result) {
    auto &tbox_vec = args.data[0];
    auto &vsize_vec = args.data[1];
    Vector *vorigin_vec = args.ColumnCount() >= 3 ? &args.data[2] : nullptr;
    idx_t row_count = args.size();
    tbox_vec.Flatten(row_count);
    vsize_vec.Flatten(row_count);
    if (vorigin_vec) vorigin_vec->Flatten(row_count);

    auto &result_validity = FlatVector::Validity(result);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &child_vector = ListVector::GetEntry(result);
    child_vector.SetVectorType(VectorType::FLAT_VECTOR);
    ListVector::Reserve(result, row_count);
    idx_t total_offset = 0;

    auto tbox_data = FlatVector::GetData<string_t>(tbox_vec);
    for (idx_t i = 0; i < row_count; ++i) {
        if (FlatVector::IsNull(tbox_vec, i) || FlatVector::IsNull(vsize_vec, i) ||
            (vorigin_vec && FlatVector::IsNull(*vorigin_vec, i))) {
            result_validity.SetInvalid(i);
            continue;
        }
        string_t blob = tbox_data[i];
        TBox *box = (TBox *) malloc(blob.GetSize());
        memcpy(box, blob.GetData(), blob.GetSize());

        int count = 0;
        TBox *tiles = nullptr;
        if (box->span.spantype == T_INTSPAN) {
            int32_t vsize = FlatVector::GetData<int32_t>(vsize_vec)[i];
            int32_t vorigin = vorigin_vec ? FlatVector::GetData<int32_t>(*vorigin_vec)[i] : 0;
            tiles = tintbox_value_tiles(box, vsize, vorigin, &count);
        } else if (box->span.spantype == T_FLOATSPAN) {
            double vsize = FlatVector::GetData<double>(vsize_vec)[i];
            double vorigin = vorigin_vec ? FlatVector::GetData<double>(*vorigin_vec)[i] : 0.0;
            tiles = tfloatbox_value_tiles(box, vsize, vorigin, &count);
        } else {
            free(box);
            throw InvalidInputException("valueTiles: tbox has no value dimension");
        }
        free(box);
        EmitTboxList(result, i, tiles, count, total_offset, list_entries, child_vector,
                     result_validity);
    }
}

// MobilityDB default torigin for time bins: '2000-01-03' (a Monday).
// In MEOS PG-epoch microseconds that is 2 days * 86_400 * 1_000_000.
constexpr int64_t DEFAULT_TIME_ORIGIN_MEOS = 2LL * 86400LL * 1000000LL;

// timeTiles(tbox, duration [, torigin])
void TboxTimeTilesExec(DataChunk &args, ExpressionState &, Vector &result) {
    auto &tbox_vec = args.data[0];
    auto &dur_vec = args.data[1];
    Vector *torigin_vec = args.ColumnCount() >= 3 ? &args.data[2] : nullptr;
    idx_t row_count = args.size();
    tbox_vec.Flatten(row_count);
    dur_vec.Flatten(row_count);
    if (torigin_vec) torigin_vec->Flatten(row_count);

    auto &result_validity = FlatVector::Validity(result);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &child_vector = ListVector::GetEntry(result);
    child_vector.SetVectorType(VectorType::FLAT_VECTOR);
    ListVector::Reserve(result, row_count);
    idx_t total_offset = 0;

    auto tbox_data = FlatVector::GetData<string_t>(tbox_vec);
    auto dur_data = FlatVector::GetData<interval_t>(dur_vec);
    for (idx_t i = 0; i < row_count; ++i) {
        if (FlatVector::IsNull(tbox_vec, i) || FlatVector::IsNull(dur_vec, i) ||
            (torigin_vec && FlatVector::IsNull(*torigin_vec, i))) {
            result_validity.SetInvalid(i);
            continue;
        }
        string_t blob = tbox_data[i];
        TBox *box = (TBox *) malloc(blob.GetSize());
        memcpy(box, blob.GetData(), blob.GetSize());

        MeosInterval iv = IntervaltToInterval(dur_data[i]);
        timestamp_tz_t torigin_meos;
        torigin_meos.value = DEFAULT_TIME_ORIGIN_MEOS;
        if (torigin_vec) {
            timestamp_tz_t torigin_in = FlatVector::GetData<timestamp_tz_t>(*torigin_vec)[i];
            torigin_meos = DuckDBToMeosTimestamp(torigin_in);
        }

        int count = 0;
        TBox *tiles = nullptr;
        if (box->span.spantype == T_INTSPAN) {
            tiles = tintbox_time_tiles(box, &iv, (TimestampTz) torigin_meos.value, &count);
        } else if (box->span.spantype == T_FLOATSPAN) {
            tiles = tfloatbox_time_tiles(box, &iv, (TimestampTz) torigin_meos.value, &count);
        } else {
            free(box);
            throw InvalidInputException("timeTiles: tbox has no value dimension to dispatch on");
        }
        free(box);
        EmitTboxList(result, i, tiles, count, total_offset, list_entries, child_vector,
                     result_validity);
    }
}

// valueTimeTiles(tbox, vsize, duration [, vorigin, torigin])
void TboxValueTimeTilesExec(DataChunk &args, ExpressionState &, Vector &result) {
    auto &tbox_vec = args.data[0];
    auto &vsize_vec = args.data[1];
    auto &dur_vec = args.data[2];
    Vector *vorigin_vec = args.ColumnCount() >= 4 ? &args.data[3] : nullptr;
    Vector *torigin_vec = args.ColumnCount() >= 5 ? &args.data[4] : nullptr;
    idx_t row_count = args.size();
    tbox_vec.Flatten(row_count);
    vsize_vec.Flatten(row_count);
    dur_vec.Flatten(row_count);
    if (vorigin_vec) vorigin_vec->Flatten(row_count);
    if (torigin_vec) torigin_vec->Flatten(row_count);

    auto &result_validity = FlatVector::Validity(result);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &child_vector = ListVector::GetEntry(result);
    child_vector.SetVectorType(VectorType::FLAT_VECTOR);
    ListVector::Reserve(result, row_count);
    idx_t total_offset = 0;

    auto tbox_data = FlatVector::GetData<string_t>(tbox_vec);
    auto dur_data = FlatVector::GetData<interval_t>(dur_vec);
    for (idx_t i = 0; i < row_count; ++i) {
        if (FlatVector::IsNull(tbox_vec, i) || FlatVector::IsNull(vsize_vec, i) ||
            FlatVector::IsNull(dur_vec, i) ||
            (vorigin_vec && FlatVector::IsNull(*vorigin_vec, i)) ||
            (torigin_vec && FlatVector::IsNull(*torigin_vec, i))) {
            result_validity.SetInvalid(i);
            continue;
        }
        string_t blob = tbox_data[i];
        TBox *box = (TBox *) malloc(blob.GetSize());
        memcpy(box, blob.GetData(), blob.GetSize());

        MeosInterval iv = IntervaltToInterval(dur_data[i]);
        timestamp_tz_t torigin_meos;
        torigin_meos.value = DEFAULT_TIME_ORIGIN_MEOS;
        if (torigin_vec) {
            timestamp_tz_t torigin_in = FlatVector::GetData<timestamp_tz_t>(*torigin_vec)[i];
            torigin_meos = DuckDBToMeosTimestamp(torigin_in);
        }

        int count = 0;
        TBox *tiles = nullptr;
        if (box->span.spantype == T_INTSPAN) {
            int32_t vsize = FlatVector::GetData<int32_t>(vsize_vec)[i];
            int32_t vorigin = vorigin_vec ? FlatVector::GetData<int32_t>(*vorigin_vec)[i] : 0;
            tiles = tintbox_value_time_tiles(box, vsize, &iv, vorigin,
                                             (TimestampTz) torigin_meos.value, &count);
        } else if (box->span.spantype == T_FLOATSPAN) {
            double vsize = FlatVector::GetData<double>(vsize_vec)[i];
            double vorigin = vorigin_vec ? FlatVector::GetData<double>(*vorigin_vec)[i] : 0.0;
            tiles = tfloatbox_value_time_tiles(box, vsize, &iv, vorigin,
                                               (TimestampTz) torigin_meos.value, &count);
        } else {
            free(box);
            throw InvalidInputException("valueTimeTiles: tbox has no value dimension");
        }
        free(box);
        EmitTboxList(result, i, tiles, count, total_offset, list_entries, child_vector,
                     result_validity);
    }
}

} // namespace

void TemporalTypes::RegisterTileGetters(ExtensionLoader &loader) {
    // Single-bin getters — getBin(value, size, origin) -> <type>span
    loader.RegisterFunction(ScalarFunction(
        "getBin", {LogicalType::INTEGER, LogicalType::INTEGER, LogicalType::INTEGER},
        SpanTypes::intspan(), GetBinIntExec));
    loader.RegisterFunction(ScalarFunction(
        "getBin", {LogicalType::BIGINT, LogicalType::BIGINT, LogicalType::BIGINT},
        SpanTypes::bigintspan(), GetBinBigintExec));
    loader.RegisterFunction(ScalarFunction(
        "getBin", {LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::DOUBLE},
        SpanTypes::floatspan(), GetBinFloatExec));
    loader.RegisterFunction(ScalarFunction(
        "getBin", {LogicalType::TIMESTAMP_TZ, LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ},
        SpanTypes::tstzspan(), GetBinTstzExec));
    loader.RegisterFunction(ScalarFunction(
        "getBin", {LogicalType::DATE, LogicalType::INTERVAL, LogicalType::DATE},
        SpanTypes::datespan(), GetBinDateExec));

    LogicalType list_tbox = LogicalType::LIST(TboxType::tbox());

    // valueTiles(tbox, vsize [, vorigin]) — both INTEGER and DOUBLE size variants
    loader.RegisterFunction(ScalarFunction(
        "valueTiles", {TboxType::tbox(), LogicalType::INTEGER},
        list_tbox, TboxValueTilesExec));
    loader.RegisterFunction(ScalarFunction(
        "valueTiles", {TboxType::tbox(), LogicalType::INTEGER, LogicalType::INTEGER},
        list_tbox, TboxValueTilesExec));
    loader.RegisterFunction(ScalarFunction(
        "valueTiles", {TboxType::tbox(), LogicalType::DOUBLE},
        list_tbox, TboxValueTilesExec));
    loader.RegisterFunction(ScalarFunction(
        "valueTiles", {TboxType::tbox(), LogicalType::DOUBLE, LogicalType::DOUBLE},
        list_tbox, TboxValueTilesExec));

    // timeTiles(tbox, duration [, torigin])
    loader.RegisterFunction(ScalarFunction(
        "timeTiles", {TboxType::tbox(), LogicalType::INTERVAL},
        list_tbox, TboxTimeTilesExec));
    loader.RegisterFunction(ScalarFunction(
        "timeTiles", {TboxType::tbox(), LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ},
        list_tbox, TboxTimeTilesExec));

    // valueTimeTiles(tbox, vsize, duration [, vorigin, torigin])
    loader.RegisterFunction(ScalarFunction(
        "valueTimeTiles", {TboxType::tbox(), LogicalType::INTEGER, LogicalType::INTERVAL},
        list_tbox, TboxValueTimeTilesExec));
    loader.RegisterFunction(ScalarFunction(
        "valueTimeTiles",
        {TboxType::tbox(), LogicalType::INTEGER, LogicalType::INTERVAL,
         LogicalType::INTEGER, LogicalType::TIMESTAMP_TZ},
        list_tbox, TboxValueTimeTilesExec));
    loader.RegisterFunction(ScalarFunction(
        "valueTimeTiles", {TboxType::tbox(), LogicalType::DOUBLE, LogicalType::INTERVAL},
        list_tbox, TboxValueTimeTilesExec));
    loader.RegisterFunction(ScalarFunction(
        "valueTimeTiles",
        {TboxType::tbox(), LogicalType::DOUBLE, LogicalType::INTERVAL,
         LogicalType::DOUBLE, LogicalType::TIMESTAMP_TZ},
        list_tbox, TboxValueTimeTilesExec));
}

// ============================================================
// timeSplit / valueSplit / valueTimeSplit  — table-function impls
// ============================================================

namespace {

struct TemporalSplitGlobalState : public GlobalTableFunctionState {
    idx_t idx = 0;
    vector<Value> time_bins;   // TIMESTAMPTZ (timeSplit / valueTimeSplit)
    vector<Value> value_bins;  // INTEGER or DOUBLE (valueSplit / valueTimeSplit)
    vector<Value> temporals;   // aliased BLOB
};

// ---------- timeSplit ----------

struct TimeSplitBindData : public TableFunctionData {
    string temp_blob;
    interval_t duration;
    timestamp_tz_t torigin;
    bool has_torigin;
    LogicalType ttype;
    string col_name;
};

unique_ptr<FunctionData> TimeSplitBind(ClientContext &, TableFunctionBindInput &input,
                                       vector<LogicalType> &return_types, vector<string> &names) {
    if (input.inputs[0].IsNull())
        throw BinderException("timeSplit: temporal input cannot be null");
    auto bd = make_uniq<TimeSplitBindData>();
    bd->temp_blob = StringValue::Get(input.inputs[0]);
    bd->duration  = input.inputs[1].GetValue<interval_t>();
    bd->has_torigin = (input.inputs.size() >= 3 && !input.inputs[2].IsNull());
    bd->torigin   = bd->has_torigin ? input.inputs[2].GetValue<timestamp_tz_t>() : timestamp_tz_t(0);
    bd->ttype     = input.inputs[0].type();
    bd->col_name  = StringUtil::Lower(bd->ttype.GetAlias());
    return_types  = {LogicalType::TIMESTAMP_TZ, bd->ttype};
    names         = {"time", bd->col_name};
    return std::move(bd);
}

unique_ptr<GlobalTableFunctionState> TimeSplitInit(ClientContext &, TableFunctionInitInput &input) {
    EnsureMeosThreadInitialized();
    auto &bd    = input.bind_data->Cast<TimeSplitBindData>();
    auto  state = make_uniq<TemporalSplitGlobalState>();

    Temporal *t = static_cast<Temporal *>(malloc(bd.temp_blob.size()));
    memcpy(t, bd.temp_blob.data(), bd.temp_blob.size());

    MeosInterval mi = IntervaltToInterval(bd.duration);
    TimestampTz torigin = bd.has_torigin
        ? static_cast<TimestampTz>(DuckDBToMeosTimestamp(bd.torigin).value)
        : TimestampTz(0);

    int count = 0;
    TimestampTz *tbins = nullptr;
    Temporal **parts   = temporal_time_split(t, &mi, torigin, &tbins, &count);
    free(t);

    if (!parts || count <= 0) {
        if (parts) free(parts);
        if (tbins) free(tbins);
        return std::move(state);
    }
    state->time_bins.reserve(count);
    state->temporals.reserve(count);
    for (int i = 0; i < count; i++) {
        timestamp_tz_t ts = MeosToDuckDBTimestamp(timestamp_tz_t(static_cast<int64_t>(tbins[i])));
        state->time_bins.push_back(Value::TIMESTAMPTZ(ts));
        size_t sz     = temporal_mem_size(parts[i]);
        Value  tblob  = Value::BLOB(reinterpret_cast<const_data_ptr_t>(parts[i]), sz);
        tblob.Reinterpret(bd.ttype);
        state->temporals.push_back(std::move(tblob));
        free(parts[i]);
    }
    free(parts);
    free(tbins);
    return std::move(state);
}

void TimeSplitExec(ClientContext &, TableFunctionInput &input, DataChunk &output) {
    auto &state   = input.global_state->Cast<TemporalSplitGlobalState>();
    idx_t remaining = state.temporals.size() - state.idx;
    idx_t emit      = MinValue<idx_t>(STANDARD_VECTOR_SIZE, remaining);
    for (idx_t i = 0; i < emit; i++) {
        output.data[0].SetValue(i, state.time_bins[state.idx]);
        output.data[1].SetValue(i, state.temporals[state.idx]);
        state.idx++;
    }
    output.SetCardinality(emit);
}

// ---------- valueSplit ----------

struct ValueSplitBindData : public TableFunctionData {
    string   temp_blob;
    bool     is_int;     // true → tint + INTEGER size; false → tfloat + DOUBLE size
    Datum    vsize;
    Datum    vorigin;
    LogicalType ttype;
    LogicalType value_type;
};

template <bool IsInt>
unique_ptr<FunctionData> ValueSplitBind(ClientContext &, TableFunctionBindInput &input,
                                        vector<LogicalType> &return_types, vector<string> &names) {
    if (input.inputs[0].IsNull())
        throw BinderException("valueSplit: temporal input cannot be null");
    auto bd = make_uniq<ValueSplitBindData>();
    bd->temp_blob  = StringValue::Get(input.inputs[0]);
    bd->is_int     = IsInt;
    bd->ttype      = input.inputs[0].type();
    if constexpr (IsInt) {
        int32_t sz  = input.inputs[1].GetValue<int32_t>();
        int32_t org = (input.inputs.size() >= 3 && !input.inputs[2].IsNull())
                       ? input.inputs[2].GetValue<int32_t>() : 0;
        bd->vsize   = Int32GetDatum(sz);
        bd->vorigin = Int32GetDatum(org);
        bd->value_type = LogicalType::INTEGER;
        return_types   = {LogicalType::INTEGER, bd->ttype};
        names          = {"value", StringUtil::Lower(bd->ttype.GetAlias())};
    } else {
        double sz  = input.inputs[1].GetValue<double>();
        double org = (input.inputs.size() >= 3 && !input.inputs[2].IsNull())
                      ? input.inputs[2].GetValue<double>() : 0.0;
        bd->vsize   = Float8GetDatum(sz);
        bd->vorigin = Float8GetDatum(org);
        bd->value_type = LogicalType::DOUBLE;
        return_types   = {LogicalType::DOUBLE, bd->ttype};
        names          = {"value", StringUtil::Lower(bd->ttype.GetAlias())};
    }
    return std::move(bd);
}

unique_ptr<GlobalTableFunctionState> ValueSplitInit(ClientContext &, TableFunctionInitInput &input) {
    auto &bd    = input.bind_data->Cast<ValueSplitBindData>();
    auto  state = make_uniq<TemporalSplitGlobalState>();

    Temporal *t = static_cast<Temporal *>(malloc(bd.temp_blob.size()));
    memcpy(t, bd.temp_blob.data(), bd.temp_blob.size());

    int   count  = 0;
    Datum *vbins = nullptr;
    Temporal **parts = tnumber_value_split(t, bd.vsize, bd.vorigin, &vbins, &count);
    free(t);

    if (!parts || count <= 0) {
        if (parts) free(parts);
        if (vbins) free(vbins);
        return std::move(state);
    }
    state->value_bins.reserve(count);
    state->temporals.reserve(count);
    for (int i = 0; i < count; i++) {
        if (bd.is_int)
            state->value_bins.push_back(Value::INTEGER(DatumGetInt32(vbins[i])));
        else
            state->value_bins.push_back(Value::DOUBLE(DatumGetFloat8(vbins[i])));
        size_t sz    = temporal_mem_size(parts[i]);
        Value  tblob = Value::BLOB(reinterpret_cast<const_data_ptr_t>(parts[i]), sz);
        tblob.Reinterpret(bd.ttype);
        state->temporals.push_back(std::move(tblob));
        free(parts[i]);
    }
    free(parts);
    free(vbins);
    return std::move(state);
}

void ValueSplitExec(ClientContext &, TableFunctionInput &input, DataChunk &output) {
    auto &state   = input.global_state->Cast<TemporalSplitGlobalState>();
    idx_t remaining = state.temporals.size() - state.idx;
    idx_t emit      = MinValue<idx_t>(STANDARD_VECTOR_SIZE, remaining);
    for (idx_t i = 0; i < emit; i++) {
        output.data[0].SetValue(i, state.value_bins[state.idx]);
        output.data[1].SetValue(i, state.temporals[state.idx]);
        state.idx++;
    }
    output.SetCardinality(emit);
}

// ---------- valueTimeSplit ----------

struct ValueTimeSplitBindData : public TableFunctionData {
    string   temp_blob;
    bool     is_int;
    Datum    vsize;
    Datum    vorigin;
    interval_t duration;
    timestamp_tz_t torigin;
    bool     has_torigin;
    LogicalType ttype;
};

template <bool IsInt>
unique_ptr<FunctionData> ValueTimeSplitBind(ClientContext &, TableFunctionBindInput &input,
                                            vector<LogicalType> &return_types, vector<string> &names) {
    if (input.inputs[0].IsNull())
        throw BinderException("valueTimeSplit: temporal input cannot be null");
    auto bd = make_uniq<ValueTimeSplitBindData>();
    bd->temp_blob = StringValue::Get(input.inputs[0]);
    bd->is_int    = IsInt;
    bd->duration  = input.inputs[2].GetValue<interval_t>();
    bd->ttype     = input.inputs[0].type();
    if constexpr (IsInt) {
        int32_t sz  = input.inputs[1].GetValue<int32_t>();
        int32_t org = (input.inputs.size() >= 5 && !input.inputs[3].IsNull())
                       ? input.inputs[3].GetValue<int32_t>() : 0;
        bd->vsize   = Int32GetDatum(sz);
        bd->vorigin = Int32GetDatum(org);
    } else {
        double sz  = input.inputs[1].GetValue<double>();
        double org = (input.inputs.size() >= 5 && !input.inputs[3].IsNull())
                      ? input.inputs[3].GetValue<double>() : 0.0;
        bd->vsize   = Float8GetDatum(sz);
        bd->vorigin = Float8GetDatum(org);
    }
    bd->has_torigin = (input.inputs.size() >= 5 && !input.inputs[4].IsNull());
    bd->torigin     = bd->has_torigin ? input.inputs[4].GetValue<timestamp_tz_t>() : timestamp_tz_t(0);
    LogicalType vt  = IsInt ? LogicalType::INTEGER : LogicalType::DOUBLE;
    return_types    = {vt, LogicalType::TIMESTAMP_TZ, bd->ttype};
    names           = {"value", "time", StringUtil::Lower(bd->ttype.GetAlias())};
    return std::move(bd);
}

unique_ptr<GlobalTableFunctionState> ValueTimeSplitInit(ClientContext &, TableFunctionInitInput &input) {
    EnsureMeosThreadInitialized();
    auto &bd    = input.bind_data->Cast<ValueTimeSplitBindData>();
    auto  state = make_uniq<TemporalSplitGlobalState>();

    Temporal *t = static_cast<Temporal *>(malloc(bd.temp_blob.size()));
    memcpy(t, bd.temp_blob.data(), bd.temp_blob.size());

    MeosInterval mi = IntervaltToInterval(bd.duration);
    TimestampTz torigin = bd.has_torigin
        ? static_cast<TimestampTz>(DuckDBToMeosTimestamp(bd.torigin).value)
        : TimestampTz(0);

    int count     = 0;
    Datum       *vbins = nullptr;
    TimestampTz *tbins = nullptr;
    Temporal **parts   = tnumber_value_time_split(t, bd.vsize, &mi, bd.vorigin, torigin,
                                                   &vbins, &tbins, &count);
    free(t);

    if (!parts || count <= 0) {
        if (parts) free(parts);
        if (vbins) free(vbins);
        if (tbins) free(tbins);
        return std::move(state);
    }
    state->value_bins.reserve(count);
    state->time_bins.reserve(count);
    state->temporals.reserve(count);
    for (int i = 0; i < count; i++) {
        if (bd.is_int)
            state->value_bins.push_back(Value::INTEGER(DatumGetInt32(vbins[i])));
        else
            state->value_bins.push_back(Value::DOUBLE(DatumGetFloat8(vbins[i])));
        timestamp_tz_t ts = MeosToDuckDBTimestamp(timestamp_tz_t(static_cast<int64_t>(tbins[i])));
        state->time_bins.push_back(Value::TIMESTAMPTZ(ts));
        size_t sz    = temporal_mem_size(parts[i]);
        Value  tblob = Value::BLOB(reinterpret_cast<const_data_ptr_t>(parts[i]), sz);
        tblob.Reinterpret(bd.ttype);
        state->temporals.push_back(std::move(tblob));
        free(parts[i]);
    }
    free(parts);
    free(vbins);
    free(tbins);
    return std::move(state);
}

void ValueTimeSplitExec(ClientContext &, TableFunctionInput &input, DataChunk &output) {
    auto &state   = input.global_state->Cast<TemporalSplitGlobalState>();
    idx_t remaining = state.temporals.size() - state.idx;
    idx_t emit      = MinValue<idx_t>(STANDARD_VECTOR_SIZE, remaining);
    for (idx_t i = 0; i < emit; i++) {
        output.data[0].SetValue(i, state.value_bins[state.idx]);
        output.data[1].SetValue(i, state.time_bins[state.idx]);
        output.data[2].SetValue(i, state.temporals[state.idx]);
        state.idx++;
    }
    output.SetCardinality(emit);
}

} // anonymous namespace

void TemporalTypes::RegisterTemporalTileSplit(ExtensionLoader &loader) {
    const auto I  = LogicalType::INTEGER;
    const auto D  = LogicalType::DOUBLE;
    const auto IV = LogicalType::INTERVAL;
    const auto TS = LogicalType::TIMESTAMP_TZ;

    // timeSplit(temporal, interval [, timestamptz])
    for (const auto &ttype : AllTypes()) {
        loader.RegisterFunction(TableFunction(
            "timeSplit", {ttype, IV}, TimeSplitExec, TimeSplitBind, TimeSplitInit));
        loader.RegisterFunction(TableFunction(
            "timeSplit", {ttype, IV, TS}, TimeSplitExec, TimeSplitBind, TimeSplitInit));
    }
    // also for tgeompoint and tgeometry
    for (const auto &ttype : {TgeompointType::tgeompoint()}) {
        loader.RegisterFunction(TableFunction(
            "timeSplit", {ttype, IV}, TimeSplitExec, TimeSplitBind, TimeSplitInit));
        loader.RegisterFunction(TableFunction(
            "timeSplit", {ttype, IV, TS}, TimeSplitExec, TimeSplitBind, TimeSplitInit));
    }

    // valueSplit is registered separately via RegisterTnumberValueSplit

    // valueTimeSplit(tint, integer, interval [, integer, timestamptz])
    loader.RegisterFunction(TableFunction(
        "valueTimeSplit", {tint(), I, IV},
        ValueTimeSplitExec, ValueTimeSplitBind<true>, ValueTimeSplitInit));
    loader.RegisterFunction(TableFunction(
        "valueTimeSplit", {tint(), I, IV, I, TS},
        ValueTimeSplitExec, ValueTimeSplitBind<true>, ValueTimeSplitInit));
    // valueTimeSplit(tfloat, double, interval [, double, timestamptz])
    loader.RegisterFunction(TableFunction(
        "valueTimeSplit", {tfloat(), D, IV},
        ValueTimeSplitExec, ValueTimeSplitBind<false>, ValueTimeSplitInit));
    loader.RegisterFunction(TableFunction(
        "valueTimeSplit", {tfloat(), D, IV, D, TS},
        ValueTimeSplitExec, ValueTimeSplitBind<false>, ValueTimeSplitInit));
}

/* ***************************************************
 * valueSplit(tint|tfloat, size, origin) → SETOF (number, tnumber)
 * ---------------------------------------------------
 * Wraps MEOS tint_value_split / tfloat_value_split. Each row is a (bin-start
 * value, sub-temporal) pair where the sub-temporal is the slice of the input
 * whose value fell into that bin.
 ****************************************************/

struct TnumberValueSplitBindData : public TableFunctionData {
    string   blob;
    MeosType temptype;
    LogicalType base_type;     // BIGINT for tint, DOUBLE for tfloat
    LogicalType temporal_type; // tint or tfloat
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
    if (alias == "tint") {
        bind->base_type = LogicalType::BIGINT;
        bind->temporal_type = TemporalTypes::tint();
    } else if (alias == "tfloat") {
        bind->base_type = LogicalType::DOUBLE;
        bind->temporal_type = TemporalTypes::tfloat();
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
    EnsureMeosThreadInitialized();
    auto &bind = input.bind_data->Cast<TnumberValueSplitBindData>();
    auto state = make_uniq<TnumberValueSplitGlobalState>();

    const uint8_t *data = (const uint8_t *)bind.blob.data();
    size_t size = bind.blob.size();
    Temporal *temp = (Temporal *)malloc(size);
    memcpy(temp, data, size);

    auto make_slice_value = [&](Temporal *slice) {
        size_t slice_size = temporal_mem_size(slice);
        uint8_t *slice_buf = (uint8_t *)malloc(slice_size);
        memcpy(slice_buf, slice, slice_size);
        Value slice_blob = Value::BLOB(slice_buf, slice_size);
        // Carry the BLOB bytes through under the tint/tfloat alias. There's no
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


void TemporalTypes::RegisterTnumberValueSplit(ExtensionLoader &loader) {
    // tint variant
    {
        TableFunction fn("valueSplit",
                         {TemporalTypes::tint(), LogicalType::INTEGER},
                         TnumberValueSplitExec, TnumberValueSplitBind, TnumberValueSplitInit);
        loader.RegisterFunction(fn);
    }
    {
        TableFunction fn("valueSplit",
                         {TemporalTypes::tint(), LogicalType::INTEGER, LogicalType::INTEGER},
                         TnumberValueSplitExec, TnumberValueSplitBind, TnumberValueSplitInit);
        loader.RegisterFunction(fn);
    }
    // tfloat variant
    {
        TableFunction fn("valueSplit",
                         {TemporalTypes::tfloat(), LogicalType::DOUBLE},
                         TnumberValueSplitExec, TnumberValueSplitBind, TnumberValueSplitInit);
        loader.RegisterFunction(fn);
    }
    {
        TableFunction fn("valueSplit",
                         {TemporalTypes::tfloat(), LogicalType::DOUBLE, LogicalType::DOUBLE},
                         TnumberValueSplitExec, TnumberValueSplitBind, TnumberValueSplitInit);
        loader.RegisterFunction(fn);
    }
}

} // namespace duckdb
