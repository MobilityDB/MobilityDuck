#include "meos_wrapper_simple.hpp"
#include "common.hpp"
// #include "temporal/temporal_types.hpp"
#include "temporal/temporal_functions.hpp"
#include "temporal/spanset.hpp"

#include "duckdb/common/types/blob.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_util.hpp"
#include <cmath>
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

#include "time_util.hpp"

namespace duckdb {

static const alias_type_struct DUCKDB_ALIAS_TYPE_CATALOG[] = {
    {(char*)"TINT", T_TINT},
    {(char*)"TFLOAT", T_TFLOAT},
    {(char*)"TBOOL", T_TBOOL},
    {(char*)"TTEXT", T_TTEXT},
    {(char*)"TGEOMPOINT", T_TGEOMPOINT},
    {(char*)"TGEOGPOINT", T_TGEOGPOINT},
    {(char*)"TGEOMETRY", T_TGEOMETRY}
};

meosType TemporalHelpers::GetTemptypeFromAlias(const char *alias) {
    for (size_t i = 0; i < sizeof(DUCKDB_ALIAS_TYPE_CATALOG) / sizeof(DUCKDB_ALIAS_TYPE_CATALOG[0]); i++) {
        if (strcmp(alias, DUCKDB_ALIAS_TYPE_CATALOG[i].alias) == 0) {
            return DUCKDB_ALIAS_TYPE_CATALOG[i].temptype;
        }
    }
    throw InternalException("Unknown alias: " + std::string(alias));
}

vector<Value> TemporalHelpers::TempArrToArray(Temporal **temparr, int32_t count, LogicalType element_type) {
    vector<Value> values;
    values.reserve(count);

    for (idx_t i = 0; i < count; i++) {
        vector<Value> struct_values;
        struct_values.push_back(Value::BIGINT((uintptr_t)temparr[i]));

        Value val = Value::STRUCT(element_type, struct_values);
        values.push_back(val);
    }
    return values;
}

/* ***************************************************
 * In/out functions: VARCHAR <-> Temporal
 ****************************************************/

bool TemporalFunctions::Temporal_in(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    auto &target_type = result.GetType();
    meosType temptype = TemporalHelpers::GetTemptypeFromAlias(target_type.GetAlias().c_str());
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

bool TemporalFunctions::Composite_out(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    source.Flatten(count);
    auto &children = StructVector::GetEntries(source);
    auto &value_child = children[0];
    auto &time_child = children[1];

    for (idx_t i = 0; i < count; i++) {
        string_t value_str = value_child->GetValue(i).ToString();

        // string_t spanset_str = time_child->GetValue(i);
        // SpanSet *spanset = nullptr;
        // if (spanset_str.GetSize() > 0) {
        //     spanset = (SpanSet*)malloc(spanset_str.GetSize());
        //     memcpy(spanset, spanset_str.GetData(), spanset_str.GetSize());
        // }
        // if (!spanset) {
        //     throw InternalException("Failure in Composite_out: unable to reconstruct spanset");
        // }
        // char *cstr = spanset_out(spanset, 15);

        std::string result_str = "{value: " + value_str.GetString() + "}";
        string_t stored_data = StringVector::AddStringOrBlob(result, result_str);
        result.SetValue(i, stored_data);
    }
    return true;
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

template <typename T>
void TemporalFunctions::Tinstant_constructor_common(Vector &value, Vector &ts, Vector &result, idx_t count) {
    BinaryExecutor::Execute<T, timestamp_tz_t, string_t>(
        value, ts, result, count,
        [&](T value, timestamp_tz_t ts) {
            meosType temptype = TemporalHelpers::GetTemptypeFromAlias(result.GetType().GetAlias().c_str());
            timestamp_tz_t meos_ts = DuckDBToMeosTimestamp(ts);

            Datum datum;
            if (temptype == T_TFLOAT) {
                datum = Float8GetDatum(value);
            } else {
                datum = (Datum)value;
            }
            TInstant *inst = tinstant_make(datum, temptype, (TimestampTz)meos_ts.value);
            Temporal *temp = (Temporal*)inst;

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
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Tinstant_constructor_text(Vector &value, Vector &ts, Vector &result, idx_t count) {
    BinaryExecutor::Execute<string_t, timestamp_tz_t, string_t>(
        value, ts, result, count,
        [&](string_t value, timestamp_tz_t ts) {
            meosType temptype = TemporalHelpers::GetTemptypeFromAlias(result.GetType().GetAlias().c_str());
            timestamp_tz_t meos_ts = DuckDBToMeosTimestamp(ts);

            std::string str = value.GetString();
            text *txt = cstring2text(str.c_str());
            TInstant *inst = ttextinst_make(txt, (TimestampTz)meos_ts.value);
            Temporal *temp = (Temporal*)inst;

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
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Tinstant_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    const auto &arg_type = args.data[0].GetType();

    if (arg_type.id() == LogicalTypeId::VARCHAR) {
        Tinstant_constructor_text(args.data[0], args.data[1], result, args.size());
    } else if (arg_type.id() == LogicalTypeId::DOUBLE || arg_type.id() == LogicalTypeId::FLOAT) {
        Tinstant_constructor_common<double>(args.data[0], args.data[1], result, args.size());
    } else if (arg_type.id() == LogicalTypeId::BOOLEAN) {
        Tinstant_constructor_common<bool>(args.data[0], args.data[1], result, args.size());
    } else if (arg_type.id() == LogicalTypeId::INTEGER || arg_type.id() == LogicalTypeId::BIGINT || arg_type.id() == LogicalTypeId::SMALLINT || arg_type.id() == LogicalTypeId::TINYINT) {
        Tinstant_constructor_common<int64_t>(args.data[0], args.data[1], result, args.size());
    } else {
        throw InvalidInputException("Invalid argument type for Tinstant_constructor: " + arg_type.ToString());
    }
}

void TemporalFunctions::Tsequence_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto row_count = args.size();
    auto arg_count = args.ColumnCount();
    auto &array_vec = args.data[0];
    array_vec.Flatten(row_count);
    auto *list_entries = ListVector::GetData(array_vec);
    auto &child_vec = ListVector::GetEntry(array_vec);

    meosType temptype = TemporalHelpers::GetTemptypeFromAlias(result.GetType().GetAlias().c_str());
    interpType interp = temptype_continuous(temptype) ? LINEAR : STEP;
    bool lower_inc = true;
    bool upper_inc = true;
    
    if (arg_count > 1) {
        auto &interp_child = args.data[1];
        interp_child.Flatten(row_count);
        auto interp_str = interp_child.GetValue(0).ToString();
        interp = interptype_from_string(interp_str.c_str());
    }
    if (arg_count > 2) {
        auto &lower_inc_child = args.data[2];
        lower_inc = lower_inc_child.GetValue(0).GetValue<bool>();
    }
    if (arg_count > 3) {
        auto &upper_inc_child = args.data[3];
        upper_inc = upper_inc_child.GetValue(0).GetValue<bool>();
    }

    child_vec.Flatten(ListVector::GetListSize(array_vec));
    auto child_data = FlatVector::GetData<string_t>(child_vec);

    UnaryExecutor::Execute<list_entry_t, string_t>(
        array_vec, result, row_count,
        [&](const list_entry_t &list) {
            auto offset = list.offset;
            auto length = list.length;
            
            int32_t valid_count = 0;
            for (idx_t i = 0; i < length; i++) {
                idx_t child_idx = offset + i;
                auto wkb_data = child_data[child_idx];
                size_t data_size = wkb_data.GetSize();
                if (data_size < sizeof(void*)) {
                    continue;
                }
                if (wkb_data.GetData() == nullptr) {
                    continue;
                }
                if (wkb_data.GetDataUnsafe() == nullptr) {
                    continue;
                }
                if (data_size > 0) {
                    valid_count++;
                }
            }

            TInstant **instants = (TInstant **)malloc(valid_count * sizeof(TInstant *));
            if (!instants) {
                throw InternalException("Memory allocation failed in TsequenceConstructor");
            }

            idx_t valid_idx = 0;
            for (idx_t i = 0; i < length; i++) {
                idx_t child_idx = offset + i;
                auto wkb_data = child_data[child_idx];
                size_t data_size = wkb_data.GetSize();
                if (data_size < sizeof(void*)) {
                    continue;
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, wkb_data.GetData(), data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
                if (!temp) {
                    free(instants);
                    throw InternalException("Failure in TsequenceConstructor: unable to convert WKB to temporal");
                }
                instants[valid_idx] = (TInstant*)temp;
                valid_idx++;
            }

            TSequence *seq = tsequence_make((TInstant **)instants, valid_count,
                lower_inc, upper_inc, interp, true);
            if (!seq) {
                for (idx_t j = 0; j < valid_count; j++) {
                    free(instants[j]);
                }
                free(instants);
                throw InternalException("Failure in TsequenceConstructor: unable to create sequence");
            }

            size_t temp_size = temporal_mem_size((Temporal*)seq);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, (Temporal*)seq, temp_size);
            string_t result_str(reinterpret_cast<char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, result_str);
            free(seq);
            for (idx_t j = 0; j < valid_count; j++) {
                free(instants[j]);
            }
            free(instants);
            free(temp_data);
            return stored_data;
        }
    );
    if (row_count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Tsequenceset_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &array_vec = args.data[0];
    array_vec.Flatten(count);
    auto *list_entries = ListVector::GetData(array_vec);
    auto &child_vec = ListVector::GetEntry(array_vec);

    child_vec.Flatten(ListVector::GetListSize(array_vec));
    auto child_data = FlatVector::GetData<string_t>(child_vec);

    UnaryExecutor::Execute<list_entry_t, string_t>(
        array_vec, result, count,
        [&](const list_entry_t &list) {
            auto offset = list.offset;
            auto length = list.length;

            TSequence **sequences = (TSequence **)malloc(length * sizeof(TSequence *));
            if (!sequences) {
                throw InternalException("Memory allocation failed in TsequencesetConstructor");
            }
            for (idx_t i = 0; i < length; i++) {
                idx_t child_idx = offset + i;
                auto wkb_data = child_data[child_idx];
                size_t data_size = wkb_data.GetSize();
                if (data_size < sizeof(void*)) {
                    free(sequences);
                    throw InvalidInputException("[Tsequenceset_constructor] Invalid Temporal data: insufficient size");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, wkb_data.GetData(), data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
                if (!temp) {
                    free(sequences);
                    throw InternalException("Failure in TsequencesetConstructor: unable to convert WKB to temporal");
                }
                sequences[i] = (TSequence*)temp;
            }

            TSequenceSet *seqset = tsequenceset_make((TSequence **)sequences, length, true);
            if (!seqset) {
                for (idx_t j = 0; j < length; j++) {
                    free(sequences[j]);
                }
                free(sequences);
                throw InternalException("Failure in TsequencesetConstructor: unable to create sequence set");
            }

            size_t temp_size = temporal_mem_size((Temporal*)seqset);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, (Temporal*)seqset, temp_size);
            string_t result_str(reinterpret_cast<char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, result_str);
            free(seqset);
            for (idx_t j = 0; j < length; j++) {
                free(sequences[j]);
            }
            free(sequences);
            free(temp_data);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

static string_t Tsequence_from_base_tstzset_impl(Datum datum, string_t set_blob, meosType temptype, Vector &result) {
    size_t data_size = set_blob.GetSize();
    if (data_size < sizeof(void*)) {
        throw InvalidInputException("[Tsequence_from_base_tstzset] Invalid tstzset data: insufficient size");
    }
    uint8_t *data_copy = (uint8_t*)malloc(data_size);
    memcpy(data_copy, set_blob.GetData(), data_size);
    Set *s = reinterpret_cast<Set*>(data_copy);

    TSequence *seq = tsequence_from_base_tstzset(datum, temptype, s);
    if (!seq) {
        free(data_copy);
        throw InternalException("Failure in Tsequence_from_base_tstzset: unable to create sequence");
    }

    size_t temp_size = temporal_mem_size((Temporal*)seq);
    uint8_t *temp_data = (uint8_t*)malloc(temp_size);
    memcpy(temp_data, (Temporal*)seq, temp_size);
    string_t result_str(reinterpret_cast<char*>(temp_data), temp_size);
    string_t stored_data = StringVector::AddStringOrBlob(result, result_str);

    free(temp_data);
    free(seq);
    free(data_copy);
    return stored_data;
}

void TemporalFunctions::Tsequence_from_base_tstzset(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    const auto &arg_type = args.data[0].GetType();
    meosType temptype = TemporalHelpers::GetTemptypeFromAlias(result.GetType().GetAlias().c_str());

    if (arg_type.id() == LogicalTypeId::VARCHAR) {
        BinaryExecutor::Execute<string_t, string_t, string_t>(
            args.data[0], args.data[1], result, count,
            [&](string_t value, string_t set_blob) {
                text *txt = cstring2text(value.GetString().c_str());
                return Tsequence_from_base_tstzset_impl(PointerGetDatum(txt), set_blob, temptype, result);
            });
    } else if (arg_type.id() == LogicalTypeId::DOUBLE) {
        BinaryExecutor::Execute<double, string_t, string_t>(
            args.data[0], args.data[1], result, count,
            [&](double value, string_t set_blob) {
                return Tsequence_from_base_tstzset_impl(Float8GetDatum(value), set_blob, temptype, result);
            });
    } else if (arg_type.id() == LogicalTypeId::BOOLEAN) {
        BinaryExecutor::Execute<bool, string_t, string_t>(
            args.data[0], args.data[1], result, count,
            [&](bool value, string_t set_blob) {
                return Tsequence_from_base_tstzset_impl((Datum)value, set_blob, temptype, result);
            });
    } else if (arg_type.id() == LogicalTypeId::INTEGER || arg_type.id() == LogicalTypeId::BIGINT || arg_type.id() == LogicalTypeId::SMALLINT || arg_type.id() == LogicalTypeId::TINYINT) {
        BinaryExecutor::Execute<int64_t, string_t, string_t>(
            args.data[0], args.data[1], result, count,
            [&](int64_t value, string_t set_blob) {
                return Tsequence_from_base_tstzset_impl((Datum)value, set_blob, temptype, result);
            });
    } else {
        throw InvalidInputException("Invalid argument type for Tsequence_from_base_tstzset: " + arg_type.ToString());
    }

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}   

static string_t Tsequence_from_base_tstzspan_impl(Datum datum, string_t span_blob, meosType temptype, interpType interp, Vector &result) {
    size_t data_size = span_blob.GetSize();
    if (data_size < sizeof(void*)) {
        throw InvalidInputException("[Tsequence_from_base_tstzspan] Invalid tstzspan data: insufficient size");
    }
    uint8_t *data_copy = (uint8_t*)malloc(data_size);
    memcpy(data_copy, span_blob.GetData(), data_size);
    Span *span = reinterpret_cast<Span*>(data_copy);

    TSequence *seq = tsequence_from_base_tstzspan(datum, temptype, span, interp);
    if (!seq) {
        free(data_copy);
        throw InternalException("Failure in Tsequence_from_base_tstzspan: unable to create sequence");
    }

    size_t temp_size = temporal_mem_size((Temporal*)seq);
    uint8_t *temp_data = (uint8_t*)malloc(temp_size);
    memcpy(temp_data, (Temporal*)seq, temp_size);
    string_t result_str(reinterpret_cast<char*>(temp_data), temp_size);
    string_t stored_data = StringVector::AddStringOrBlob(result, result_str);

    free(temp_data);
    free(seq);
    free(data_copy);
    return stored_data;
}

void TemporalFunctions::Tsequence_from_base_tstzspan(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    const auto &arg_type = args.data[0].GetType();
    meosType temptype = TemporalHelpers::GetTemptypeFromAlias(result.GetType().GetAlias().c_str());
    interpType interp = temptype_continuous(temptype) ? LINEAR : STEP;
    if (args.ColumnCount() > 2) {
        auto &interp_child = args.data[2];
        interp_child.Flatten(count);
        auto interp_str = interp_child.GetValue(0).ToString();
        interp = interptype_from_string(interp_str.c_str());
    }

    if (arg_type.id() == LogicalTypeId::VARCHAR) {
        BinaryExecutor::Execute<string_t, string_t, string_t>(
            args.data[0], args.data[1], result, count,
            [&](string_t value, string_t span_blob) {
                text *txt = cstring2text(value.GetString().c_str());
                return Tsequence_from_base_tstzspan_impl(PointerGetDatum(txt), span_blob, temptype, interp, result);
            });
    } else if (arg_type.id() == LogicalTypeId::DOUBLE || arg_type.id() == LogicalTypeId::FLOAT) {
        BinaryExecutor::Execute<double, string_t, string_t>(
            args.data[0], args.data[1], result, count,
            [&](double value, string_t span_blob) {
                return Tsequence_from_base_tstzspan_impl(Float8GetDatum(value), span_blob, temptype, interp, result);
            });
    } else if (arg_type.id() == LogicalTypeId::BOOLEAN) {
        BinaryExecutor::Execute<bool, string_t, string_t>(
            args.data[0], args.data[1], result, count,
            [&](bool value, string_t span_blob) {
                return Tsequence_from_base_tstzspan_impl((Datum)value, span_blob, temptype, interp, result);
            });
    } else if (arg_type.id() == LogicalTypeId::INTEGER || arg_type.id() == LogicalTypeId::BIGINT ||
               arg_type.id() == LogicalTypeId::SMALLINT || arg_type.id() == LogicalTypeId::TINYINT) {
        BinaryExecutor::Execute<int64_t, string_t, string_t>(
            args.data[0], args.data[1], result, count,
            [&](int64_t value, string_t span_blob) {
                return Tsequence_from_base_tstzspan_impl((Datum)value, span_blob, temptype, interp, result);
            });
    } else {
        throw InvalidInputException("Invalid argument type for Tsequence_from_base_tstzspan: " + arg_type.ToString());
    }

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

static string_t Tsequenceset_from_base_tstzspanset_impl(Datum datum, string_t spanset_blob, meosType temptype, interpType interp, Vector &result) {
    size_t data_size = spanset_blob.GetSize();
    if (data_size < sizeof(void*)) {
        throw InvalidInputException("[Tsequenceset_from_base_tstzspanset] Invalid tstzspanset data: insufficient size");
    }
    uint8_t *data_copy = (uint8_t*)malloc(data_size);
    memcpy(data_copy, spanset_blob.GetData(), data_size);
    SpanSet *spanset = reinterpret_cast<SpanSet*>(data_copy);

    TSequenceSet *ss = tsequenceset_from_base_tstzspanset(datum, temptype, spanset, interp);
    if (!ss) {
        free(data_copy);
        throw InternalException("Failure in Tsequenceset_from_base_tstzspanset: unable to create sequence set");
    }

    size_t temp_size = temporal_mem_size((Temporal*)ss);
    uint8_t *temp_data = (uint8_t*)malloc(temp_size);
    memcpy(temp_data, (Temporal*)ss, temp_size);
    string_t result_str(reinterpret_cast<char*>(temp_data), temp_size);
    string_t stored_data = StringVector::AddStringOrBlob(result, result_str);

    free(temp_data);
    free(ss);
    free(data_copy);
    return stored_data;
}

void TemporalFunctions::Tsequenceset_from_base_tstzspanset(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    const auto &arg_type = args.data[0].GetType();
    meosType temptype = TemporalHelpers::GetTemptypeFromAlias(result.GetType().GetAlias().c_str());
    interpType interp = temptype_continuous(temptype) ? LINEAR : STEP;
    if (args.ColumnCount() > 2) {
        auto &interp_child = args.data[2];
        interp_child.Flatten(count);
        auto interp_str = interp_child.GetValue(0).ToString();
        interp = interptype_from_string(interp_str.c_str());
    }

    if (arg_type.id() == LogicalTypeId::VARCHAR) {
        BinaryExecutor::Execute<string_t, string_t, string_t>(
            args.data[0], args.data[1], result, count,
            [&](string_t value, string_t spanset_blob) {
                text *txt = cstring2text(value.GetString().c_str());
                return Tsequenceset_from_base_tstzspanset_impl(PointerGetDatum(txt), spanset_blob, temptype, interp, result);
            });
    } else if (arg_type.id() == LogicalTypeId::DOUBLE || arg_type.id() == LogicalTypeId::FLOAT) {
        BinaryExecutor::Execute<double, string_t, string_t>(
            args.data[0], args.data[1], result, count,
            [&](double value, string_t spanset_blob) {
                return Tsequenceset_from_base_tstzspanset_impl(Float8GetDatum(value), spanset_blob, temptype, interp, result);
            });
    } else if (arg_type.id() == LogicalTypeId::BOOLEAN) {
        BinaryExecutor::Execute<bool, string_t, string_t>(
            args.data[0], args.data[1], result, count,
            [&](bool value, string_t spanset_blob) {
                return Tsequenceset_from_base_tstzspanset_impl((Datum)value, spanset_blob, temptype, interp, result);
            });
    } else if (arg_type.id() == LogicalTypeId::INTEGER || arg_type.id() == LogicalTypeId::BIGINT ||
               arg_type.id() == LogicalTypeId::SMALLINT || arg_type.id() == LogicalTypeId::TINYINT) {
        BinaryExecutor::Execute<int64_t, string_t, string_t>(
            args.data[0], args.data[1], result, count,
            [&](int64_t value, string_t spanset_blob) {
                return Tsequenceset_from_base_tstzspanset_impl((Datum)value, spanset_blob, temptype, interp, result);
            });
    } else {
        throw InvalidInputException("Invalid argument type for Tsequenceset_from_base_tstzspanset: " + arg_type.ToString());
    }

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}
/* ***************************************************
 * Conversion functions: [TYPE] -> Temporal
 ****************************************************/

void TemporalFunctions::Temporal_to_tstzspan(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_to_tstzspan] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_to_tstzspan: unable to cast string to temporal");
            }

            Span *ret = (Span*)malloc(sizeof(Span));
            temporal_set_tstzspan(temp, ret);
            size_t span_size = sizeof(*ret);
            uint8_t *span_buffer = (uint8_t*) malloc(span_size);
            memcpy(span_buffer, ret, span_size);
            string_t span_string_t((char *) span_buffer, span_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
            free(span_buffer);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Tnumber_to_span(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Tnumber_to_span] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Tnumber_to_span: unable to cast string to temporal");
            }

            Span *ret = tnumber_to_span(temp);
            size_t span_size = sizeof(*ret);
            uint8_t *span_buffer = (uint8_t*) malloc(span_size);
            memcpy(span_buffer, ret, span_size);
            string_t span_string_t((char *) span_buffer, span_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);
            free(span_buffer);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

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

void TemporalFunctions::Tbool_to_tint(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            return Tbool_to_tint_common(input, result);
        }
    );
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
void TemporalFunctions::Tint_to_tfloat(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            return Tint_to_tfloat_common(input, result);
        }
    );
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
void TemporalFunctions::Tfloat_to_tint(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            return Tfloat_to_tint_common(input, result);
        }
    );
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

static inline string_t Tnumber_to_tbox_common(Datum datum, meosType basetype, Vector &result) {
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

void TemporalFunctions::Tnumber_to_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    const auto &arg_type = args.data[0].GetType();
    if (arg_type.id() == LogicalTypeId::BLOB) {
        UnaryExecutor::Execute<string_t, string_t>(
            args.data[0], result, count,
            [&](string_t input) {
                return Tnumber_temporal_to_tbox_common(input, result);
            });
    } else if (arg_type.id() == LogicalTypeId::DOUBLE || arg_type.id() == LogicalTypeId::FLOAT) {
        meosType basetype = TemporalHelpers::GetTemptypeFromAlias(arg_type.GetAlias().c_str());
        UnaryExecutor::Execute<double, string_t>(
            args.data[0], result, count,
            [&](double value) {
                return Tnumber_to_tbox_common(Float8GetDatum(value), basetype, result);
            });
    } else if (arg_type.id() == LogicalTypeId::INTEGER || arg_type.id() == LogicalTypeId::BIGINT ||
               arg_type.id() == LogicalTypeId::SMALLINT || arg_type.id() == LogicalTypeId::TINYINT) {
        meosType basetype = TemporalHelpers::GetTemptypeFromAlias(arg_type.GetAlias().c_str());
        UnaryExecutor::Execute<int64_t, string_t>(
            args.data[0], result, count,
            [&](int64_t value) {
                return Tnumber_to_tbox_common((Datum)value, basetype, result);
            });
    } else {
        throw InvalidInputException("Invalid argument type for Tnumber_to_tbox: " + arg_type.ToString());
    }
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
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
        meosType basetype = TemporalHelpers::GetTemptypeFromAlias(source.GetType().GetAlias().c_str());
        UnaryExecutor::Execute<double, string_t>(
            source, result, count,
            [&](double value) {
                return Tnumber_to_tbox_common(Float8GetDatum(value), basetype, result);
            }
        );
    } else if (source.GetType().id() == LogicalTypeId::INTEGER || source.GetType().id() == LogicalTypeId::BIGINT ||
               source.GetType().id() == LogicalTypeId::SMALLINT || source.GetType().id() == LogicalTypeId::TINYINT) {
        meosType basetype = TemporalHelpers::GetTemptypeFromAlias(source.GetType().GetAlias().c_str());
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

bool TemporalFunctions::Temporal_enforce_typmod_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    source.Flatten(count);
    auto result_data = FlatVector::GetData<string_t>(result);
    for (idx_t i = 0; i < count; ++i) {
        if (FlatVector::IsNull(source, i)) {
            FlatVector::SetNull(result, i, true);
            continue;
        }
        Value val = source.GetValue(i);
        const string_t &blob = StringValue::Get(val);
        result_data[i] = StringVector::AddStringOrBlob(result, blob);
    }
    result.SetVectorType(VectorType::FLAT_VECTOR);
    return true;
}

/* ***************************************************
 * Accessor functions
 ****************************************************/

void TemporalFunctions::Temporal_subtype(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_subtype] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_subtype: unable to cast string to temporal");
            }
            tempSubtype subtype = (tempSubtype)temp->subtype;
            const char *str = tempsubtype_name(subtype);
            free(temp);
            return string_t(str);
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_interp(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_interp] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_interp: unable to cast string to temporal");
            }
            const char *str = temporal_interp(temp);
            free(temp);
            return string_t(str);
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_mem_size(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, int32_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_mem_size] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_mem_size: unable to cast string to temporal");
            }
            size_t mem_size = temporal_mem_size(temp);
            free(temp);
            return (int32_t)mem_size;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Tinstant_value(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, int64_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Tinstant_value] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Tinstant_value: unable to cast string to temporal");
            }
            Datum ret = tinstant_value((TInstant*)temp);
            free(temp);
            return (int64_t)ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_valueset(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_valueset] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_valueset: unable to cast string to temporal");
            }
            int32_t count;
            Datum *values = temporal_values_p(temp, &count);
            meosType basetype = temptype_basetype((meosType)temp->temptype);
            if (temp->temptype == T_TBOOL) {
                // TODO: handle tbool
            }
            Set *ret = set_make_free(values, count, basetype, false);
            size_t total_size = set_mem_size(ret);
            string_t blob = StringVector::AddStringOrBlob(result, (const char*)ret, total_size);        
            free(ret);
            return blob;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_start_value(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, int64_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_start_value] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_start_value: unable to cast string to temporal");
            }
        Datum ret = temporal_start_value(temp);
            free(temp);
            return (int64_t)ret;
    }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_end_value(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, int64_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_end_value] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_end_value: unable to cast string to temporal");
            }
        Datum ret = temporal_end_value(temp);
            free(temp);
            return (int64_t)ret;
    }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_min_value(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, int64_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_min_value] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_min_value: unable to cast string to temporal");
            }
        Datum ret = temporal_min_value(temp);
            free(temp);
            return (int64_t)ret;
    }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_max_value(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, int64_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_max_value] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_max_value: unable to cast string to temporal");
            }
        Datum ret = temporal_max_value(temp);
            free(temp);
            return (int64_t)ret;
    }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_value_n(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, int64_t, int64_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input, int64_t n, ValidityMask &mask, idx_t idx) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_value_n] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_value_n: unable to cast string to temporal");
            }
        Datum ret;
        bool found = temporal_value_n(temp, n, &ret);
        if (!found) {
                free(temp);
                mask.SetInvalid(idx);
                return int64_t();
            }
            free(temp);
            return (int64_t)ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_num_instants(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, int32_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_num_instants] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_num_instants: unable to cast string to temporal");
            }
            int32_t ret = temporal_num_instants(temp);
            free(temp);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_min_instant(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_min_instant] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_min_instant: unable to cast string to temporal");
            }
            TInstant *ret = temporal_min_instant(temp);
            size_t temp_size = temporal_mem_size((Temporal*)ret);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, (Temporal*)ret, temp_size);
            string_t ret_str(reinterpret_cast<const char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);

            free(temp_data);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_max_instant(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_max_instant] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_max_instant: unable to cast string to temporal");
            }
            TInstant *ret = temporal_max_instant(temp);
            size_t temp_size = temporal_mem_size((Temporal*)ret);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, (Temporal*)ret, temp_size);
            string_t ret_str(reinterpret_cast<const char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);

            free(temp_data);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Tinstant_timestamptz(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, timestamp_tz_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Tinstant_timestamptz] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in TInstantTimestamptz: unable to cast string to temporal");
            }
            timestamp_tz_t ret = (timestamp_tz_t)((TInstant*)temp)->t;
            timestamp_tz_t duckdb_ts = MeosToDuckDBTimestamp(ret);
            free(temp);
            return duckdb_ts;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_time(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_time] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_time: unable to cast string to temporal");
            }

            SpanSet *ret = temporal_time(temp);
            size_t spanset_size = spanset_mem_size(ret);
            uint8_t *spanset_buffer = (uint8_t*)malloc(spanset_size);
            memcpy(spanset_buffer, ret, spanset_size);
            string_t ret_str(reinterpret_cast<const char*>(spanset_buffer), spanset_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(spanset_buffer);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_duration(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, bool, interval_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input, bool boundspan) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_duration] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_duration: unable to cast string to temporal");
            }
            MeosInterval *ret = temporal_duration(temp, boundspan);
            interval_t duckdb_interval = IntervalToIntervalt(ret);
            free(ret);
            free(temp);
            return duckdb_interval;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_sequences(DataChunk &args, ExpressionState &state, Vector &result) {
    idx_t total_count = 0;
    UnaryExecutor::Execute<string_t, list_entry_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_sequences] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_sequences: unable to cast string to temporal");
            }
            int32_t seq_count;
            const TSequence **sequences = temporal_sequences_p(temp, &seq_count);
            if (seq_count == 0) {
                free(temp);
                return list_entry_t();
            }
            const auto entry = list_entry_t(total_count, seq_count);
            total_count += seq_count;
            ListVector::Reserve(result, total_count);

            auto &seq_vec = ListVector::GetEntry(result);
            const auto seq_data = FlatVector::GetData<string_t>(seq_vec);

            for (idx_t i = 0; i < seq_count; i++) {
                const TSequence *seq = sequences[i];
                size_t temp_size = temporal_mem_size((Temporal*)seq);
                uint8_t *temp_data = (uint8_t*)malloc(temp_size);
                memcpy(temp_data, (Temporal*)seq, temp_size);
                string_t ret_str(reinterpret_cast<const char*>(temp_data), temp_size);
                string_t stored = StringVector::AddStringOrBlob(seq_vec, ret_str);
                free(temp_data);
                seq_data[entry.offset + i] = stored;
            }
            free(temp);
            return entry;
        }
    );
    ListVector::SetListSize(result, total_count);
}

