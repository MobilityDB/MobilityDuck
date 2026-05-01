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
#include "geo/tgeography.hpp"
#include "geo/tgeogpoint.hpp"

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

        // sequenceN(temporal, int) — returns the n-th sequence as a temporal.
        // Already wired for the four spatial-temporal types via their own
        // _ops files; here we extend coverage to tint / tfloat / tbool / ttext.
        loader.RegisterFunction(
            ScalarFunction(
                "sequenceN",
                {type, LogicalType::INTEGER},
                type,
                TemporalFunctions::Temporal_sequence_n
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

        // SeqSetGaps — partition a list of TInstant values into
        // sequences with optional maxt / maxdist gap thresholds and a
        // sticky interp keyword. tbool / ttext only carry the
        // (list, maxt) shape (no metric, no interp); tint / tfloat
        // get the full 4-arg surface.
        const std::string seqset_gaps_name =
            StringUtil::Lower(type.GetAlias()) + "SeqSetGaps";
        loader.RegisterFunction(ScalarFunction(seqset_gaps_name,
            {LogicalType::LIST(type)}, type,
            TemporalFunctions::Tsequenceset_constructor_gaps));
        loader.RegisterFunction(ScalarFunction(seqset_gaps_name,
            {LogicalType::LIST(type), LogicalType::INTERVAL}, type,
            TemporalFunctions::Tsequenceset_constructor_gaps));
        if (type.GetAlias() == "TINT" || type.GetAlias() == "TFLOAT") {
            loader.RegisterFunction(ScalarFunction(seqset_gaps_name,
                {LogicalType::LIST(type), LogicalType::INTERVAL, LogicalType::DOUBLE},
                type, TemporalFunctions::Tsequenceset_constructor_gaps));
            loader.RegisterFunction(ScalarFunction(seqset_gaps_name,
                {LogicalType::LIST(type), LogicalType::INTERVAL, LogicalType::DOUBLE,
                 LogicalType::VARCHAR},
                type, TemporalFunctions::Tsequenceset_constructor_gaps));
        }

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
    loader.RegisterFunction(ScalarFunction("deltaValue", {TemporalTypes::TINT()},   TemporalTypes::TINT(),   TemporalFunctions::Tnumber_delta_value));
    loader.RegisterFunction(ScalarFunction("deltaValue", {TemporalTypes::TFLOAT()}, TemporalTypes::TFLOAT(), TemporalFunctions::Tnumber_delta_value));

    // -----------------------------------------------------------------
    // MFJSON / Hex(E)WKB I/O — `asMFJSON`, `asHexWKB`, plus the
    // type-specific `tIntFromMFJSON` / etc. parse constructors. The
    // spatial-temporal types also expose `asHexEWKB` as an alias for
    // asHexWKB; that registration is in the per-type _ops file.
    // -----------------------------------------------------------------
    {
        using ScalarFn = void (*)(DataChunk &, ExpressionState &, Vector &);
        auto register_mfjson_io = [&](const LogicalType &type, ScalarFn from_fn) {
            // asMFJSON — 1/2/3-arg shapes mirroring MobilityDB SQL.
            loader.RegisterFunction(ScalarFunction("asMFJSON",
                {type}, LogicalType::VARCHAR, TemporalFunctions::Temporal_as_mfjson));
            loader.RegisterFunction(ScalarFunction("asMFJSON",
                {type, LogicalType::BOOLEAN}, LogicalType::VARCHAR,
                TemporalFunctions::Temporal_as_mfjson));
            loader.RegisterFunction(ScalarFunction("asMFJSON",
                {type, LogicalType::BOOLEAN, LogicalType::INTEGER},
                LogicalType::VARCHAR, TemporalFunctions::Temporal_as_mfjson));

            // asHexWKB — single-argument scalar form.
            loader.RegisterFunction(ScalarFunction("asHexWKB",
                {type}, LogicalType::VARCHAR, TemporalFunctions::Temporal_as_hexwkb));

            // typeFromMFJSON parser — type-specific because temptype
            // has to be picked at registration time.
            loader.RegisterFunction(ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "FromMFJSON",
                {LogicalType::VARCHAR}, type, from_fn));
        };
        register_mfjson_io(TemporalTypes::TINT(),    TemporalFunctions::Tint_from_mfjson);
        register_mfjson_io(TemporalTypes::TFLOAT(),  TemporalFunctions::Tfloat_from_mfjson);
        register_mfjson_io(TemporalTypes::TBOOL(),   TemporalFunctions::Tbool_from_mfjson);
        register_mfjson_io(TemporalTypes::TTEXT(),   TemporalFunctions::Ttext_from_mfjson);
    }
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

    // Ever / always equality and inequality (named functions; DuckDB
    // parser does not accept ?= / #= operator names).
#define REG_EA(NAME, FN)                                                                                                                                                          \
    loader.RegisterFunction(ScalarFunction(NAME, {LogicalType::BOOLEAN,        TemporalTypes::TBOOL()},   LogicalType::BOOLEAN, TemporalFunctions::FN##_bool_tbool));             \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TBOOL(),      LogicalType::BOOLEAN},     LogicalType::BOOLEAN, TemporalFunctions::FN##_tbool_bool));             \
    loader.RegisterFunction(ScalarFunction(NAME, {LogicalType::INTEGER,        TemporalTypes::TINT()},    LogicalType::BOOLEAN, TemporalFunctions::FN##_int_tint));               \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TINT(),       LogicalType::INTEGER},     LogicalType::BOOLEAN, TemporalFunctions::FN##_tint_int));               \
    loader.RegisterFunction(ScalarFunction(NAME, {LogicalType::DOUBLE,         TemporalTypes::TFLOAT()},  LogicalType::BOOLEAN, TemporalFunctions::FN##_float_tfloat));           \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TFLOAT(),     LogicalType::DOUBLE},      LogicalType::BOOLEAN, TemporalFunctions::FN##_tfloat_float));           \
    loader.RegisterFunction(ScalarFunction(NAME, {LogicalType::VARCHAR,        TemporalTypes::TTEXT()},   LogicalType::BOOLEAN, TemporalFunctions::FN##_text_ttext));             \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TTEXT(),      LogicalType::VARCHAR},     LogicalType::BOOLEAN, TemporalFunctions::FN##_ttext_text));             \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TINT(),       TemporalTypes::TINT()},    LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal));      \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TFLOAT(),     TemporalTypes::TFLOAT()},  LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal));      \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TBOOL(),      TemporalTypes::TBOOL()},   LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal));      \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TTEXT(),      TemporalTypes::TTEXT()},   LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal));

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
    loader.RegisterFunction(ScalarFunction(NAME, {LogicalType::VARCHAR,    TemporalTypes::TTEXT()},  LogicalType::BOOLEAN, TemporalFunctions::FN##_text_ttext));      \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TTEXT(),  LogicalType::VARCHAR},    LogicalType::BOOLEAN, TemporalFunctions::FN##_ttext_text));      \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TINT(),   TemporalTypes::TINT()},   LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal)); \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TFLOAT(), TemporalTypes::TFLOAT()}, LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal)); \
    loader.RegisterFunction(ScalarFunction(NAME, {TemporalTypes::TTEXT(),  TemporalTypes::TTEXT()},  LogicalType::BOOLEAN, TemporalFunctions::FN##_temporal_temporal));

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
        loader.RegisterFunction(ScalarFunction("frechetDistance", {t, t}, LogicalType::DOUBLE, TemporalFunctions::Temporal_frechet_distance));
        loader.RegisterFunction(ScalarFunction("discreteFrechet", {t, t}, LogicalType::DOUBLE, TemporalFunctions::Temporal_frechet_distance));
        loader.RegisterFunction(ScalarFunction("dynTimeWarp",     {t, t}, LogicalType::DOUBLE, TemporalFunctions::Temporal_dyntimewarp_distance));
        // MobilityDB SQL exposes the same function under the longer
        // `dynTimeWarpDistance` name; register both for compatibility.
        loader.RegisterFunction(ScalarFunction("dynTimeWarpDistance", {t, t}, LogicalType::DOUBLE, TemporalFunctions::Temporal_dyntimewarp_distance));
        loader.RegisterFunction(ScalarFunction("hausdorffDistance", {t, t}, LogicalType::DOUBLE, TemporalFunctions::Temporal_hausdorff_distance));
    }

    // Temporal simplification — Douglas-Peucker, max-dist, min-dist,
    // min-time-delta. MobilityDB SQL exposes these for tfloat plus the
    // four spatial-temporal types; the MEOS C functions are subtype-
    // agnostic, so the registration is just per-type plumbing.
    {
        const std::vector<LogicalType> simplify_types = {
            TemporalTypes::TFLOAT(),
            TgeompointType::TGEOMPOINT(),
            TGeometryTypes::TGEOMETRY(),
            TGeographyTypes::TGEOGRAPHY(),
            TGeogpointType::TGEOGPOINT(),
        };
        for (const auto &t : simplify_types) {
            loader.RegisterFunction(ScalarFunction("douglasPeuckerSimplify",
                {t, LogicalType::DOUBLE}, t, TemporalFunctions::Temporal_simplify_dp));
            loader.RegisterFunction(ScalarFunction("douglasPeuckerSimplify",
                {t, LogicalType::DOUBLE, LogicalType::BOOLEAN}, t,
                TemporalFunctions::Temporal_simplify_dp));
            loader.RegisterFunction(ScalarFunction("maxDistSimplify",
                {t, LogicalType::DOUBLE}, t, TemporalFunctions::Temporal_simplify_max_dist));
            loader.RegisterFunction(ScalarFunction("maxDistSimplify",
                {t, LogicalType::DOUBLE, LogicalType::BOOLEAN}, t,
                TemporalFunctions::Temporal_simplify_max_dist));
            loader.RegisterFunction(ScalarFunction("minDistSimplify",
                {t, LogicalType::DOUBLE}, t, TemporalFunctions::Temporal_simplify_min_dist));
            loader.RegisterFunction(ScalarFunction("minTimeDeltaSimplify",
                {t, LogicalType::INTERVAL}, t,
                TemporalFunctions::Temporal_simplify_min_tdelta));
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

        // Above/below/front/back as named functions (DuckDB parser rejects |>>, <<|, etc.)
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

// ----- span list emitter (LIST(SPAN) outputs for *bins / timeBins) -----

inline void EmitSpanList(Vector &result, idx_t row_idx, Span *bins, int count,
                         idx_t &total_offset, list_entry_t *list_entries,
                         Vector &child_vector, ValidityMask &result_validity) {
    if (!bins || count <= 0) {
        if (bins) free(bins);
        result_validity.SetInvalid(row_idx);
        return;
    }
    ListVector::SetListSize(result, total_offset + count);
    list_entries[row_idx] = list_entry_t{total_offset, static_cast<uint64_t>(count)};
    auto *child_data = FlatVector::GetData<string_t>(child_vector);
    const size_t span_bytes = sizeof(Span);
    for (int j = 0; j < count; ++j) {
        child_data[total_offset + j] = StringVector::AddStringOrBlob(
            child_vector, reinterpret_cast<const char *>(&bins[j]), span_bytes);
    }
    free(bins);
    total_offset += count;
}

// Datum shims local to this translation unit. Kept narrow on purpose —
// when cluster G expands to splitN* and tnumber_value_time_split, the
// Wasm-portability variant of Float8ToDatum from temporal_aggregates.cpp
// should be extracted into a shared header.
inline Datum SpanBinInt32ToDatum(int32_t v)  { return (Datum) (int64_t) v; }
inline Datum SpanBinInt64ToDatum(int64_t v)  { return (Datum) v; }
inline Datum SpanBinFloat8ToDatum(double v)  {
    if (sizeof(Datum) >= sizeof(double)) {
        Datum d = 0;
        memcpy(&d, &v, sizeof(double));
        return d;
    }
    double *p = (double *) malloc(sizeof(double));
    *p = v;
    return (Datum) (uintptr_t) p;
}

// Generic LIST(SPAN) execs over span_bins / tnumber_value_bins. The
// span_bins MEOS entry takes Datum arguments; the input span itself
// encodes the basetype, so the same C call works for int / bigint /
// float / date span families with the right Datum wrappers.

template <Datum (*TO_DATUM)(int32_t)>
void SpanBinsIntExec(DataChunk &args, ExpressionState &, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t c = 0; c < args.ColumnCount(); ++c) args.data[c].Flatten(row_count);
    auto &result_validity = FlatVector::Validity(result);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &child_vector = ListVector::GetEntry(result);
    child_vector.SetVectorType(VectorType::FLAT_VECTOR);
    ListVector::Reserve(result, row_count);
    idx_t total_offset = 0;

    auto span_data = FlatVector::GetData<string_t>(args.data[0]);
    auto size_data = FlatVector::GetData<int32_t>(args.data[1]);
    auto origin_data = FlatVector::GetData<int32_t>(args.data[2]);
    for (idx_t i = 0; i < row_count; ++i) {
        if (FlatVector::IsNull(args.data[0], i) || FlatVector::IsNull(args.data[1], i) ||
            FlatVector::IsNull(args.data[2], i)) {
            result_validity.SetInvalid(i);
            continue;
        }
        Span s;
        memcpy(&s, span_data[i].GetData(), sizeof(Span));
        int count = 0;
        Span *bins = span_bins(&s, TO_DATUM(size_data[i]), TO_DATUM(origin_data[i]), &count);
        EmitSpanList(result, i, bins, count, total_offset, list_entries, child_vector,
                     result_validity);
    }
}

void SpanBinsBigintExec(DataChunk &args, ExpressionState &, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t c = 0; c < args.ColumnCount(); ++c) args.data[c].Flatten(row_count);
    auto &result_validity = FlatVector::Validity(result);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &child_vector = ListVector::GetEntry(result);
    child_vector.SetVectorType(VectorType::FLAT_VECTOR);
    ListVector::Reserve(result, row_count);
    idx_t total_offset = 0;

    auto span_data = FlatVector::GetData<string_t>(args.data[0]);
    auto size_data = FlatVector::GetData<int64_t>(args.data[1]);
    auto origin_data = FlatVector::GetData<int64_t>(args.data[2]);
    for (idx_t i = 0; i < row_count; ++i) {
        if (FlatVector::IsNull(args.data[0], i) || FlatVector::IsNull(args.data[1], i) ||
            FlatVector::IsNull(args.data[2], i)) {
            result_validity.SetInvalid(i);
            continue;
        }
        Span s;
        memcpy(&s, span_data[i].GetData(), sizeof(Span));
        int count = 0;
        Span *bins = span_bins(&s, SpanBinInt64ToDatum(size_data[i]),
                               SpanBinInt64ToDatum(origin_data[i]), &count);
        EmitSpanList(result, i, bins, count, total_offset, list_entries, child_vector,
                     result_validity);
    }
}

void SpanBinsFloatExec(DataChunk &args, ExpressionState &, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t c = 0; c < args.ColumnCount(); ++c) args.data[c].Flatten(row_count);
    auto &result_validity = FlatVector::Validity(result);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &child_vector = ListVector::GetEntry(result);
    child_vector.SetVectorType(VectorType::FLAT_VECTOR);
    ListVector::Reserve(result, row_count);
    idx_t total_offset = 0;

    auto span_data = FlatVector::GetData<string_t>(args.data[0]);
    auto size_data = FlatVector::GetData<double>(args.data[1]);
    auto origin_data = FlatVector::GetData<double>(args.data[2]);
    for (idx_t i = 0; i < row_count; ++i) {
        if (FlatVector::IsNull(args.data[0], i) || FlatVector::IsNull(args.data[1], i) ||
            FlatVector::IsNull(args.data[2], i)) {
            result_validity.SetInvalid(i);
            continue;
        }
        Span s;
        memcpy(&s, span_data[i].GetData(), sizeof(Span));
        int count = 0;
        Span *bins = span_bins(&s, SpanBinFloat8ToDatum(size_data[i]),
                               SpanBinFloat8ToDatum(origin_data[i]), &count);
        EmitSpanList(result, i, bins, count, total_offset, list_entries, child_vector,
                     result_validity);
    }
}

// bins(tstzspan, interval, timestamptz) → LIST(tstzspan).
void SpanBinsTstzExec(DataChunk &args, ExpressionState &, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t c = 0; c < args.ColumnCount(); ++c) args.data[c].Flatten(row_count);
    auto &result_validity = FlatVector::Validity(result);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &child_vector = ListVector::GetEntry(result);
    child_vector.SetVectorType(VectorType::FLAT_VECTOR);
    ListVector::Reserve(result, row_count);
    idx_t total_offset = 0;

    auto span_data = FlatVector::GetData<string_t>(args.data[0]);
    auto iv_data = FlatVector::GetData<interval_t>(args.data[1]);
    auto torigin_data = FlatVector::GetData<timestamp_tz_t>(args.data[2]);
    for (idx_t i = 0; i < row_count; ++i) {
        if (FlatVector::IsNull(args.data[0], i) || FlatVector::IsNull(args.data[1], i) ||
            FlatVector::IsNull(args.data[2], i)) {
            result_validity.SetInvalid(i);
            continue;
        }
        Span s;
        memcpy(&s, span_data[i].GetData(), sizeof(Span));
        MeosInterval iv = IntervaltToInterval(iv_data[i]);
        timestamp_tz_t tor = DuckDBToMeosTimestamp(torigin_data[i]);
        // span_bins for tstzspan expects size as Datum-encoded Interval pointer
        // and origin as timestamptz Datum.
        int count = 0;
        Span *bins = span_bins(&s, (Datum)(uintptr_t)&iv, (Datum)(int64_t)tor.value, &count);
        EmitSpanList(result, i, bins, count, total_offset, list_entries, child_vector,
                     result_validity);
    }
}

// timeBins(temporal, interval, timestamptz) → LIST(tstzspan).
void TemporalTimeBinsExec(DataChunk &args, ExpressionState &, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t c = 0; c < args.ColumnCount(); ++c) args.data[c].Flatten(row_count);
    auto &result_validity = FlatVector::Validity(result);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &child_vector = ListVector::GetEntry(result);
    child_vector.SetVectorType(VectorType::FLAT_VECTOR);
    ListVector::Reserve(result, row_count);
    idx_t total_offset = 0;

    auto temp_data = FlatVector::GetData<string_t>(args.data[0]);
    auto iv_data = FlatVector::GetData<interval_t>(args.data[1]);
    auto torigin_data = FlatVector::GetData<timestamp_tz_t>(args.data[2]);
    for (idx_t i = 0; i < row_count; ++i) {
        if (FlatVector::IsNull(args.data[0], i) || FlatVector::IsNull(args.data[1], i) ||
            FlatVector::IsNull(args.data[2], i)) {
            result_validity.SetInvalid(i);
            continue;
        }
        Temporal *t = (Temporal *) malloc(temp_data[i].GetSize());
        memcpy(t, temp_data[i].GetData(), temp_data[i].GetSize());
        MeosInterval iv = IntervaltToInterval(iv_data[i]);
        timestamp_tz_t tor = DuckDBToMeosTimestamp(torigin_data[i]);
        int count = 0;
        Span *bins = temporal_time_bins(t, &iv, (TimestampTz) tor.value, &count);
        free(t);
        EmitSpanList(result, i, bins, count, total_offset, list_entries, child_vector,
                     result_validity);
    }
}

// valueBins(tnumber, size, origin) → LIST(numspan). Dispatches on the
// declared input type (tint vs tfloat) at registration time so the
// Datum encoding is correct.
template <bool IS_INT>
void TnumberValueBinsExec(DataChunk &args, ExpressionState &, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t c = 0; c < args.ColumnCount(); ++c) args.data[c].Flatten(row_count);
    auto &result_validity = FlatVector::Validity(result);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &child_vector = ListVector::GetEntry(result);
    child_vector.SetVectorType(VectorType::FLAT_VECTOR);
    ListVector::Reserve(result, row_count);
    idx_t total_offset = 0;

    auto temp_data = FlatVector::GetData<string_t>(args.data[0]);
    for (idx_t i = 0; i < row_count; ++i) {
        if (FlatVector::IsNull(args.data[0], i) || FlatVector::IsNull(args.data[1], i) ||
            FlatVector::IsNull(args.data[2], i)) {
            result_validity.SetInvalid(i);
            continue;
        }
        Temporal *t = (Temporal *) malloc(temp_data[i].GetSize());
        memcpy(t, temp_data[i].GetData(), temp_data[i].GetSize());
        Datum size_d, origin_d;
        if (IS_INT) {
            int32_t s = FlatVector::GetData<int32_t>(args.data[1])[i];
            int32_t o = FlatVector::GetData<int32_t>(args.data[2])[i];
            size_d = SpanBinInt32ToDatum(s);
            origin_d = SpanBinInt32ToDatum(o);
        } else {
            double s = FlatVector::GetData<double>(args.data[1])[i];
            double o = FlatVector::GetData<double>(args.data[2])[i];
            size_d = SpanBinFloat8ToDatum(s);
            origin_d = SpanBinFloat8ToDatum(o);
        }
        int count = 0;
        Span *bins = tnumber_value_bins(t, size_d, origin_d, &count);
        free(t);
        EmitSpanList(result, i, bins, count, total_offset, list_entries, child_vector,
                     result_validity);
    }
}

// ----- tbox tile emitters: LIST(TBOX) outputs -----

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
// frechetDistancePath / dynTimeWarpPath — emit
// `LIST<STRUCT(i INTEGER, j INTEGER)>`. MobilityDB's PG surface uses
// `SETOF warp` (table-valued); DuckDB scalar funcs return scalars, so
// we wrap the list and let the user `unnest()` if they want rows.
template <Match *(*FN)(const Temporal *, const Temporal *, int *)>
void TemporalPathExec(DataChunk &args, ExpressionState &, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t c = 0; c < args.ColumnCount(); ++c) args.data[c].Flatten(row_count);
    auto &result_validity = FlatVector::Validity(result);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &child_vector = ListVector::GetEntry(result);
    child_vector.SetVectorType(VectorType::FLAT_VECTOR);
    ListVector::Reserve(result, row_count);
    idx_t total_offset = 0;

    auto a_data = FlatVector::GetData<string_t>(args.data[0]);
    auto b_data = FlatVector::GetData<string_t>(args.data[1]);
    auto &child_entries = StructVector::GetEntries(child_vector);
    auto i_data = FlatVector::GetData<int32_t>(*child_entries[0]);
    auto j_data = FlatVector::GetData<int32_t>(*child_entries[1]);
    for (idx_t i = 0; i < row_count; ++i) {
        if (FlatVector::IsNull(args.data[0], i) || FlatVector::IsNull(args.data[1], i)) {
            result_validity.SetInvalid(i);
            continue;
        }
        Temporal *a = (Temporal *) malloc(a_data[i].GetSize());
        memcpy(a, a_data[i].GetData(), a_data[i].GetSize());
        Temporal *b = (Temporal *) malloc(b_data[i].GetSize());
        memcpy(b, b_data[i].GetData(), b_data[i].GetSize());
        int count = 0;
        Match *path = FN(a, b, &count);
        free(a); free(b);
        if (!path || count <= 0) {
            if (path) free(path);
            result_validity.SetInvalid(i);
            continue;
        }
        ListVector::SetListSize(result, total_offset + count);
        list_entries[i] = list_entry_t{total_offset, static_cast<uint64_t>(count)};
        // Re-fetch child data pointers — SetListSize / Reserve may
        // re-allocate the child vector, invalidating earlier ones.
        i_data = FlatVector::GetData<int32_t>(*child_entries[0]);
        j_data = FlatVector::GetData<int32_t>(*child_entries[1]);
        for (int k = 0; k < count; ++k) {
            i_data[total_offset + k] = path[k].i;
            j_data[total_offset + k] = path[k].j;
        }
        free(path);
        total_offset += count;
    }
}

// splitNTboxes(tnumber, int) — partitions a tnumber into the given
// box-count and returns the bounding tbox of each partition.
// splitEachNTboxes(tnumber, int) — same shape but partition by
// element-count instead of total-box-count. Both wrap the
// subtype-agnostic `tnumber_*_tboxes` MEOS exports.
template <TBox *(*FN)(const Temporal *, int, int *)>
void TnumberSplitTboxesExec(DataChunk &args, ExpressionState &, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t c = 0; c < args.ColumnCount(); ++c) args.data[c].Flatten(row_count);
    auto &result_validity = FlatVector::Validity(result);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &child_vector = ListVector::GetEntry(result);
    child_vector.SetVectorType(VectorType::FLAT_VECTOR);
    ListVector::Reserve(result, row_count);
    idx_t total_offset = 0;

    auto temp_data = FlatVector::GetData<string_t>(args.data[0]);
    auto n_data = FlatVector::GetData<int32_t>(args.data[1]);
    for (idx_t i = 0; i < row_count; ++i) {
        if (FlatVector::IsNull(args.data[0], i) || FlatVector::IsNull(args.data[1], i)) {
            result_validity.SetInvalid(i);
            continue;
        }
        Temporal *t = (Temporal *) malloc(temp_data[i].GetSize());
        memcpy(t, temp_data[i].GetData(), temp_data[i].GetSize());
        int count = 0;
        TBox *tiles = FN(t, n_data[i], &count);
        free(t);
        EmitTboxList(result, i, tiles, count, total_offset, list_entries, child_vector,
                     result_validity);
    }
}

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
        SpanTypes::INTSPAN(), GetBinIntExec));
    loader.RegisterFunction(ScalarFunction(
        "getBin", {LogicalType::BIGINT, LogicalType::BIGINT, LogicalType::BIGINT},
        SpanTypes::BIGINTSPAN(), GetBinBigintExec));
    loader.RegisterFunction(ScalarFunction(
        "getBin", {LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::DOUBLE},
        SpanTypes::FLOATSPAN(), GetBinFloatExec));
    loader.RegisterFunction(ScalarFunction(
        "getBin", {LogicalType::TIMESTAMP_TZ, LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ},
        SpanTypes::TSTZSPAN(), GetBinTstzExec));
    loader.RegisterFunction(ScalarFunction(
        "getBin", {LogicalType::DATE, LogicalType::INTERVAL, LogicalType::DATE},
        SpanTypes::DATESPAN(), GetBinDateExec));

    LogicalType list_tbox = LogicalType::LIST(TboxType::TBOX());

    // valueTiles(tbox, vsize [, vorigin]) — both INTEGER and DOUBLE size variants
    loader.RegisterFunction(ScalarFunction(
        "valueTiles", {TboxType::TBOX(), LogicalType::INTEGER},
        list_tbox, TboxValueTilesExec));
    loader.RegisterFunction(ScalarFunction(
        "valueTiles", {TboxType::TBOX(), LogicalType::INTEGER, LogicalType::INTEGER},
        list_tbox, TboxValueTilesExec));
    loader.RegisterFunction(ScalarFunction(
        "valueTiles", {TboxType::TBOX(), LogicalType::DOUBLE},
        list_tbox, TboxValueTilesExec));
    loader.RegisterFunction(ScalarFunction(
        "valueTiles", {TboxType::TBOX(), LogicalType::DOUBLE, LogicalType::DOUBLE},
        list_tbox, TboxValueTilesExec));

    // timeTiles(tbox, duration [, torigin])
    loader.RegisterFunction(ScalarFunction(
        "timeTiles", {TboxType::TBOX(), LogicalType::INTERVAL},
        list_tbox, TboxTimeTilesExec));
    loader.RegisterFunction(ScalarFunction(
        "timeTiles", {TboxType::TBOX(), LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ},
        list_tbox, TboxTimeTilesExec));

    // valueTimeTiles(tbox, vsize, duration [, vorigin, torigin])
    loader.RegisterFunction(ScalarFunction(
        "valueTimeTiles", {TboxType::TBOX(), LogicalType::INTEGER, LogicalType::INTERVAL},
        list_tbox, TboxValueTimeTilesExec));
    loader.RegisterFunction(ScalarFunction(
        "valueTimeTiles",
        {TboxType::TBOX(), LogicalType::INTEGER, LogicalType::INTERVAL,
         LogicalType::INTEGER, LogicalType::TIMESTAMP_TZ},
        list_tbox, TboxValueTimeTilesExec));
    loader.RegisterFunction(ScalarFunction(
        "valueTimeTiles", {TboxType::TBOX(), LogicalType::DOUBLE, LogicalType::INTERVAL},
        list_tbox, TboxValueTimeTilesExec));
    loader.RegisterFunction(ScalarFunction(
        "valueTimeTiles",
        {TboxType::TBOX(), LogicalType::DOUBLE, LogicalType::INTERVAL,
         LogicalType::DOUBLE, LogicalType::TIMESTAMP_TZ},
        list_tbox, TboxValueTimeTilesExec));

    // -----------------------------------------------------------------
    // bins(span, size, origin) — list-of-spans emitters covering all
    // five span families. timeBins(temporal, interval, ts) and
    // valueBins(tnumber, size, origin) are the temporal-value
    // counterparts.
    // -----------------------------------------------------------------
    {
        const auto INT  = LogicalType::INTEGER;
        const auto BIG  = LogicalType::BIGINT;
        const auto DBL  = LogicalType::DOUBLE;
        const auto TS   = LogicalType::TIMESTAMP_TZ;
        const auto IVAL = LogicalType::INTERVAL;
        const auto INTSPAN  = SpanTypes::INTSPAN();
        const auto BIGSPAN  = SpanTypes::BIGINTSPAN();
        const auto FLTSPAN  = SpanTypes::FLOATSPAN();
        const auto TSTZSPAN = SpanTypes::TSTZSPAN();
        LogicalType list_intspan  = LogicalType::LIST(INTSPAN);
        LogicalType list_bigspan  = LogicalType::LIST(BIGSPAN);
        LogicalType list_fltspan  = LogicalType::LIST(FLTSPAN);
        LogicalType list_tstzspan = LogicalType::LIST(TSTZSPAN);

        // bins(intspan, int, int)
        loader.RegisterFunction(ScalarFunction("bins",
            {INTSPAN, INT, INT}, list_intspan,
            SpanBinsIntExec<SpanBinInt32ToDatum>));
        // bins(bigintspan, bigint, bigint)
        loader.RegisterFunction(ScalarFunction("bins",
            {BIGSPAN, BIG, BIG}, list_bigspan, SpanBinsBigintExec));
        // bins(floatspan, double, double)
        loader.RegisterFunction(ScalarFunction("bins",
            {FLTSPAN, DBL, DBL}, list_fltspan, SpanBinsFloatExec));
        // bins(tstzspan, interval, timestamptz)
        loader.RegisterFunction(ScalarFunction("bins",
            {TSTZSPAN, IVAL, TS}, list_tstzspan, SpanBinsTstzExec));

        // timeBins(temporal, interval, timestamptz) for every temporal type.
        for (const auto &type : TemporalTypes::AllTypes()) {
            loader.RegisterFunction(ScalarFunction("timeBins",
                {type, IVAL, TS}, list_tstzspan, TemporalTimeBinsExec));
        }
        loader.RegisterFunction(ScalarFunction("timeBins",
            {TgeompointType::TGEOMPOINT(), IVAL, TS}, list_tstzspan,
            TemporalTimeBinsExec));
        loader.RegisterFunction(ScalarFunction("timeBins",
            {TGeometryTypes::TGEOMETRY(), IVAL, TS}, list_tstzspan,
            TemporalTimeBinsExec));
        loader.RegisterFunction(ScalarFunction("timeBins",
            {TGeographyTypes::TGEOGRAPHY(), IVAL, TS}, list_tstzspan,
            TemporalTimeBinsExec));
        loader.RegisterFunction(ScalarFunction("timeBins",
            {TGeogpointType::TGEOGPOINT(), IVAL, TS}, list_tstzspan,
            TemporalTimeBinsExec));

        // valueBins(tnumber, size, origin) — separate template
        // instantiations because the Datum encoding differs.
        loader.RegisterFunction(ScalarFunction("valueBins",
            {TemporalTypes::TINT(), INT, INT}, list_intspan,
            TnumberValueBinsExec<true>));
        loader.RegisterFunction(ScalarFunction("valueBins",
            {TemporalTypes::TFLOAT(), DBL, DBL}, list_fltspan,
            TnumberValueBinsExec<false>));

        // splitNTboxes / splitEachNTboxes — partition a tnumber into N
        // boxes (or N elements per box). Both return LIST<tbox>.
        loader.RegisterFunction(ScalarFunction("splitNTboxes",
            {TemporalTypes::TINT(), INT}, list_tbox,
            TnumberSplitTboxesExec<tnumber_split_n_tboxes>));
        loader.RegisterFunction(ScalarFunction("splitNTboxes",
            {TemporalTypes::TFLOAT(), INT}, list_tbox,
            TnumberSplitTboxesExec<tnumber_split_n_tboxes>));
        loader.RegisterFunction(ScalarFunction("splitEachNTboxes",
            {TemporalTypes::TINT(), INT}, list_tbox,
            TnumberSplitTboxesExec<tnumber_split_each_n_tboxes>));
        loader.RegisterFunction(ScalarFunction("splitEachNTboxes",
            {TemporalTypes::TFLOAT(), INT}, list_tbox,
            TnumberSplitTboxesExec<tnumber_split_each_n_tboxes>));

        // frechetDistancePath / dynTimeWarpPath — emit
        // LIST<STRUCT(i INTEGER, j INTEGER)>. Wired for tnumber and
        // for the four spatial-temporal types (the MEOS function
        // dispatches on subtype internally).
        const auto warp_struct = LogicalType::STRUCT({
            {"i", LogicalType::INTEGER},
            {"j", LogicalType::INTEGER},
        });
        const auto list_warp = LogicalType::LIST(warp_struct);
        for (const auto &t : {TemporalTypes::TINT(), TemporalTypes::TFLOAT(),
                               TgeompointType::TGEOMPOINT(), TGeogpointType::TGEOGPOINT(),
                               TGeometryTypes::TGEOMETRY(), TGeographyTypes::TGEOGRAPHY()}) {
            loader.RegisterFunction(ScalarFunction("frechetDistancePath",
                {t, t}, list_warp, TemporalPathExec<temporal_frechet_path>));
            loader.RegisterFunction(ScalarFunction("dynTimeWarpPath",
                {t, t}, list_warp, TemporalPathExec<temporal_dyntimewarp_path>));
        }
    }
}

} // namespace duckdb
