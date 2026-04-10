#include "meos_wrapper_simple.hpp"

#include "common.hpp"
#include "temporal/temporal.hpp"
#include "temporal/temporal_functions.hpp"
#include "temporal/spanset.hpp"
#include "temporal/tbox.hpp"

#include "duckdb/common/types/blob.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/common/types/data_chunk.hpp"

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

    //     ExtensionUtil::RegisterCastFunction(
    //         instance,
    //         type,
    //         type,
    //         TemporalFunctions::Temporal_enforce_typmod_cast,
    //         100
    //     );
    // }

    ExtensionUtil::RegisterCastFunction(
        instance,
        LogicalType::BLOB,
        SpansetTypes::tstzspanset(),
        TemporalFunctions::Blob_to_tstzspanset
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        TemporalTypes::TBOOL(),
        TemporalTypes::TINT(),
        TemporalFunctions::Tbool_to_tint_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        TemporalTypes::TINT(),
        TemporalTypes::TFLOAT(),
        TemporalFunctions::Tint_to_tfloat_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        TemporalTypes::TFLOAT(),
        TemporalTypes::TINT(),
        TemporalFunctions::Tfloat_to_tint_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        TemporalTypes::TINT(),
        TboxType::TBOX(),
        TemporalFunctions::Tnumber_to_tbox_cast
    );

    ExtensionUtil::RegisterCastFunction(
        instance,
        TemporalTypes::TFLOAT(),
        TboxType::TBOX(),
        TemporalFunctions::Tnumber_to_tbox_cast
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
                StringUtil::Lower(type.GetAlias()),
                {type, LogicalType::INTEGER},
                type,
                TemporalFunctions::Temporal_enforce_typmod
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SetTypes::tstzset()},
                type,
                TemporalFunctions::Tsequence_from_base_tstzset
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SpanTypes::TSTZSPAN()},
                type,
                TemporalFunctions::Tsequence_from_base_tstzspan
            )
        );
        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SpanTypes::TSTZSPAN(), LogicalType::VARCHAR},
                type,
                TemporalFunctions::Tsequence_from_base_tstzspan
            )
        );
        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SpansetTypes::tstzspanset()},
                type,
                TemporalFunctions::Tsequenceset_from_base_tstzspanset
            )
        );
        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str()), SpansetTypes::tstzspanset(), LogicalType::VARCHAR},
                type,
                TemporalFunctions::Tsequenceset_from_base_tstzspanset
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

            ExtensionUtil::RegisterFunction(
                instance,
                ScalarFunction(
                    "atMin",
                    {type},
                    type,
                    TemporalFunctions::Temporal_at_min
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
                "endTimestamp",
                {type},
                LogicalType::TIMESTAMP_TZ,
                TemporalFunctions::Temporal_end_timestamptz
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                "timestamps",
                {type},
                LogicalType::LIST(LogicalType::TIMESTAMP_TZ),
                TemporalFunctions::Temporal_timestamps
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                "instants",
                {type},
                LogicalType::LIST(type),
                TemporalFunctions::Temporal_instants
            )
        );

        ExtensionUtil::RegisterFunction(
            instance,
            ScalarFunction(
                "atTime",
                {type, LogicalType::TIMESTAMP_TZ},
                type,
                TemporalFunctions::Temporal_at_timestamptz
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
        if (type.GetAlias() != "TBOOL") {
            ExtensionUtil::RegisterFunction(
                instance,
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

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "atValues",
            {TemporalTypes::TINT(), SpanTypes::INTSPAN()},
            TemporalTypes::TINT(),
            TemporalFunctions::Tnumber_at_span
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "atValues",
            {TemporalTypes::TFLOAT(), SpanTypes::FLOATSPAN()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Tnumber_at_span
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "round",
            {TemporalTypes::TFLOAT()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Temporal_round
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "round",
            {TemporalTypes::TFLOAT(), LogicalType::INTEGER},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Temporal_round
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tint",
            {TemporalTypes::TBOOL()},
            TemporalTypes::TINT(),
            TemporalFunctions::Tbool_to_tint
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tfloat",
            {TemporalTypes::TINT()},
            TemporalTypes::TFLOAT(),
            TemporalFunctions::Tint_to_tfloat
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tint",
            {TemporalTypes::TFLOAT()},
            TemporalTypes::TINT(),
            TemporalFunctions::Tfloat_to_tint
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {TemporalTypes::TINT()},
            TboxType::TBOX(),
            TemporalFunctions::Tnumber_to_tbox
        )
    );

    ExtensionUtil::RegisterFunction(
        instance,
        ScalarFunction(
            "tbox",
            {TemporalTypes::TFLOAT()},
            TboxType::TBOX(),
            TemporalFunctions::Tnumber_to_tbox
        )
    );
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

void TemporalTypes::RegisterTemporalUnnestFunction(DatabaseInstance &instance) {
    for (auto &type : TemporalTypes::AllTypes()) {
        if (type.GetAlias() != "TBOOL") {
            TableFunction fn("tempUnnest",
                            {type},
                            TemporalUnnestExec,
                            TemporalUnnestBind,
                            TemporalUnnestInit);
            ExtensionUtil::RegisterFunction(instance, fn);
        }
    }
}

} // namespace duckdb