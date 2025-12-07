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
            return stored_data;
        }
    );
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
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
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
                seq_data[entry.offset + i] = ret_str;
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
                inst_data[entry.offset + i] = ret_str;
            }
            free(instants);
            free(temp);
            return entry;
        }
    );
    ListVector::SetListSize(result, total_count);
}

/* ***************************************************
 * Transformation functions
 ****************************************************/

void TemporalFunctions::Temporal_to_tsequence(DataChunk &args, ExpressionState &state, Vector &result) {
    interpType interp = INTERP_NONE;
    if (args.size() > 1) {
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

            free(temp);
            return result_str;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TemporalFunctions::Temporal_to_tsequenceset(DataChunk &args, ExpressionState &state, Vector &result) {
        interpType interp = INTERP_NONE;
        if (args.size() > 1) {
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

            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
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

void TemporalFunctions::Temporal_at_value_tbool(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, bool, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input, bool value) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input.GetData());
            size_t data_size = input.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("[Temporal_at_value_tbool] Invalid Temporal data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InternalException("Failure in Temporal_at_value_tbool: unable to cast string to temporal");
            }
            Temporal *ret = temporal_restrict_value(temp, (Datum)value, true);
            if (!ret) {
                throw InternalException("Failure in TemporalAtValue: unable to cast string to temporal");
                return string_t();
            }
            size_t temp_size = temporal_mem_size(ret);
            uint8_t *temp_data = (uint8_t*)malloc(temp_size);
            memcpy(temp_data, ret, temp_size);
            string_t ret_str(reinterpret_cast<const char*>(temp_data), temp_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);

            free(ret);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
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