void TemporalFunctions::Temporal_start_timestamptz(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, timestamp_tz_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_start_timestamptz] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_start_timestamptz: unable to cast string to temporal");
            }
            TimestampTz ret_meos = temporal_start_timestamptz(temp);
            timestamp_tz_t ret = MeosToDuckDBTimestamp((timestamp_tz_t)ret_meos);
            free(temp);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_end_timestamptz(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, timestamp_tz_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_end_timestamptz] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_end_timestamptz: unable to cast string to temporal");
            }
            TimestampTz ret_meos = temporal_end_timestamptz(temp);
            timestamp_tz_t ret = MeosToDuckDBTimestamp((timestamp_tz_t)ret_meos);
            free(temp);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_timestamps(DataChunk &args, ExpressionState &state, Vector &result) {
    idx_t total_count = 0;
    UnaryExecutor::Execute<string_t, list_entry_t>(
        args.data[0], result, args.size(),
        [&](string_t temp_str) -> list_entry_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_timestamps] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_timestamps: unable to cast string to temporal");
            }

            int ts_count;
            TimestampTz *times = temporal_timestamps(temp, &ts_count);
            timestamp_tz_t *times_duckdb = (timestamp_tz_t*)malloc(ts_count * sizeof(timestamp_tz_t));
            for (idx_t i = 0; i < ts_count; i++) {
                times_duckdb[i] = MeosToDuckDBTimestamp((timestamp_tz_t)times[i]);
            }
            const auto entry = list_entry_t(total_count, ts_count);
            total_count += ts_count;
            ListVector::Reserve(result, total_count);

            auto &ts_vec = ListVector::GetEntry(result);
            const auto ts_data = FlatVector::GetData<timestamp_tz_t>(ts_vec);

            for (idx_t i = 0; i < ts_count; i++) {
                ts_data[entry.offset + i] = times_duckdb[i];
            }

            free(times);
            free(times_duckdb);
            free(temp);
            return entry;
        }
    );
    ListVector::SetListSize(result, total_count);
}

