#include "meos_wrapper_simple.hpp"
#include "common.hpp"
#include "temporal/temporal_functions.hpp"
#include "temporal/spanset.hpp"
#include "geo_util.hpp"
#include "duckdb_version_compat.hpp"

#include "duckdb/common/types/blob.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <cmath>
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include <functional>

#include "time_util.hpp"

namespace duckdb {

static const alias_type_struct DUCKDB_ALIAS_TYPE_CATALOG[] = {
    {(char*)"tint", T_TINT},
    {(char*)"tbigint", T_TBIGINT},
    {(char*)"tfloat", T_TFLOAT},
    {(char*)"tbool", T_TBOOL},
    {(char*)"ttext", T_TTEXT},
    {(char*)"tgeompoint", T_TGEOMPOINT},
    {(char*)"tgeogpoint", T_TGEOGPOINT},
    {(char*)"tgeometry", T_TGEOMETRY}
};

MeosType TemporalHelpers::GetTemptypeFromAlias(const char *alias) {
    for (size_t i = 0; i < sizeof(DUCKDB_ALIAS_TYPE_CATALOG) / sizeof(DUCKDB_ALIAS_TYPE_CATALOG[0]); i++) {
        if (strcmp(alias, DUCKDB_ALIAS_TYPE_CATALOG[i].alias) == 0) {
            return DUCKDB_ALIAS_TYPE_CATALOG[i].temptype;
        }
    }
    throw InternalException("Unknown alias: " + std::string(alias));
}

/* ***************************************************
 * In/out functions: VARCHAR <-> Temporal
 ****************************************************/

bool TemporalFunctions::Temporal_in(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    auto &target_type = result.GetType();
    MeosType temptype = TemporalHelpers::GetTemptypeFromAlias(target_type.GetAlias().c_str());
    bool success = true;
    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(
        source, result, count,
        [&](string_t input_string, ValidityMask &mask, idx_t idx) {
            std::string input_str = input_string.GetString();
            Temporal *temp = temporal_in(input_str.c_str(), temptype);
            if (!temp) {
                throw InternalException("Failure in Temporal_in: unable to cast string to temporal");
                success = false;
                return string_t();
            }
            size_t temp_size = temporal_mem_size(temp);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, temp, temp_size);
            string_t output(reinterpret_cast<char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, output);

            free(temp_data);
            free(temp);
            return stored_data;
        }
    );
    return success;
}

bool TemporalFunctions::Temporal_out(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    bool success = true;
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_out] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid Temporal input: " + input_blob.GetString());
            }

            char *ret = temporal_out(temp, OUT_DEFAULT_DECIMAL_DIGITS);
            if (!ret) {
                free(data_copy);
                throw InternalException("Failure in Temporal_out: unable to cast temporal to string");
            }
            std::string ret_string(ret);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_string);

            free(ret);
            free(temp);
            return stored_data;
        }
    );
    return success;
}

bool TemporalFunctions::Blob_to_tstzspanset(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    bool success = true;
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) {
            return StringVector::AddStringOrBlob(result, input_blob);
        }
    );
    return success;
}

/* ***************************************************
 * Constructor functions
 ****************************************************/

/* ***************************************************
 * Conversion functions: [TYPE] -> Temporal
 ****************************************************/

static string_t Tbool_to_tint_common(string_t input, Vector &result) {
    const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
    size_t data_size = input.GetSize();
    if (data_size < sizeof(void*)) {
        throw InvalidInputException("[Tbool_to_tint] Invalid Temporal data: insufficient size");
    }
    uint8_t *data_copy = (uint8_t*)malloc(data_size);
    memcpy(data_copy, data, data_size);
    Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

    Temporal *ret = tbool_to_tint(temp);
    if (!ret) {
        free(data_copy);
        throw InternalException("Failure in Tbool_to_tint: unable to convert tbool to tint");
    }

    size_t ret_size = temporal_mem_size(ret);
    uint8_t *ret_data = (uint8_t*)malloc(ret_size);
    memcpy(ret_data, ret, ret_size);
    string_t result_str(reinterpret_cast<char*>(ret_data), ret_size);
    string_t stored_data = StringVector::AddStringOrBlob(result, result_str);

    free(ret_data);
    free(ret);
    free(data_copy);
    return stored_data;
}

