#include "meos_wrapper_simple.hpp"

#include "common.hpp"
#include "temporal/temporal.hpp"
#include "temporal/temporal_functions.hpp"
#include "temporal/spanset.hpp"

#include "duckdb/common/types/blob.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "duckdb/common/extension_type_info.hpp"

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

void TemporalTypes::RegisterTypes(DatabaseInstance &db) {
    ExtensionUtil::RegisterType(db, "TINT", TINT());
    ExtensionUtil::RegisterType(db, "TBOOL", TBOOL());
    ExtensionUtil::RegisterType(db, "TFLOAT", TFLOAT());
    ExtensionUtil::RegisterType(db, "TTEXT", TTEXT());
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

void TemporalTypes::RegisterCastFunctions(DatabaseInstance &instance) {
    for (auto &type : TemporalTypes::AllTypes()) {
        ExtensionUtil::RegisterCastFunction(
            instance,
            LogicalType::VARCHAR,
            type,
            TemporalFunctions::Temporal_in
        );

        ExtensionUtil::RegisterCastFunction(
            instance,
            type,
            LogicalType::VARCHAR,
            TemporalFunctions::Temporal_out
        );
    }
}

void TemporalTypes::RegisterScalarFunctions(DatabaseInstance &instance) {
    for (auto &type : TemporalTypes::AllTypes()) {
        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), LogicalType::TIMESTAMP_TZ},
                type,
                TemporalFunctions::Tinstant_constructor
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                "tempSubtype",
                {type},
                LogicalType::VARCHAR,
                TemporalFunctions::Temporal_subtype
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                "interp",
                {type},
                LogicalType::VARCHAR,
                TemporalFunctions::Temporal_interp
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                "getValue",
                {type},
                TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()),
                TemporalFunctions::Tinstant_value
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                "startValue",
                {type},
                TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()),
                TemporalFunctions::Temporal_start_value
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                "endValue",
                {type},
                TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()),
                TemporalFunctions::Temporal_end_value
            )
        );

        if (type.GetAlias() != "TBOOL") {
            ExtensionUtil::RegisterFunction(
                instance,
                ScalarFunction(
                    "minValue",
                    {type},
                    TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()),
                    TemporalFunctions::Temporal_min_value
                )
            );

            ExtensionUtil::RegisterFunction(
                instance,
                ScalarFunction(
                    "maxValue",
                    {type},
                    TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()),
                    TemporalFunctions::Temporal_max_value
                )
            );

            ExtensionUtil::RegisterFunction(
                instance,
                ScalarFunction(
                    "minInstant",
                    {type},
                    type,
                    TemporalFunctions::Temporal_min_instant
                )
            );
    
            ExtensionUtil::RegisterFunction(
                instance,
                ScalarFunction(
                    "maxInstant",
                    {type},
                    type,
                    TemporalFunctions::Temporal_max_instant
                )
            );
        }

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                "valueN",
                {type, LogicalType::BIGINT},
                TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()),
                TemporalFunctions::Temporal_value_n
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                "getTimestamp",
                {type},
                LogicalType::TIMESTAMP_TZ,
                TemporalFunctions::Tinstant_timestamptz
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                "getTime",
                {type},
                SpansetTypes::tstzspanset(),
                TemporalFunctions::Temporal_time
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                "duration",
                {type, LogicalType::BOOLEAN},
                LogicalType::INTERVAL,
                TemporalFunctions::Temporal_duration
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "Seq",
                {LogicalType::LIST(type)},
                type,
                TemporalFunctions::Tsequence_constructor
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "Seq",
                {LogicalType::LIST(type), LogicalType::VARCHAR},
                type,
                TemporalFunctions::Tsequence_constructor
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "Seq",
                {LogicalType::LIST(type), LogicalType::VARCHAR, LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Tsequence_constructor
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "Seq",
                {LogicalType::LIST(type), LogicalType::VARCHAR, LogicalType::BOOLEAN, LogicalType::BOOLEAN},
                type,
                TemporalFunctions::Tsequence_constructor
            )
        );
        
        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "Seq",
                {type, LogicalType::VARCHAR},
                type,
                TemporalFunctions::Temporal_to_tsequence
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "Seq",
                {type},
                type,
                TemporalFunctions::Temporal_to_tsequence
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "SeqSet",
                {LogicalType::LIST(type)},
                type,
                TemporalFunctions::Tsequenceset_constructor
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()) + "SeqSet",
                {type},
                type,
                TemporalFunctions::Temporal_to_tsequenceset
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                "timeSpan",
                {type},
                SpanTypes::TSTZSPAN(),
                TemporalFunctions::Temporal_to_tstzspan
            )
        );

        if (type.GetAlias() == "TINT") {
            ExtensionUtil::RegisterFunction(
                instance,
                ScalarFunction(
                    "valueSpan",
                    {type},
                    SpanTypes::INTSPAN(),
                    TemporalFunctions::Tnumber_to_span
                )
            );

            ExtensionUtil::RegisterFunction(
                instance,
                ScalarFunction(
                    "valueSet",
                    {type},
                    SetTypes::intset(),
                    TemporalFunctions::Temporal_valueset
                )
            );
        } else if (type.GetAlias() == "TFLOAT") {
            ExtensionUtil::RegisterFunction(
                instance,
                ScalarFunction(
                    "valueSpan",
                    {type},
                    SpanTypes::FLOATSPAN(),
                    TemporalFunctions::Tnumber_to_span
                )
            );

            ExtensionUtil::RegisterFunction(
                instance,
                ScalarFunction(
                    "valueSet",
                    {type},
                    SetTypes::floatset(),
                    TemporalFunctions::Temporal_valueset
                )
            );
        }

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                "sequences",
                {type},
                LogicalType::LIST(type),
                TemporalFunctions::Temporal_sequences
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                "startTimestamp",
                {type},
                LogicalType::TIMESTAMP_TZ,
                TemporalFunctions::Temporal_start_timestamptz
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                "atTime",
                {type, SpanTypes::TSTZSPAN()},
                type,
                TemporalFunctions::Temporal_at_tstzspan
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                "atTime",
                {type, SpansetTypes::tstzspanset()},
                type,
                TemporalFunctions::Temporal_at_tstzspanset
            )
        );

        if (type.GetAlias() == "TINT" || type.GetAlias() == "TFLOAT") {
            ExtensionUtil::RegisterFunction(
                instance,
                ScalarFunction(
                    "shiftValue",
                    {type, LogicalType::BIGINT},
                    type,
                    TemporalFunctions::Tnumber_shift_value
                )
            );

            ExtensionUtil::RegisterFunction(
                instance,
                ScalarFunction(
                    "scaleValue",
                    {type, LogicalType::BIGINT},
                    type,
                    TemporalFunctions::Tnumber_scale_value
                )
            );

            ExtensionUtil::RegisterFunction(
                instance,
                ScalarFunction(
                    "shiftScaleValue",
                    {type, LogicalType::BIGINT, LogicalType::BIGINT},
                    type,
                    TemporalFunctions::Tnumber_shift_scale_value
                )
            );
        }
    }

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "atValues",
            {TemporalTypes::TBOOL(), LogicalType::BOOLEAN},
            TemporalTypes::TBOOL(),
            TemporalFunctions::Temporal_at_value_tbool
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "whenTrue",
            {TemporalTypes::TBOOL()},
            SpansetTypes::tstzspanset(),
            TemporalFunctions::Tbool_when_true
        )
    );
}

} // namespace duckdb