void TemporalFunctions::Temporal_instants(DataChunk &args, ExpressionState &state, Vector &result) {
    idx_t total_count = 0;
    UnaryExecutor::Execute<string_t, list_entry_t>(
        args.data[0], result, args.size(),
        [&](string_t temp_str) -> list_entry_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_instants] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_instants: unable to cast string to temporal");
            }

            int inst_count;
            const TInstant **instants = temporal_insts_p(temp, &inst_count);
            const auto entry = list_entry_t(total_count, inst_count);
            total_count += inst_count;
            ListVector::Reserve(result, total_count);

            auto &inst_vec = ListVector::GetEntry(result);
            const auto inst_data = FlatVector::GetData<string_t>(inst_vec);

            for (idx_t i = 0; i < inst_count; i++) {
                const TInstant *inst = instants[i];
                size_t temp_size = temporal_mem_size((Temporal*)inst);
                uint8_t *temp_data = (uint8_t*)malloc(temp_size);
                memcpy(temp_data, (Temporal*)inst, temp_size);
                string_t ret_str(reinterpret_cast<const char*>(temp_data), temp_size);
                string_t stored = StringVector::AddStringOrBlob(inst_vec, ret_str);
                free(temp_data);
                inst_data[entry.offset + i] = stored;
            }
            free(instants);
            free(temp);
            return entry;
        }
    );
    ListVector::SetListSize(result, total_count);
}

void TemporalFunctions::Temporal_num_sequences(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, int32_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_num_sequences] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_num_sequences: unable to cast string to temporal");
            }
            int32_t ret = temporal_num_sequences(temp);
            free(temp);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_lower_inc(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, bool>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_lower_inc] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_lower_inc: unable to cast string to temporal");
            }
            bool ret = temporal_lower_inc(temp);
            free(temp);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_upper_inc(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, bool>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_upper_inc] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_upper_inc: unable to cast string to temporal");
            }
            bool ret = temporal_upper_inc(temp);
            free(temp);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_start_instant(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_start_instant] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_start_instant: unable to cast string to temporal");
            }
            TInstant *ret = temporal_start_instant(temp);
            size_t temp_size = temporal_mem_size((Temporal*)ret);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, (Temporal*)ret, temp_size);
            string_t ret_str(reinterpret_cast<const char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(temp_data);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_end_instant(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_start_instant] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_start_instant: unable to cast string to temporal");
            }
            TInstant *ret = temporal_end_instant(temp);
            size_t temp_size = temporal_mem_size((Temporal*)ret);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, (Temporal*)ret, temp_size);
            string_t ret_str(reinterpret_cast<const char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(temp_data);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_instant_n(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, int32_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input, int32_t n) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_start_value] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_start_value: unable to cast string to temporal");
            }
            TInstant *ret = temporal_instant_n(temp, n);
            size_t temp_size = temporal_mem_size((Temporal*)ret);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, (Temporal*)ret, temp_size);
            string_t ret_str(reinterpret_cast<const char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(temp_data);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_num_timestamps(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, int32_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_num_timestamps] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_num_timestamps: unable to cast string to temporal");
            }
            int32_t ret = temporal_num_timestamps(temp);
            free(temp);
            return ret;
        }    
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