bool TemporalFunctions::Tbool_to_tint_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input) {
            return Tbool_to_tint_common(input, result);
        }
    );
    return true;
}

static inline string_t Tint_to_tfloat_common(string_t input, Vector &result) {
    const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
    size_t data_size = input.GetSize();
    if (data_size < sizeof(void*)) {
        throw InvalidInputException("[Tint_to_tfloat] Invalid Temporal data: insufficient size");
    }
    uint8_t *data_copy = (uint8_t*)malloc(data_size);
    memcpy(data_copy, data, data_size);
    Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
    if (!temp) {
        free(data_copy);
        throw InternalException("Failure in Tint_to_tfloat: unable to cast string to temporal");
    }
    Temporal *ret = tint_to_tfloat(temp);
    if (!ret) {
        free(data_copy);
        throw InternalException("Failure in Tint_to_tfloat: unable to convert tint to tfloat");
    }
    size_t ret_size = temporal_mem_size(ret);
    uint8_t *ret_data = (uint8_t*)malloc(ret_size);
    memcpy(ret_data, ret, ret_size);
    string_t result_str(reinterpret_cast<char*>(ret_data), ret_size);
    string_t stored_data = StringVector::AddStringOrBlob(result, result_str);
    free(ret_data);
    free(ret);
    free(data_copy);
    return stored_data;
}
bool TemporalFunctions::Tint_to_tfloat_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input) {
            return Tint_to_tfloat_common(input, result);
        }
    );
    return true;
}
static inline string_t Tfloat_to_tint_common(string_t input, Vector &result) {
    const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
    size_t data_size = input.GetSize();
    if (data_size < sizeof(void*)) {
        throw InvalidInputException("[Tfloat_to_tint] Invalid Temporal data: insufficient size");
    }
    uint8_t *data_copy = (uint8_t*)malloc(data_size);
    memcpy(data_copy, data, data_size);
    Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
    if (!temp) {
        free(data_copy);
        throw InternalException("Failure in Tfloat_to_tint: unable to cast string to temporal");
    }
    Temporal *ret = tfloat_to_tint(temp);
    if (!ret) {
        free(data_copy);
        throw InternalException("Failure in Tfloat_to_tint: unable to convert tfloat to tint");
    }
    size_t ret_size = temporal_mem_size(ret);
    uint8_t *ret_data = (uint8_t*)malloc(ret_size);
    memcpy(ret_data, ret, ret_size);
    string_t result_str(reinterpret_cast<char*>(ret_data), ret_size);
    string_t stored_data = StringVector::AddStringOrBlob(result, result_str);
    free(ret_data);
    free(ret);
    free(data_copy);
    return stored_data;
}
bool TemporalFunctions::Tfloat_to_tint_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input) {
            return Tfloat_to_tint_common(input, result);
        }
    );
    return true;
}

static inline string_t Tnumber_to_tbox_common(Datum datum, MeosType basetype, Vector &result) {
    TBox *tbox = number_tbox(datum, basetype);
    size_t tbox_size = sizeof(TBox);
    uint8_t *tbox_data = (uint8_t*)malloc(tbox_size);
    memcpy(tbox_data, tbox, tbox_size);
    string_t result_str(reinterpret_cast<char*>(tbox_data), tbox_size);
    string_t stored_data = StringVector::AddStringOrBlob(result, result_str);
    free(tbox_data);
    free(tbox);
    return stored_data;
}
static string_t Tnumber_temporal_to_tbox_common(string_t input, Vector &result) {
    const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
    size_t data_size = input.GetSize();
    if (data_size < sizeof(void*)) {
        throw InvalidInputException("[Tnumber_to_tbox] Invalid Temporal data: insufficient size");
    }
    uint8_t *data_copy = (uint8_t*)malloc(data_size);
    memcpy(data_copy, data, data_size);
    Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

    TBox *tbox = tnumber_to_tbox(temp);
    if (!tbox) {
        free(data_copy);
        throw InternalException("Failure in Tnumber_to_tbox: unable to convert temporal to tbox");
    }

    size_t tbox_size = sizeof(TBox);
    uint8_t *tbox_data = (uint8_t*)malloc(tbox_size);
    memcpy(tbox_data, tbox, tbox_size);
    string_t result_str(reinterpret_cast<char*>(tbox_data), tbox_size);
    string_t stored_data = StringVector::AddStringOrBlob(result, result_str);

    free(tbox_data);
    free(tbox);
    free(data_copy);
    return stored_data;
}

bool TemporalFunctions::Tnumber_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    if (source.GetType().id() == LogicalTypeId::BLOB) {
        UnaryExecutor::Execute<string_t, string_t>(
            source, result, count,
            [&](string_t input) {
                return Tnumber_temporal_to_tbox_common(input, result);
            }
        );
    } else if (source.GetType().id() == LogicalTypeId::DOUBLE) {
        MeosType basetype = TemporalHelpers::GetTemptypeFromAlias(source.GetType().GetAlias().c_str());
        UnaryExecutor::Execute<double, string_t>(
            source, result, count,
            [&](double value) {
                return Tnumber_to_tbox_common(Float8GetDatum(value), basetype, result);
            }
        );
    } else if (source.GetType().id() == LogicalTypeId::INTEGER || source.GetType().id() == LogicalTypeId::BIGINT ||
               source.GetType().id() == LogicalTypeId::SMALLINT || source.GetType().id() == LogicalTypeId::TINYINT) {
        MeosType basetype = TemporalHelpers::GetTemptypeFromAlias(source.GetType().GetAlias().c_str());
        UnaryExecutor::Execute<int64_t, string_t>(
            source, result, count,
            [&](int64_t value) {
                return Tnumber_to_tbox_common((Datum)value, basetype, result);
            }
        );
    } else {
        throw InvalidInputException("Invalid argument type for Tnumber_to_tbox_cast: " + source.GetType().ToString());
    }
    return true;
}
/****************************************************
 * Special Cast
****************************************************/