void TemporalFunctions::Temporal_timestamptz_n(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, int32_t, timestamp_tz_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input, int32_t n, ValidityMask &mask, idx_t idx) -> timestamp_tz_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_timestamptz_n] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_timestamptz_n: unable to cast string to temporal");
            }
            TimestampTz ret_meos;
            if (!temporal_timestamptz_n(temp, n, &ret_meos)) {
                free(temp);
                mask.SetInvalid(idx);
                return timestamp_tz_t();
            }
            timestamp_tz_t ret = MeosToDuckDBTimestamp((timestamp_tz_t)ret_meos);
            free(temp);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_start_sequence(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_start_sequence] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_start_sequence: unable to cast string to temporal");
            }
            TSequence *ret = temporal_start_sequence(temp);
            size_t temp_size = temporal_mem_size((Temporal*)ret);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, (Temporal*)ret, temp_size);
            string_t ret_str(reinterpret_cast<const char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(temp_data);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


void TemporalFunctions::Temporal_end_sequence(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_start_sequence] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_start_sequence: unable to cast string to temporal");
            }
            TSequence *ret = temporal_end_sequence(temp);
            size_t temp_size = temporal_mem_size((Temporal*)ret);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, (Temporal*)ret, temp_size);
            string_t ret_str(reinterpret_cast<const char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(temp_data);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_sequence_n(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, int32_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input, int32_t n) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_sequence_n] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_sequence_n: unable to cast string to temporal");
            }
            TSequence *ret = temporal_sequence_n(temp, n);
            size_t temp_size = temporal_mem_size((Temporal*)ret);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, (Temporal*)ret, temp_size);
            string_t ret_str(reinterpret_cast<const char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(temp_data);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_segments(DataChunk &args, ExpressionState &state, Vector &result) {
    idx_t total_count = 0;
    UnaryExecutor::Execute<string_t, list_entry_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_segments] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_segments: unable to cast string to temporal");
            }

            int32_t seg_count;
            TSequence **segments = temporal_segments(temp, &seg_count);
            if (seg_count == 0 || !segments) {
                free(data_copy);
                return list_entry_t();
            }

            const auto entry = list_entry_t(total_count, seg_count);
            total_count += seg_count;
            ListVector::Reserve(result, total_count);

            auto &seg_vec = ListVector::GetEntry(result);
            auto seg_data = FlatVector::GetData<string_t>(seg_vec);

            for (idx_t i = 0; i < (idx_t)seg_count; i++) {
                TSequence *seg = segments[i];
                size_t seg_size = temporal_mem_size((Temporal*)seg);
                uint8_t *seg_buf = (uint8_t*)malloc(seg_size);
                memcpy(seg_buf, (Temporal*)seg, seg_size);
                string_t ret_str(reinterpret_cast<const char*>(seg_buf), seg_size);
                string_t stored = StringVector::AddStringOrBlob(seg_vec, ret_str);
                free(seg_buf);
                free(seg);
                seg_data[entry.offset + i] = stored;
            }
            free(segments);
            free(data_copy);
            return entry;
        }
    );
    ListVector::SetListSize(result, total_count);
}