void TemporalFunctions::Temporal_enforce_typmod(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, int32_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input, int32_t typmod) -> string_t {
            return StringVector::AddStringOrBlob(result, input);
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// Tnumber_at_tbox / Tnumber_minus_tbox retired: atTbox/minusTbox are generated from the
// catalog (Gen_tnumber_at_tbox / Gen_tnumber_minus_tbox in generated_temporal_udfs.cpp),
// which call the same MEOS tnumber_at_tbox / tnumber_minus_tbox.

// Tnumber_avg_value retired: avgValue(tint | tbigint | tfloat) is generated from the catalog
// (Gen_tnumber_avg_value in generated_temporal_udfs.cpp), which calls the same MEOS
// tnumber_avg_value.

// whenTrue (tbool_when_true), tsample (temporal_tsample), shiftScaleTime
// (temporal_shift_scale_time) and the <type>Inst conversions (temporal_as_tinstant) are generated
// from the catalog (generated_temporal_udfs.cpp).

/* ***************************************************
 * Arithmetic operators on tnumber
 ****************************************************/

// floor/ceil/round/degrees/radians on tfloat are generated from the catalog
// (generated_temporal_udfs.cpp); no hand bodies remain.

/* ***************************************************
 * Workaround functions
 ****************************************************/

template <typename T>
void TemporalFunctions::Temporal_dump_common(DataChunk &args, Vector &result, MeosType basetype) {
    auto count = args.size();
    auto &temp_vec = args.data[0];
    UnifiedVectorFormat temp_format;
    temp_vec.ToUnifiedFormat(count, temp_format);

    idx_t total_temp_count = 0;

    vector<T> values;
    vector<string_t> times;

    for (idx_t out_row_idx = 0; out_row_idx < count; out_row_idx++) {
        auto in_row_idx = temp_format.sel->get_index(out_row_idx);

        if (!temp_format.validity.RowIsValid(in_row_idx)) {
            FlatVector::SetNull(result, out_row_idx, true);
            continue;
        }

        string_t blob = UnifiedVectorFormat::GetData<string_t>(temp_format)[in_row_idx];
        const uint8_t *data = reinterpret_cast<const uint8_t*>(blob.GetData());
        size_t data_size = blob.GetSize();
        if (data_size < sizeof(void*)) {
            throw InvalidInputException("[Temporal_dump_common] Invalid Temporal data: insufficient size");
        }
        uint8_t *data_copy = (uint8_t*)malloc(data_size);
        memcpy(data_copy, data, data_size);
        Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
        if (!temp) {
            free(data_copy);
            throw InternalException("Failure in Temporal_dump: unable to cast string to temporal");
        }

        int32_t elem_count;
        Datum *extracted_values = temporal_values(temp, &elem_count);
        Temporal *temp_copy = temporal_copy(temp);

        values.clear();
        times.clear();

        for (idx_t i = 0; i < elem_count; i++) {
            Datum val = extracted_values[i];

            if constexpr (std::is_same_v<T, int32_t>) {
                int32_t actual_value = DatumGetInt32(val);
                values.push_back(actual_value);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                int64_t actual_value = DatumGetInt64(val);
                values.push_back(actual_value);
            } else if constexpr (std::is_same_v<T, double>) {
                double actual_value = DatumGetFloat8(val);
                values.push_back(actual_value);
            } else if constexpr (std::is_same_v<T, string_t>) {
                text *txt = DatumGetTextP(val);
                char *actual_value = text_to_cstring(txt);
                values.push_back(string_t(actual_value));
            }

            Temporal *rest = temporal_restrict_value(temp_copy, extracted_values[i], true);
            SpanSet *time_spanset = temporal_time(rest);
            size_t spanset_size = spanset_mem_size(time_spanset);
            uint8_t *spanset_data = (uint8_t *)malloc(spanset_size);
            memcpy(spanset_data, time_spanset, spanset_size);
            string_t spanset_str(reinterpret_cast<const char*>(spanset_data), spanset_size);
            times.push_back(spanset_str);
            free(time_spanset);
            free(rest);
        }

        auto result_entries = ListVector::GetData(result);
        auto val_offset = total_temp_count;
        auto val_length = values.size();

        result_entries[out_row_idx].length = val_length;
        result_entries[out_row_idx].offset = val_offset;

        total_temp_count += val_length;

        ListVector::Reserve(result, total_temp_count);
        ListVector::SetListSize(result, total_temp_count);

        auto &result_list = ListVector::GetEntry(result);
        auto &result_list_children = StructVector::GetEntries(result_list);
        auto &result_val_vec = result_list_children[0];
        auto &result_time_vec = result_list_children[1];

        auto val_data = FlatVector::GetData<T>(*result_val_vec);
        auto time_data = FlatVector::GetData<string_t>(*result_time_vec);
        for (idx_t i = 0; i < val_length; i++) {
            val_data[val_offset + i] = values[i];
            time_data[val_offset + i] = times[i];
        }
        free(temp_copy);
        free(extracted_values);
        free(temp);
    }
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_dump(DataChunk &args, ExpressionState &state, Vector &result) {
    // const auto &basetype = args.data[0].GetType();
    LogicalType base_struct_type = ListType::GetChildType(result.GetType());
    const auto &basetype = StructType::GetChildType(base_struct_type, 0);

    switch (basetype.id()) {
        case LogicalTypeId::INTEGER: {
            Temporal_dump_common<int32_t>(args, result, T_INT4);
            break;
        }
        case LogicalTypeId::BIGINT: {
            Temporal_dump_common<int64_t>(args, result, T_INT8);
            break;
    }
        case LogicalTypeId::DOUBLE: {
            Temporal_dump_common<double>(args, result, T_FLOAT8);
            break;
        }
        case LogicalTypeId::VARCHAR: {
            Temporal_dump_common<string_t>(args, result, T_TEXT);
            break;
        }
        default: {
            throw NotImplementedException("Temporal dump: unsupported base type");
        }
    }
}

/* ***************************************************
 * Math functions
 ****************************************************/

// round(tfloat) at both arities is generated from the catalog temporal_round
// signature (generated_temporal_udfs.cpp); no hand body remains.

} // namespace duckdb