// shift, scale 
void TemporalFunctions::Temporal_shift_time(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, interval_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_temporal, interval_t interval, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_temporal.GetData());
            size_t data_size = input_temporal.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_shift_time] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_shift_time: unable to cast binary to temporal");
            }
            MeosInterval shift = IntervaltToInterval(interval);
            Temporal *ret = temporal_shift_scale_time(temp, &shift, NULL);
            if (!ret) {
                free(temp);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_scale_time(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, interval_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_temporal, interval_t interval, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_temporal.GetData());
            size_t data_size = input_temporal.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_scale_time] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_scale_time: unable to cast binary to temporal");
            }
            MeosInterval duration = IntervaltToInterval(interval);
            Temporal *ret = temporal_shift_scale_time(temp, NULL, &duration);
            if (!ret) {
                free(temp);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_shift_scale_time(DataChunk &args, ExpressionState &state, Vector &result) {
    TernaryExecutor::Execute<string_t, interval_t, interval_t, string_t>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t input_temporal, interval_t duckdb_shift, interval_t duckdb_duration) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_temporal.GetData());
            size_t data_size = input_temporal.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_shift_scale_time] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_shift_scale_time: unable to cast binary to temporal");
            }
            MeosInterval shift = IntervaltToInterval(duckdb_shift);
            MeosInterval duration = IntervaltToInterval(duckdb_duration);
            Temporal *ret = temporal_shift_scale_time(temp, &shift, &duration);
            if (!ret) {
                free(temp);
                throw InternalException("Failure in Temporal_shift_scale_time: temporal_shift_scale_time returned null");
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


/* ***************************************************
 * Transformation functions
 ****************************************************/

void TemporalFunctions::Temporal_to_tinstant(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_to_tinstant] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_to_tinstant: unable to cast string to temporal");
            }
            TInstant *ret = temporal_to_tinstant(temp);
            if (!ret) {
                free(data_copy);
                throw InternalException("Failure in Temporal_to_tinstant: conversion failed");
            }
            size_t ret_size = temporal_mem_size((Temporal*)ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, (Temporal*)ret, ret_size);
            string_t result_str(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, result_str);
            free(ret_data);
            free(ret);
            free(data_copy);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_to_tsequence(DataChunk &args, ExpressionState &state, Vector &result) {
    interpType interp = INTERP_NONE;
    if (args.ColumnCount() > 1) {
        auto &interp_child = args.data[1];
        interp_child.Flatten(args.size());
        auto interp_str = interp_child.GetValue(0).ToString();
        interp = interptype_from_string(interp_str.c_str());
    }

    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_to_tsequence] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_to_tsequence: unable to cast string to temporal");
            }
            TSequence *ret = temporal_to_tsequence(temp, interp);
            size_t temp_size = temporal_mem_size((Temporal*)ret);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, (Temporal*)ret, temp_size);
            string_t result_str(reinterpret_cast<const char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, result_str);

            free(temp_data);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_to_tsequenceset(DataChunk &args, ExpressionState &state, Vector &result) {
    interpType interp = INTERP_NONE;
    if (args.ColumnCount() > 1) {
        auto &interp_child = args.data[1];
        interp_child.Flatten(args.size());
        auto interp_str = interp_child.GetValue(0).ToString();
        interp = interptype_from_string(interp_str.c_str());
    }

    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_to_tsequenceset] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_to_tsequenceset: unable to cast string to temporal");
            }
            TSequenceSet *ret = temporal_to_tsequenceset(temp, interp);
            size_t temp_size = temporal_mem_size((Temporal*)ret);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, (Temporal*)ret, temp_size);
            string_t result_str(reinterpret_cast<const char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, result_str);

            free(temp_data);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_set_interp(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &interp_child = args.data[1];
    interp_child.Flatten(count);
    auto interp_str = interp_child.GetValue(0).ToString();
    interpType interp = interptype_from_string(interp_str.c_str());

    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, count,
        [&](string_t input) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_set_interp] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_set_interp: unable to cast string to temporal");
            }
            Temporal *ret = temporal_set_interp(temp, interp);
            if (!ret) {
                free(data_copy);
                throw InternalException("Failure in Temporal_set_interp: conversion failed");
            }
            size_t ret_size = temporal_mem_size(ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            string_t result_str(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, result_str);
            free(ret_data);
            free(ret);
            free(data_copy);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_append_tinstant(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    meosType temptype = TemporalHelpers::GetTemptypeFromAlias(result.GetType().GetAlias().c_str());
    interpType interp = temptype_continuous(temptype) ? LINEAR : STEP;
    if (args.ColumnCount() > 2) {
        auto &interp_child = args.data[2];
        interp_child.Flatten(count);
        auto interp_str = interp_child.GetValue(0).ToString();
        interp = interptype_from_string(interp_str.c_str());
    }

    BinaryExecutor::Execute<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, count,
        [&](string_t input_temp, string_t input_inst) {
            size_t temp_size = input_temp.GetSize();
            if (temp_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_append_tinstant] Invalid Temporal data: insufficient size");
            }
            uint8_t *temp_copy = (uint8_t*)malloc(temp_size);
            memcpy(temp_copy, input_temp.GetData(), temp_size);
            Temporal *temp = reinterpret_cast<Temporal*>(temp_copy);

            size_t inst_size = input_inst.GetSize();
            if (inst_size < sizeof(void*)) {
                free(temp_copy);
                throw InvalidInputException("[Temporal_append_tinstant] Invalid TInstant data: insufficient size");
            }
            uint8_t *inst_copy = (uint8_t*)malloc(inst_size);
            memcpy(inst_copy, input_inst.GetData(), inst_size);
            TInstant *inst = reinterpret_cast<TInstant*>(inst_copy);

            Temporal *ret = temporal_append_tinstant(temp, inst, interp, 0.0, NULL, false);
            if (!ret) {
                free(inst_copy);
                free(temp_copy);
                throw InternalException("Failure in Temporal_append_tinstant: append failed");
            }

            size_t ret_size = temporal_mem_size(ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            string_t result_str(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, result_str);
            free(ret_data);
            free(ret);
            free(inst_copy);
            free(temp_copy);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_append_tsequence(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    BinaryExecutor::Execute<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, count,
        [&](string_t input_temp, string_t input_seq) {
            size_t temp_size = input_temp.GetSize();
            if (temp_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_append_tsequence] Invalid Temporal data: insufficient size");
            }
            uint8_t *temp_copy = (uint8_t*)malloc(temp_size);
            memcpy(temp_copy, input_temp.GetData(), temp_size);
            Temporal *temp = reinterpret_cast<Temporal*>(temp_copy);

            size_t seq_size = input_seq.GetSize();
            if (seq_size < sizeof(void*)) {
                free(temp_copy);
                throw InvalidInputException("[Temporal_append_tsequence] Invalid TSequence data: insufficient size");
            }
            uint8_t *seq_copy = (uint8_t*)malloc(seq_size);
            memcpy(seq_copy, input_seq.GetData(), seq_size);
            TSequence *seq = reinterpret_cast<TSequence*>(seq_copy);

            Temporal *ret = temporal_append_tsequence(temp, seq, false);
            if (!ret) {
                free(seq_copy);
                free(temp_copy);
                throw InternalException("Failure in Temporal_append_tsequence: append failed");
            }

            size_t ret_size = temporal_mem_size(ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            string_t result_str(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, result_str);
            free(ret_data);
            free(ret);
            free(seq_copy);
            free(temp_copy);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_merge(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, count,
        [&](string_t input1, string_t input2, ValidityMask &mask, idx_t idx) -> string_t {
            size_t size1 = input1.GetSize();
            size_t size2 = input2.GetSize();
            if (size1 < sizeof(void*) || size2 < sizeof(void*)) {
                throw InvalidInputException("[Temporal_merge] Invalid Temporal data: insufficient size");
            }
            uint8_t *copy1 = (uint8_t*)malloc(size1);
            memcpy(copy1, input1.GetData(), size1);
            Temporal *temp1 = reinterpret_cast<Temporal*>(copy1);

            uint8_t *copy2 = (uint8_t*)malloc(size2);
            memcpy(copy2, input2.GetData(), size2);
            Temporal *temp2 = reinterpret_cast<Temporal*>(copy2);

            Temporal *ret = temporal_merge(temp1, temp2);
            if (!ret) {
                free(copy2);
                free(copy1);
                mask.SetInvalid(idx);
                return string_t();
            }

            size_t ret_size = temporal_mem_size(ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            string_t result_str(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, result_str);
            free(ret_data);
            free(ret);
            free(copy2);
            free(copy1);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_merge_array(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &array_vec = args.data[0];
    array_vec.Flatten(count);
    auto &child_vec = ListVector::GetEntry(array_vec);
    child_vec.Flatten(ListVector::GetListSize(array_vec));
    auto child_data = FlatVector::GetData<string_t>(child_vec);

    UnaryExecutor::Execute<list_entry_t, string_t>(
        array_vec, result, count,
        [&](const list_entry_t &list) {
            auto offset = list.offset;
            auto length = list.length;
            if (length == 0) {
                throw InvalidInputException("[Temporal_merge_array] Empty array");
            }

            Temporal **temparr = (Temporal **)malloc(length * sizeof(Temporal *));
            for (idx_t i = 0; i < length; i++) {
                auto blob = child_data[offset + i];
                size_t blob_size = blob.GetSize();
                if (blob_size < sizeof(void*)) {
                    for (idx_t j = 0; j < i; j++) free(temparr[j]);
                    free(temparr);
                    throw InvalidInputException("[Temporal_merge_array] Invalid Temporal data: insufficient size");
                }
                uint8_t *data_copy = (uint8_t*)malloc(blob_size);
                memcpy(data_copy, blob.GetData(), blob_size);
                temparr[i] = reinterpret_cast<Temporal*>(data_copy);
            }

            Temporal *ret = temporal_merge_array(temparr, (int)length);
            if (!ret) {
                for (idx_t j = 0; j < length; j++) free(temparr[j]);
                free(temparr);
                throw InternalException("Failure in Temporal_merge_array: merge failed");
            }

            size_t ret_size = temporal_mem_size(ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            string_t result_str(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, result_str);

            free(ret_data);
            free(ret);
            for (idx_t j = 0; j < length; j++) free(temparr[j]);
            free(temparr);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Tnumber_shift_value(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, int64_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input, int64_t shift) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Tnumber_shift_value] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Tnumber_shift_value: unable to cast string to temporal");
            }
            Temporal *ret = tnumber_shift_scale_value(temp, shift, 0, true, false);
            size_t temp_size = temporal_mem_size((Temporal*)ret);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, ret, temp_size);
            string_t ret_str(reinterpret_cast<const char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);

            free(temp_data);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Tnumber_scale_value(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, int64_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input, int64_t duration) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Tnumber_scale_value] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Tnumber_scale_value: unable to cast string to temporal");
            }
            Temporal *ret = tnumber_shift_scale_value(temp, 0, duration, false, true);
            size_t temp_size = temporal_mem_size((Temporal*)ret);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, ret, temp_size);
            string_t ret_str(reinterpret_cast<const char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);

            free(temp_data);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Tnumber_shift_scale_value(DataChunk &args, ExpressionState &state, Vector &result) {
    TernaryExecutor::Execute<string_t, int64_t, int64_t, string_t>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t input, int64_t shift, int64_t duration) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Tnumber_shift_scale_value] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Tnumber_shift_scale_value: unable to cast string to temporal");
            }
            Temporal *ret = tnumber_shift_scale_value(temp, shift, duration, true, true);
            size_t temp_size = temporal_mem_size(ret);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, ret, temp_size);
            string_t ret_str(reinterpret_cast<const char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);

            free(temp_data);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Restriction functions
 ****************************************************/

static string_t temporal_restrict_value_impl(string_t temp_str, Datum value, bool atfunc, Vector &result, ValidityMask &mask, idx_t idx) {
    size_t data_size = temp_str.GetSize();
    if (data_size < sizeof(void*)) {
        throw InvalidInputException("[temporal_restrict_value] Invalid Temporal data: insufficient size");
    }
    uint8_t *data_copy = (uint8_t*)malloc(data_size);
    memcpy(data_copy, temp_str.GetData(), data_size);
    Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

    Temporal *ret = temporal_restrict_value(temp, value, atfunc);
    if (!ret) {
        free(data_copy);
        mask.SetInvalid(idx);
        return string_t();
    }
    size_t ret_size = temporal_mem_size(ret);
    string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
    string_t stored = StringVector::AddStringOrBlob(result, ret_str);
    free(ret);
    free(data_copy);
    return stored;
}

static string_t temporal_restrict_values_impl(string_t temp_str, string_t set_str, bool atfunc, Vector &result, ValidityMask &mask, idx_t idx) {
    size_t data_size = temp_str.GetSize();
    if (data_size < sizeof(void*)) {
        throw InvalidInputException("[temporal_restrict_values] Invalid Temporal data: insufficient size");
    }
    uint8_t *data_copy = (uint8_t*)malloc(data_size);
    memcpy(data_copy, temp_str.GetData(), data_size);
    Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

    size_t set_size = set_str.GetSize();
    if (set_size < sizeof(void*)) {
        free(data_copy);
        throw InvalidInputException("[temporal_restrict_values] Invalid Set data: insufficient size");
    }
    uint8_t *set_copy = (uint8_t*)malloc(set_size);
    memcpy(set_copy, set_str.GetData(), set_size);
    Set *s = reinterpret_cast<Set*>(set_copy);

    Temporal *ret = temporal_restrict_values(temp, s, atfunc);
    if (!ret) {
        free(set_copy);
        free(data_copy);
        mask.SetInvalid(idx);
        return string_t();
    }
    size_t ret_size = temporal_mem_size(ret);
    string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
    string_t stored = StringVector::AddStringOrBlob(result, ret_str);
    free(ret);
    free(set_copy);
    free(data_copy);
    return stored;
}

static void temporal_at_minus_values_dispatch(DataChunk &args, ExpressionState &state, Vector &result, bool atfunc) {
    auto count = args.size();
    auto &val_type = args.data[1].GetType();

    if (val_type.id() == LogicalTypeId::BIGINT || val_type.id() == LogicalTypeId::INTEGER ||
        val_type.id() == LogicalTypeId::SMALLINT || val_type.id() == LogicalTypeId::TINYINT) {
        BinaryExecutor::ExecuteWithNulls<string_t, int64_t, string_t>(
            args.data[0], args.data[1], result, count,
            [&](string_t temp_str, int64_t value, ValidityMask &mask, idx_t idx) -> string_t {
                return temporal_restrict_value_impl(temp_str, Int32GetDatum((int32_t)value), atfunc, result, mask, idx);
            });
    } else if (val_type.id() == LogicalTypeId::DOUBLE || val_type.id() == LogicalTypeId::FLOAT) {
        BinaryExecutor::ExecuteWithNulls<string_t, double, string_t>(
            args.data[0], args.data[1], result, count,
            [&](string_t temp_str, double value, ValidityMask &mask, idx_t idx) -> string_t {
                return temporal_restrict_value_impl(temp_str, Float8GetDatum(value), atfunc, result, mask, idx);
            });
    } else if (val_type.id() == LogicalTypeId::VARCHAR) {
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
            args.data[0], args.data[1], result, count,
            [&](string_t temp_str, string_t value, ValidityMask &mask, idx_t idx) -> string_t {
                text *txt = cstring2text(value.GetString().c_str());
                string_t stored = temporal_restrict_value_impl(temp_str, PointerGetDatum(txt), atfunc, result, mask, idx);
                return stored;
            });
    } else if (val_type.id() == LogicalTypeId::BOOLEAN){
        BinaryExecutor::ExecuteWithNulls<string_t, bool, string_t>(
            args.data[0], args.data[1], result, count,
            [&](string_t temp_str, bool value, ValidityMask &mask, idx_t idx) -> string_t {
                string_t stored = temporal_restrict_value_impl(temp_str, Datum(value), atfunc, result, mask, idx);
                return stored;
            });
    }
    else if (val_type.id() == LogicalTypeId::BLOB) { // for set 
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
            args.data[0], args.data[1], result, count,
            [&](string_t temp_str, string_t set_str, ValidityMask &mask, idx_t idx) -> string_t {
                return temporal_restrict_values_impl(temp_str, set_str, atfunc, result, mask, idx);
            });
    } else {
        throw InvalidInputException("Invalid argument type for atValues/minusValues: " + val_type.ToString());
    }

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_at_value(DataChunk &args, ExpressionState &state, Vector &result) {
    temporal_at_minus_values_dispatch(args, state, result, true);
}

void TemporalFunctions::Temporal_at_values(DataChunk &args, ExpressionState &state, Vector &result) {
    temporal_at_minus_values_dispatch(args, state, result, true);
}

void TemporalFunctions::Temporal_minus_value(DataChunk &args, ExpressionState &state, Vector &result) {
    temporal_at_minus_values_dispatch(args, state, result, false);
}

void TemporalFunctions::Temporal_at_timestamptz(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t temp_str, timestamp_tz_t ts, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_at_timestamptz] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_at_timestamptz: unable to cast string to temporal");
            }

            timestamp_tz_t meos_ts = DuckDBToMeosTimestamp(ts);
            Temporal *ret = temporal_restrict_timestamptz(temp, (TimestampTz)meos_ts.value, true);
            if (!ret) {
                free(temp);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t temp_size = temporal_mem_size(ret);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, ret, temp_size);
            string_t ret_str(reinterpret_cast<const char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            
            free(temp_data);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_at_tstzspan(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t temp_str, string_t span_str, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_at_tstzspan] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_at_tstzspan: unable to cast string to temporal");
            }

            Span *span = nullptr;
            if (span_str.GetSize() > 0) {
                span = (Span*)malloc(span_str.GetSize());
                memcpy(span, span_str.GetData(), span_str.GetSize());
            }
            if (!span) {
                free(temp);
                throw InternalException("Failure in TemporalAtTstzspan: unable to cast string to span");
            }

            Temporal *ret = temporal_restrict_tstzspan(temp, span, true);
            if (!ret) {
                free(temp);
                free(span);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t temp_size = temporal_mem_size(ret);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, ret, temp_size);
            string_t ret_str(reinterpret_cast<const char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            
            free(temp_data);
            free(ret);
            free(span);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_at_tstzspanset(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t temp_str, string_t spanset_str, ValidityMask &mask, idx_t idx) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_at_tstzspanset] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_at_tstzspanset: unable to cast string to temporal");
            }

            SpanSet *spanset = nullptr;
            if (spanset_str.GetSize() > 0) {
                spanset = (SpanSet*)malloc(spanset_str.GetSize());
                memcpy(spanset, spanset_str.GetData(), spanset_str.GetSize());
            }
            if (!spanset) {
                free(temp);
                throw InternalException("Failure in TemporalAtTstzspanset: unable to cast string to spanset");
            }

            Temporal *ret = temporal_restrict_tstzspanset(temp, spanset, true);
            if (!ret) {
                free(temp);
                free(spanset);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t temp_size = temporal_mem_size(ret);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, ret, temp_size);
            string_t ret_str(reinterpret_cast<const char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);

            free(temp_data);
            free(ret);
            free(spanset);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Tnumber_at_span(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t temp_str, string_t span_str, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Tnumber_at_span] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Tnumber_at_span: unable to cast string to temporal");
            }

            Span *span = nullptr;
            if (span_str.GetSize() > 0) {
                span = (Span*)malloc(span_str.GetSize());
                memcpy(span, span_str.GetData(), span_str.GetSize());
            }
            if (!span) {
                free(temp);
                throw InternalException("Failure in Tnumber_at_span: unable to cast string to span");
            }

            Temporal *ret = tnumber_at_span(temp, span);
            if (!ret) {
                free(temp);
                free(span);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t temp_size = temporal_mem_size(ret);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, ret, temp_size);
            string_t ret_str(reinterpret_cast<const char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);

            free(temp_data);
            free(ret);
            free(span);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_at_min(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t temp_str, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_at_min] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_at_min: unable to cast string to temporal");
            }

            Temporal *ret = temporal_at_min(temp);
            if (!ret) {
                free(temp);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t temp_size = temporal_mem_size((Temporal*)ret);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, ret, temp_size);
            string_t ret_str(reinterpret_cast<const char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);

            free(temp_data);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_minus_min(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t temp_str, ValidityMask &mask, idx_t idx) -> string_t {
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_minus_min] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, temp_str.GetData(), data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            Temporal *ret = temporal_minus_min(temp);
            if (!ret) {
                free(data_copy);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(data_copy);
            return stored;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_at_max(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t temp_str, ValidityMask &mask, idx_t idx) -> string_t {
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_at_max] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, temp_str.GetData(), data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            Temporal *ret = temporal_at_max(temp);
            if (!ret) {
                free(data_copy);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(data_copy);
            return stored;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_minus_max(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t temp_str, ValidityMask &mask, idx_t idx) -> string_t {
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_minus_max] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, temp_str.GetData(), data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            Temporal *ret = temporal_minus_max(temp);
            if (!ret) {
                free(data_copy);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(data_copy);
            return stored;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Tnumber_minus_span(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t temp_str, string_t span_str, ValidityMask &mask, idx_t idx) -> string_t {
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Tnumber_minus_span] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, temp_str.GetData(), data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            Span *span = nullptr;
            if (span_str.GetSize() > 0) {
                span = (Span*)malloc(span_str.GetSize());
                memcpy(span, span_str.GetData(), span_str.GetSize());
            }
            if (!span) {
                free(data_copy);
                throw InternalException("Failure in Tnumber_minus_span: unable to cast to span");
            }

            Temporal *ret = tnumber_minus_span(temp, span);
            if (!ret) {
                free(span);
                free(data_copy);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(span);
            free(data_copy);
            return stored;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Tnumber_at_spanset(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t temp_str, string_t spanset_str, ValidityMask &mask, idx_t idx) -> string_t {
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Tnumber_at_spanset] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, temp_str.GetData(), data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            SpanSet *ss = nullptr;
            if (spanset_str.GetSize() > 0) {
                ss = (SpanSet*)malloc(spanset_str.GetSize());
                memcpy(ss, spanset_str.GetData(), spanset_str.GetSize());
            }
            if (!ss) {
                free(data_copy);
                throw InternalException("Failure in Tnumber_at_spanset: unable to cast to spanset");
            }

            Temporal *ret = tnumber_at_spanset(temp, ss);
            if (!ret) {
                free(ss);
                free(data_copy);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(ss);
            free(data_copy);
            return stored;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Tnumber_minus_spanset(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t temp_str, string_t spanset_str, ValidityMask &mask, idx_t idx) -> string_t {
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Tnumber_minus_spanset] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, temp_str.GetData(), data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            SpanSet *ss = nullptr;
            if (spanset_str.GetSize() > 0) {
                ss = (SpanSet*)malloc(spanset_str.GetSize());
                memcpy(ss, spanset_str.GetData(), spanset_str.GetSize());
            }
            if (!ss) {
                free(data_copy);
                throw InternalException("Failure in Tnumber_minus_spanset: unable to cast to spanset");
            }

            Temporal *ret = tnumber_minus_spanset(temp, ss);
            if (!ret) {
                free(ss);
                free(data_copy);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(ss);
            free(data_copy);
            return stored;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Tnumber_at_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t temp_str, string_t tbox_str, ValidityMask &mask, idx_t idx) -> string_t {
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Tnumber_at_tbox] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, temp_str.GetData(), data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            if (tbox_str.GetSize() < sizeof(TBox)) {
                free(data_copy);
                throw InvalidInputException("[Tnumber_at_tbox] Invalid TBox data: insufficient size");
            }
            TBox *box = (TBox*)malloc(sizeof(TBox));
            memcpy(box, tbox_str.GetData(), sizeof(TBox));

            Temporal *ret = tnumber_at_tbox(temp, box);
            if (!ret) {
                free(box);
                free(data_copy);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(box);
            free(data_copy);
            return stored;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Tnumber_minus_tbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t temp_str, string_t tbox_str, ValidityMask &mask, idx_t idx) -> string_t {
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Tnumber_minus_tbox] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, temp_str.GetData(), data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            if (tbox_str.GetSize() < sizeof(TBox)) {
                free(data_copy);
                throw InvalidInputException("[Tnumber_minus_tbox] Invalid TBox data: insufficient size");
            }
            TBox *box = (TBox*)malloc(sizeof(TBox));
            memcpy(box, tbox_str.GetData(), sizeof(TBox));

            Temporal *ret = tnumber_minus_tbox(temp, box);
            if (!ret) {
                free(box);
                free(data_copy);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(box);
            free(data_copy);
            return stored;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_minus_timestamptz(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t temp_str, timestamp_tz_t ts, ValidityMask &mask, idx_t idx) -> string_t {
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_minus_timestamptz] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, temp_str.GetData(), data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            timestamp_tz_t meos_ts = DuckDBToMeosTimestamp(ts);
            Temporal *ret = temporal_minus_timestamptz(temp, (TimestampTz)meos_ts.value);
            if (!ret) {
                free(data_copy);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(data_copy);
            return stored;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_value_at_timestamptz(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &res_type = result.GetType();

    if (res_type.id() == LogicalTypeId::BOOLEAN) {
        BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, bool>(
            args.data[0], args.data[1], result, count,
            [&](string_t temp_str, timestamp_tz_t ts, ValidityMask &mask, idx_t idx) -> bool {
                size_t data_size = temp_str.GetSize();
                if (data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_value_at_timestamptz] Invalid Temporal data");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, temp_str.GetData(), data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

                timestamp_tz_t meos_ts = DuckDBToMeosTimestamp(ts);
                bool value;
                bool found = tbool_value_at_timestamptz(temp, (TimestampTz)meos_ts.value, true, &value);
                free(data_copy);
                if (!found) {
                    mask.SetInvalid(idx);
                    return false;
                }
                return value;
            });
    } else if (res_type.id() == LogicalTypeId::BIGINT) {
        BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, int64_t>(
            args.data[0], args.data[1], result, count,
            [&](string_t temp_str, timestamp_tz_t ts, ValidityMask &mask, idx_t idx) -> int64_t {
                size_t data_size = temp_str.GetSize();
                if (data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_value_at_timestamptz] Invalid Temporal data");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, temp_str.GetData(), data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

                timestamp_tz_t meos_ts = DuckDBToMeosTimestamp(ts);
                int value;
                bool found = tint_value_at_timestamptz(temp, (TimestampTz)meos_ts.value, true, &value);
                free(data_copy);
                if (!found) {
                    mask.SetInvalid(idx);
                    return 0;
                }
                return (int64_t)value;
            });
    } else if (res_type.id() == LogicalTypeId::DOUBLE) {
        BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, double>(
            args.data[0], args.data[1], result, count,
            [&](string_t temp_str, timestamp_tz_t ts, ValidityMask &mask, idx_t idx) -> double {
                size_t data_size = temp_str.GetSize();
                if (data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_value_at_timestamptz] Invalid Temporal data");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, temp_str.GetData(), data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

                timestamp_tz_t meos_ts = DuckDBToMeosTimestamp(ts);
                double value;
                bool found = tfloat_value_at_timestamptz(temp, (TimestampTz)meos_ts.value, true, &value);
                free(data_copy);
                if (!found) {
                    mask.SetInvalid(idx);
                    return 0.0;
                }
                return value;
            });
    } else if (res_type.id() == LogicalTypeId::VARCHAR) {
        BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, string_t>(
            args.data[0], args.data[1], result, count,
            [&](string_t temp_str, timestamp_tz_t ts, ValidityMask &mask, idx_t idx) -> string_t {
                size_t data_size = temp_str.GetSize();
                if (data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_value_at_timestamptz] Invalid Temporal data");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, temp_str.GetData(), data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

                timestamp_tz_t meos_ts = DuckDBToMeosTimestamp(ts);
                text *value = nullptr;
                bool found = ttext_value_at_timestamptz(temp, (TimestampTz)meos_ts.value, true, &value);
                free(data_copy);
                if (!found || !value) {
                    mask.SetInvalid(idx);
                    return string_t();
                }
                char *cstr = text2cstring(value);
                string_t stored = StringVector::AddString(result, cstr);
                return stored;
            });
    } else {
        throw InvalidInputException("Unsupported result type for valueAtTimestamp");
    }

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_at_tstzset(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t temp_str, string_t set_str, ValidityMask &mask, idx_t idx) -> string_t {
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_at_tstzset] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, temp_str.GetData(), data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            size_t set_size = set_str.GetSize();
            if (set_size < sizeof(void*)) {
                free(data_copy);
                throw InvalidInputException("[Temporal_at_tstzset] Invalid Set data: insufficient size");
            }
            uint8_t *set_copy = (uint8_t*)malloc(set_size);
            memcpy(set_copy, set_str.GetData(), set_size);
            Set *s = reinterpret_cast<Set*>(set_copy);

            Temporal *ret = temporal_at_tstzset(temp, s);
            if (!ret) {
                free(set_copy);
                free(data_copy);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(set_copy);
            free(data_copy);
            return stored;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_minus_tstzset(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t temp_str, string_t set_str, ValidityMask &mask, idx_t idx) -> string_t {
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_minus_tstzset] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, temp_str.GetData(), data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            size_t set_size = set_str.GetSize();
            if (set_size < sizeof(void*)) {
                free(data_copy);
                throw InvalidInputException("[Temporal_minus_tstzset] Invalid Set data: insufficient size");
            }
            uint8_t *set_copy = (uint8_t*)malloc(set_size);
            memcpy(set_copy, set_str.GetData(), set_size);
            Set *s = reinterpret_cast<Set*>(set_copy);

            Temporal *ret = temporal_minus_tstzset(temp, s);
            if (!ret) {
                free(set_copy);
                free(data_copy);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(set_copy);
            free(data_copy);
            return stored;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_minus_tstzspan(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t temp_str, string_t span_str, ValidityMask &mask, idx_t idx) -> string_t {
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_minus_tstzspan] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, temp_str.GetData(), data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            Span *span = nullptr;
            if (span_str.GetSize() > 0) {
                span = (Span*)malloc(span_str.GetSize());
                memcpy(span, span_str.GetData(), span_str.GetSize());
            }
            if (!span) {
                free(data_copy);
                throw InternalException("Failure in Temporal_minus_tstzspan: unable to cast to span");
            }

            Temporal *ret = temporal_minus_tstzspan(temp, span);
            if (!ret) {
                free(span);
                free(data_copy);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(span);
            free(data_copy);
            return stored;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_minus_tstzspanset(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t temp_str, string_t spanset_str, ValidityMask &mask, idx_t idx) -> string_t {
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_minus_tstzspanset] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, temp_str.GetData(), data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            SpanSet *ss = nullptr;
            if (spanset_str.GetSize() > 0) {
                ss = (SpanSet*)malloc(spanset_str.GetSize());
                memcpy(ss, spanset_str.GetData(), spanset_str.GetSize());
            }
            if (!ss) {
                free(data_copy);
                throw InternalException("Failure in Temporal_minus_tstzspanset: unable to cast to spanset");
            }

            Temporal *ret = temporal_minus_tstzspanset(temp, ss);
            if (!ret) {
                free(ss);
                free(data_copy);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(ss);
            free(data_copy);
            return stored;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_before_timestamptz(DataChunk &args, ExpressionState &state, Vector &result) {
    if (args.ColumnCount() == 2) {
        BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, string_t>(
            args.data[0], args.data[1], result, args.size(),
            [&](string_t temp_str, timestamp_tz_t ts_duckdb, ValidityMask &mask, idx_t idx) -> string_t {
                const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
                size_t data_size = temp_str.GetSize();
                if (data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_before_timestamptz] Invalid Temporal data: insufficient size");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, data, data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
                if (!temp) {
                    free(data_copy);
                    throw InternalException("Failure in Temporal_before_timestamptz: unable to cast string to temporal");
                }
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                Temporal *ret = temporal_before_timestamptz(temp, (TimestampTz)ts_meos.value, true);
                if (!ret) {
                    free(temp);
                    mask.SetInvalid(idx);
                    return string_t();
                }
                size_t ret_size = temporal_mem_size(ret);
                string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
                string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
                free(ret);
                free(temp);
                return stored_data;
            }
        );
    } else if (args.ColumnCount() == 3) {
        TernaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, bool, string_t>(
            args.data[0], args.data[1], args.data[2], result, args.size(),
            [&](string_t temp_str, timestamp_tz_t ts_duckdb, bool strict_str, ValidityMask &mask, idx_t idx) -> string_t {
                const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
                size_t data_size = temp_str.GetSize();
                if (data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_before_timestamptz] Invalid Temporal data: insufficient size");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, data, data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
                if (!temp) {
                    free(data_copy);
                    throw InternalException("Failure in Temporal_before_timestamptz: unable to cast string to temporal");
                }
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                Temporal *ret = temporal_before_timestamptz(temp, (TimestampTz)ts_meos.value, strict_str);
                if (!ret) {
                    free(temp);
                    mask.SetInvalid(idx);
                    return string_t();
                }
                size_t ret_size = temporal_mem_size(ret);
                string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
                string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
                free(ret);
                free(temp);
                return stored_data;
            }
        );
    }
    else {
        throw InvalidInputException("[Temporal_before_timestamptz] Invalid number of arguments");
    }
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_after_timestamptz(DataChunk &args, ExpressionState &state, Vector &result) {
    if (args.ColumnCount() == 2) {
        BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, string_t>(
            args.data[0], args.data[1], result, args.size(),
            [&](string_t temp_str, timestamp_tz_t ts_duckdb, ValidityMask &mask, idx_t idx) -> string_t {
                const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
                size_t data_size = temp_str.GetSize();
                if (data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_after_timestamptz] Invalid Temporal data: insufficient size");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, data, data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
                if (!temp) {
                    free(data_copy);
                    throw InternalException("Failure in Temporal_after_timestamptz: unable to cast string to temporal");
                }
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                Temporal *ret = temporal_after_timestamptz(temp, (TimestampTz)ts_meos.value, true);
                if (!ret) {
                    free(temp);
                    mask.SetInvalid(idx);
                    return string_t();
                }
                size_t ret_size = temporal_mem_size(ret);
                string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
                string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
                free(ret);
                free(temp);
                return stored_data;
            }
        );
    } else if (args.ColumnCount() == 3) {
        TernaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, bool, string_t>(
            args.data[0], args.data[1], args.data[2], result, args.size(),
            [&](string_t temp_str, timestamp_tz_t ts_duckdb, bool strict_str, ValidityMask &mask, idx_t idx) -> string_t {
                const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
                size_t data_size = temp_str.GetSize();
                if (data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_after_timestamptz] Invalid Temporal data: insufficient size");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, data, data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
                if (!temp) {
                    free(data_copy);
                    throw InternalException("Failure in Temporal_after_timestamptz: unable to cast string to temporal");
                }
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                Temporal *ret = temporal_after_timestamptz(temp, (TimestampTz)ts_meos.value, strict_str);
                if (!ret) {
                    free(temp);
                    mask.SetInvalid(idx);
                    return string_t();
                }
                size_t ret_size = temporal_mem_size(ret);
                string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
                string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
                free(ret);
                free(temp);
                return stored_data;
            }
        );
    }
    else {
        throw InvalidInputException("[Temporal_after_timestamptz] Invalid number of arguments");
    }
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Tnumber_valuespans(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t temp_str) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Tnumber_valuespans] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Tnumber_valuespans: unable to cast string to temporal");
            }
            SpanSet *ret = tnumber_valuespans(temp);
            if (!ret) {
                free(temp);
                return string_t();
            }
            size_t spanset_size = spanset_mem_size(ret);
            uint8_t *spanset_buffer = (uint8_t*)malloc(spanset_size);
            memcpy(spanset_buffer, ret, spanset_size);
            string_t ret_str(reinterpret_cast<const char*>(spanset_buffer), spanset_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(spanset_buffer);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Tnumber_avg_value(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, double_t>(
        args.data[0], result, args.size(),
        [&](string_t temp_str) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Tnumber_avg_value] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Tnumber_avg_value: unable to cast string to temporal");
            }
            double ret = tnumber_avg_value(temp);
            free(data_copy);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}
/* ***************************************************
 * Modification Functions
 ****************************************************/
void TemporalFunctions::Temporal_insert(DataChunk &args, ExpressionState &state, Vector &result) {
    if (args.ColumnCount() == 2) {
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
            args.data[0], args.data[1], result, args.size(),
            [&](string_t temp_str, string_t insert_temp_str, ValidityMask &mask, idx_t idx) -> string_t {
                const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
                size_t data_size = temp_str.GetSize();
                if (data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_insert] Invalid Temporal data: insufficient size");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, data, data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
                if (!temp) {
                    free(data_copy);
                    throw InternalException("Failure in Temporal_insert: unable to cast string to temporal");
                }

                const uint8_t *insert_data = reinterpret_cast<const uint8_t*>(insert_temp_str.GetData());
                size_t insert_data_size = insert_temp_str.GetSize();
                if (insert_data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_insert] Invalid Temporal data: insufficient size");
                }
                uint8_t *insert_data_copy = (uint8_t*)malloc(insert_data_size);
                memcpy(insert_data_copy, insert_data, insert_data_size);
                Temporal *insert_temp = reinterpret_cast<Temporal*>(insert_data_copy);
                if (!insert_temp) {
                    free(insert_data_copy);
                    throw InternalException("Failure in Temporal_insert: unable to cast string to temporal");
                }

                Temporal *ret = temporal_insert(temp, insert_temp, true);
                if (!ret) {
                    free(temp);
                    mask.SetInvalid(idx);
                    return string_t();
                }
                size_t ret_size = temporal_mem_size(ret);
                string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
                string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
                free(ret);
                free(temp);
                return stored_data;
            }
        );
    } else if (args.ColumnCount() == 3) {
        TernaryExecutor::ExecuteWithNulls<string_t, string_t, bool, string_t>(
            args.data[0], args.data[1], args.data[2], result, args.size(),
            [&](string_t temp_str, string_t insert_temp_str, bool connect_str, ValidityMask &mask, idx_t idx) -> string_t {
                const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
                size_t data_size = temp_str.GetSize();
                if (data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_insert] Invalid Temporal data: insufficient size");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, data, data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
                if (!temp) {
                    free(data_copy);
                    throw InternalException("Failure in Temporal_insert: unable to cast string to temporal");
                }
                const uint8_t *insert_data = reinterpret_cast<const uint8_t*>(insert_temp_str.GetData());
                size_t insert_data_size = insert_temp_str.GetSize();
                if (insert_data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_insert] Invalid Temporal data: insufficient size");
                }
                uint8_t *insert_data_copy = (uint8_t*)malloc(insert_data_size);
                memcpy(insert_data_copy, insert_data, insert_data_size);
                Temporal *insert_temp = reinterpret_cast<Temporal*>(insert_data_copy);
                if (!insert_temp) {
                    free(insert_data_copy);
                    throw InternalException("Failure in Temporal_insert: unable to cast string to temporal");
                }

                Temporal *ret = temporal_insert(temp, insert_temp, connect_str);
                if (!ret) {
                    free(temp);
                    mask.SetInvalid(idx);
                    return string_t();
                }
                size_t ret_size = temporal_mem_size(ret);
                string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
                string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
                free(ret);
                free(temp);
                return stored_data;
            }
        );
    } else {
        throw InvalidInputException("[Temporal_insert] Invalid number of arguments");
    }
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_update(DataChunk &args, ExpressionState &state, Vector &result) {
    if (args.ColumnCount() == 2) {
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
            args.data[0], args.data[1], result, args.size(),
            [&](string_t temp_str, string_t update_temp_str, ValidityMask &mask, idx_t idx) -> string_t {
                const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
                size_t data_size = temp_str.GetSize();
                if (data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_update] Invalid Temporal data: insufficient size");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, data, data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
                if (!temp) {
                    free(data_copy);
                    throw InternalException("Failure in Temporal_update: unable to cast string to temporal");
                }

                const uint8_t *update_data = reinterpret_cast<const uint8_t*>(update_temp_str.GetData());
                size_t update_data_size = update_temp_str.GetSize();
                if (update_data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_update] Invalid Temporal data: insufficient size");
                }
                uint8_t *update_data_copy = (uint8_t*)malloc(update_data_size);
                memcpy(update_data_copy, update_data, update_data_size);
                Temporal *update_temp = reinterpret_cast<Temporal*>(update_data_copy);
                if (!update_temp) {
                    free(update_data_copy);
                    throw InternalException("Failure in Temporal_update: unable to cast string to temporal");
                }

                Temporal *ret = temporal_update(temp, update_temp, true);
                if (!ret) {
                    free(temp);
                    mask.SetInvalid(idx);
                    return string_t();
                }
                size_t ret_size = temporal_mem_size(ret);
                string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
                string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
                free(ret);
                free(temp);
                return stored_data;
            }
        );
    } else if (args.ColumnCount() == 3) {
        TernaryExecutor::ExecuteWithNulls<string_t, string_t, bool, string_t>(
            args.data[0], args.data[1], args.data[2], result, args.size(),
            [&](string_t temp_str, string_t update_temp_str, bool connect_str, ValidityMask &mask, idx_t idx) -> string_t {
                const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
                size_t data_size = temp_str.GetSize();
                if (data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_update] Invalid Temporal data: insufficient size");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, data, data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
                if (!temp) {
                    free(data_copy);
                    throw InternalException("Failure in Temporal_update: unable to cast string to temporal");
                }
                const uint8_t *update_data = reinterpret_cast<const uint8_t*>(update_temp_str.GetData());
                size_t update_data_size = update_temp_str.GetSize();
                if (update_data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_update] Invalid Temporal data: insufficient size");
                }
                uint8_t *update_data_copy = (uint8_t*)malloc(update_data_size);
                memcpy(update_data_copy, update_data, update_data_size);
                Temporal *update_temp = reinterpret_cast<Temporal*>(update_data_copy);
                if (!update_temp) {
                    free(update_data_copy);
                    throw InternalException("Failure in Temporal_update: unable to cast string to temporal");
                }

                Temporal *ret = temporal_update(temp, update_temp, connect_str);
                if (!ret) {
                    free(temp);
                    mask.SetInvalid(idx);
                    return string_t();
                }
                size_t ret_size = temporal_mem_size(ret);
                string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
                string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
                free(ret);
                free(temp);
                return stored_data;
            }
        );
    } else {
        throw InvalidInputException("[Temporal_insert] Invalid number of arguments");
    }
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_delete_timestamptz(DataChunk &args, ExpressionState &state, Vector &result) {
    if (args.ColumnCount() == 2) {
        BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, string_t>(
            args.data[0], args.data[1], result, args.size(),
            [&](string_t temp_str, timestamp_tz_t ts_duckdb, ValidityMask &mask, idx_t idx) -> string_t {
                const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
                size_t data_size = temp_str.GetSize();
                if (data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_delete_timestamptz] Invalid Temporal data: insufficient size");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, data, data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
                if (!temp) {
                    free(data_copy);
                    throw InternalException("Failure in Temporal_delete_timestamptz: unable to cast string to temporal");
                }
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                Temporal *ret = temporal_delete_timestamptz(temp, (TimestampTz)ts_meos.value, true);
                if (!ret) {
                    free(temp);
                    mask.SetInvalid(idx);
                    return string_t();
                }
                size_t ret_size = temporal_mem_size(ret);
                string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
                string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
                free(ret);
                free(temp);
                return stored_data;
            }
        );
    } else if (args.ColumnCount() == 3) {
        TernaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, bool, string_t>(
            args.data[0], args.data[1], args.data[2], result, args.size(),
            [&](string_t temp_str, timestamp_tz_t ts_duckdb, bool connect_str, ValidityMask &mask, idx_t idx) -> string_t {
                const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
                size_t data_size = temp_str.GetSize();
                if (data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_delete_timestamptz] Invalid Temporal data: insufficient size");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, data, data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
                if (!temp) {
                    free(data_copy);
                    throw InternalException("Failure in Temporal_delete_timestamptz: unable to cast string to temporal");
                }
                timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
                Temporal *ret = temporal_delete_timestamptz(temp, (TimestampTz)ts_meos.value, connect_str);
                if (!ret) {
                    free(temp);
                    mask.SetInvalid(idx);
                    return string_t();
                }
                size_t ret_size = temporal_mem_size(ret);
                string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
                string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
                free(ret);
                free(temp);
                return stored_data;
            }
        );
    } else {
        throw InvalidInputException("[Temporal_delete_timestamptz] Invalid number of arguments");
    }
}

void TemporalFunctions::Temporal_delete_tstzset(DataChunk &args, ExpressionState &state, Vector &result) {
    if (args.ColumnCount() == 2) {
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
            args.data[0], args.data[1], result, args.size(),
            [&](string_t temp_str, string_t delete_temp_str, ValidityMask &mask, idx_t idx) -> string_t {
                const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
                size_t data_size = temp_str.GetSize();
                if (data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_delete_tstzset] Invalid Temporal data: insufficient size");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, data, data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
                if (!temp) {
                    free(data_copy);
                    throw InternalException("Failure in Temporal_delete_tstzset: unable to cast string to temporal");
                }

                const uint8_t *delete_data = reinterpret_cast<const uint8_t*>(delete_temp_str.GetData());
                size_t delete_data_size = delete_temp_str.GetSize();
                if (delete_data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_delete_tstzset] Invalid Temporal data: insufficient size");
                }
                uint8_t *delete_data_copy = (uint8_t*)malloc(delete_data_size);
                memcpy(delete_data_copy, delete_data, delete_data_size);
                Set *delete_set = reinterpret_cast<Set*>(delete_data_copy);
                if (!delete_set) {
                    free(delete_data_copy);
                    throw InternalException("Failure in Temporal_delete_tstzset: unable to cast string to set");
                }

                Temporal *ret = temporal_delete_tstzset(temp, delete_set, true);
                if (!ret) {
                    free(temp);
                    mask.SetInvalid(idx);
                    return string_t();
                }
                size_t ret_size = temporal_mem_size(ret);
                string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
                string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
                free(ret);
                free(temp);
                return stored_data;
            }
        );
    } else if (args.ColumnCount() == 3) {
        TernaryExecutor::ExecuteWithNulls<string_t, string_t, bool, string_t>(
            args.data[0], args.data[1], args.data[2], result, args.size(),
            [&](string_t temp_str, string_t delete_temp_str, bool connect_str, ValidityMask &mask, idx_t idx) -> string_t {
                const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
                size_t data_size = temp_str.GetSize();  
                if (data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_delete_tstzset] Invalid Temporal data: insufficient size");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, data, data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
                if (!temp) {
                    free(data_copy);
                    throw InternalException("Failure in Temporal_delete_tstzset: unable to cast string to temporal");
                }

                const uint8_t *delete_data = reinterpret_cast<const uint8_t*>(delete_temp_str.GetData());
                size_t delete_data_size = delete_temp_str.GetSize();
                if (delete_data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_delete_tstzset] Invalid Temporal data: insufficient size");
                }
                uint8_t *delete_data_copy = (uint8_t*)malloc(delete_data_size);
                memcpy(delete_data_copy, delete_data, delete_data_size);
                Set *delete_set = reinterpret_cast<Set*>(delete_data_copy);
                if (!delete_set) {
                    free(delete_data_copy);
                    throw InternalException("Failure in Temporal_delete_tstzset: unable to cast string to set");
                }

                Temporal *ret = temporal_delete_tstzset(temp, delete_set, connect_str);
                if (!ret) {
                    free(temp);
                    mask.SetInvalid(idx);
                    return string_t();
                }
                size_t ret_size = temporal_mem_size(ret);
                string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
                string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
                free(ret);
                free(temp);
                return stored_data;
            }
        );
    } else {
        throw InvalidInputException("[Temporal_delete_tstzset] Invalid number of arguments");
    }
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_delete_tstzspan(DataChunk &args, ExpressionState &state, Vector &result) {
    if (args.ColumnCount() == 2) {
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
            args.data[0], args.data[1], result, args.size(),
            [&](string_t temp_str, string_t delete_temp_str, ValidityMask &mask, idx_t idx) -> string_t {
                const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
                size_t data_size = temp_str.GetSize();
                if (data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_delete_tstzspan] Invalid Temporal data: insufficient size");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, data, data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
                if (!temp) {
                    free(data_copy);
                    throw InternalException("Failure in Temporal_delete_tstzspan: unable to cast string to temporal");
                }

                const uint8_t *delete_data = reinterpret_cast<const uint8_t*>(delete_temp_str.GetData());
                size_t delete_data_size = delete_temp_str.GetSize();
                if (delete_data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_delete_tstzspan] Invalid Temporal data: insufficient size");
                }
                uint8_t *delete_data_copy = (uint8_t*)malloc(delete_data_size);
                memcpy(delete_data_copy, delete_data, delete_data_size);
                Span *delete_span = reinterpret_cast<Span*>(delete_data_copy);
                if (!delete_span) {
                    free(delete_data_copy);
                    throw InternalException("Failure in Temporal_delete_tstzspan: unable to cast string to span");
                }

                Temporal *ret = temporal_delete_tstzspan(temp, delete_span, true);
                if (!ret) {
                    free(temp);
                    mask.SetInvalid(idx);
                    return string_t();
                }
                size_t ret_size = temporal_mem_size(ret);
                string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
                string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
                free(ret);
                free(temp);
                return stored_data;
            }
        );
    } else if (args.ColumnCount() == 3) {
        TernaryExecutor::ExecuteWithNulls<string_t, string_t, bool, string_t>(
            args.data[0], args.data[1], args.data[2], result, args.size(),
            [&](string_t temp_str, string_t delete_temp_str, bool connect_str, ValidityMask &mask, idx_t idx) -> string_t {
                const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
                size_t data_size = temp_str.GetSize();
                if (data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_delete_tstzspan] Invalid Temporal data: insufficient size");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, data, data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);    
                if (!temp) {
                    free(data_copy);
                    throw InternalException("Failure in Temporal_delete_tstzspan: unable to cast string to temporal");
                }

                const uint8_t *delete_data = reinterpret_cast<const uint8_t*>(delete_temp_str.GetData());
                size_t delete_data_size = delete_temp_str.GetSize();
                if (delete_data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_delete_tstzspan] Invalid Temporal data: insufficient size");
                }
                uint8_t *delete_data_copy = (uint8_t*)malloc(delete_data_size);
                memcpy(delete_data_copy, delete_data, delete_data_size);
                Span *delete_span = reinterpret_cast<Span*>(delete_data_copy);
                if (!delete_span) {
                    free(delete_data_copy);
                    throw InternalException("Failure in Temporal_delete_tstzspan: unable to cast string to span");
                }

                Temporal *ret = temporal_delete_tstzspan(temp, delete_span, connect_str);
                if (!ret) {
                    free(temp);
                    mask.SetInvalid(idx);
                    return string_t();
                }
                size_t ret_size = temporal_mem_size(ret);
                string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
                string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
                free(ret);
                free(temp);
                return stored_data;
            }
        );
    } else {
        throw InvalidInputException("[Temporal_delete_tstzspan] Invalid number of arguments");
    }
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_delete_tstzspanset(DataChunk &args, ExpressionState &state, Vector &result) {
    if (args.ColumnCount() == 2) {
        BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
            args.data[0], args.data[1], result, args.size(),
            [&](string_t temp_str, string_t delete_temp_str, ValidityMask &mask, idx_t idx) -> string_t {
                const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
                size_t data_size = temp_str.GetSize();
                if (data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_delete_tstzspanset] Invalid Temporal data: insufficient size");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, data, data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
                if (!temp) {
                    free(data_copy);
                    throw InternalException("Failure in Temporal_delete_tstzspanset: unable to cast string to temporal");
                }

                const uint8_t *delete_data = reinterpret_cast<const uint8_t*>(delete_temp_str.GetData());
                size_t delete_data_size = delete_temp_str.GetSize();
                if (delete_data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_delete_tstzspanset] Invalid Temporal data: insufficient size");
                }
                uint8_t *delete_data_copy = (uint8_t*)malloc(delete_data_size);
                memcpy(delete_data_copy, delete_data, delete_data_size);
                SpanSet *delete_spanset = reinterpret_cast<SpanSet*>(delete_data_copy);
                if (!delete_spanset) {
                    free(delete_data_copy);
                    throw InternalException("Failure in Temporal_delete_tstzspanset: unable to cast string to span set");
                }

                Temporal *ret = temporal_delete_tstzspanset(temp, delete_spanset, true);
                if (!ret) {
                    free(temp);
                    mask.SetInvalid(idx);
                    return string_t();
                }
                size_t ret_size = temporal_mem_size(ret);
                string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
                string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
                free(ret);
                free(temp);
                return stored_data;
            }
        );
    } else if (args.ColumnCount() == 3) {
        TernaryExecutor::ExecuteWithNulls<string_t, string_t, bool, string_t>(
            args.data[0], args.data[1], args.data[2], result, args.size(),
            [&](string_t temp_str, string_t delete_temp_str, bool connect_str, ValidityMask &mask, idx_t idx) -> string_t {
                const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
                size_t data_size = temp_str.GetSize();
                if (data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_delete_tstzspanset] Invalid Temporal data: insufficient size");
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, data, data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
                if (!temp) {
                    free(data_copy);
                    throw InternalException("Failure in Temporal_delete_tstzspanset: unable to cast string to temporal");
                }

                const uint8_t *delete_data = reinterpret_cast<const uint8_t*>(delete_temp_str.GetData());
                size_t delete_data_size = delete_temp_str.GetSize();
                if (delete_data_size < sizeof(void*)) {
                    throw InvalidInputException("[Temporal_delete_tstzspanset] Invalid Temporal data: insufficient size");
                }
                uint8_t *delete_data_copy = (uint8_t*)malloc(delete_data_size);
                memcpy(delete_data_copy, delete_data, delete_data_size);
                SpanSet *delete_spanset = reinterpret_cast<SpanSet*>(delete_data_copy);
                if (!delete_spanset) {
                    free(delete_data_copy);
                    throw InternalException("Failure in Temporal_delete_tstzspanset: unable to cast string to span set");
                }

                Temporal *ret = temporal_delete_tstzspanset(temp, delete_spanset, connect_str);
                if (!ret) {
                    free(temp);
                    mask.SetInvalid(idx);
                    return string_t();
                }
                size_t ret_size = temporal_mem_size(ret);
                string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
                string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
                free(ret);
                free(temp);
                return stored_data;
            }
        );
    } else {
        throw InvalidInputException("[Temporal_delete_tstzspanset] Invalid number of arguments");
    }
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


/* ***************************************************
* Segment Duration Functions
****************************************************/
void TemporalFunctions::Temporal_segm_min_duration(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    bool strict = true;
    if (args.ColumnCount() == 3) {
        auto &flag_vec = args.data[2];
        flag_vec.Flatten(count);
        strict = flag_vec.GetValue(0).GetValue<bool>();
    }

    BinaryExecutor::ExecuteWithNulls<string_t, interval_t, string_t>(
        args.data[0], args.data[1], result, count,
        [&](string_t temp_str, interval_t interval, ValidityMask &mask, idx_t idx) -> string_t {
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_segm_min_duration] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, temp_str.GetData(), data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            MeosInterval meos_interval = IntervaltToInterval(interval);
            TSequenceSet *ret = temporal_segm_duration(temp, &meos_interval, true, strict);
            if (!ret) {
                free(data_copy);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size((Temporal*)ret);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(data_copy);
            return stored;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_segm_max_duration(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    bool strict = true;
    if (args.ColumnCount() == 3) {
        auto &flag_vec = args.data[2];
        flag_vec.Flatten(count);
        strict = flag_vec.GetValue(0).GetValue<bool>();
    }

    BinaryExecutor::ExecuteWithNulls<string_t, interval_t, string_t>(
        args.data[0], args.data[1], result, count,
        [&](string_t temp_str, interval_t interval, ValidityMask &mask, idx_t idx) -> string_t {
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_segm_max_duration] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, temp_str.GetData(), data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

            MeosInterval meos_interval = IntervaltToInterval(interval);
            TSequenceSet *ret = temporal_segm_duration(temp, &meos_interval, false, strict);
            if (!ret) {
                free(data_copy);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size((Temporal*)ret);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(data_copy);
            return stored;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}
/* ***************************************************
 * Boolean operators
 ****************************************************/

void TemporalFunctions::Tbool_when_true(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t temp_str, ValidityMask &mask, idx_t idx) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Tbool_when_true] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_at_tstzspanset: unable to cast string to temporal");
            }

            SpanSet *ret = tbool_when_true(temp);
            if (!ret) {
                free(temp);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t spanset_size = spanset_mem_size(ret);
            uint8_t *spanset_buffer = (uint8_t*)malloc(spanset_size);
            memcpy(spanset_buffer, ret, spanset_size);
            string_t ret_str(reinterpret_cast<const char*>(spanset_buffer), spanset_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(spanset_buffer);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Workaround functions
 ****************************************************/

template <typename T>
void TemporalFunctions::Temporal_dump_common(DataChunk &args, Vector &result, meosType basetype) {
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
                char *actual_value = text2cstring(txt);
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

void TemporalFunctions::Temporal_round(DataChunk &args, ExpressionState &state, Vector &result) {
    auto row_count = args.size();
    auto arg_count = args.ColumnCount();
    int32_t size = 0;
    if (arg_count > 1) {
        auto &size_child = args.data[1];
        size_child.Flatten(row_count);
        size = size_child.GetValue(0).GetValue<int32_t>();
    }

    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t temp_str) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(temp_str.GetData());
            size_t data_size = temp_str.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_round] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_round: unable to cast string to temporal");
            }

            Temporal *ret = temporal_round(temp, size);
            size_t temp_size = temporal_mem_size((Temporal*)ret);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, ret, temp_size);
            string_t ret_str(reinterpret_cast<const char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);

            free(temp_data);
            free(ret);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

} // namespace duckdb