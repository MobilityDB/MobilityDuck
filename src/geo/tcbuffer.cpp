#include "geo/tcbuffer.hpp"
#include "geo/tgeompoint_functions.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include <regex>
#include <string>
#include <temporal/span.hpp>
#include "temporal/spanset.hpp"
#include "temporal/set.hpp"
#include "temporal/temporal_functions.hpp"
#include "geo/stbox.hpp"
#include "geo/geoset.hpp"
#include<time_util.hpp>
#include "geo_util.hpp"
#include "spatial/spatial_types.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_geo.h>
    #include <meos_internal.h>
    #include <meos_internal_geo.h>
    #include <meos_cbuffer.h>
}


namespace duckdb {

LogicalType TCBufferTypes::TCBUFFER() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("TCBUFFER");
    return type;
}

/*
 * Constructors
*/

inline void Tcbuffer_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_geom_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_geom_vec, result, count,
        [&](string_t input_geom_str) -> string_t {
            std::string input = input_geom_str.GetString();

            Temporal *tinst = tcbuffer_in(input.c_str());
            if (!tinst) {
                throw InvalidInputException("Invalid TCBUFFER input: " + input);
            }

            size_t data_size = temporal_mem_size(tinst);

            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(tinst);
                throw InvalidInputException("Failed to allocate memory for TCBUFFER data");
            }

            memcpy(data_buffer, tinst, data_size);

            string_t data_string_t(reinterpret_cast<const char*>(data_buffer), data_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, data_string_t);

            free(data_buffer);
            free(tinst);

            return stored_data;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

inline void Tcbufferinst_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &value_vec = args.data[0];
    auto &t_vec = args.data[1];

    BinaryExecutor::Execute<string_t, timestamp_tz_t, string_t>(
        value_vec, t_vec, result, count,
        [&](string_t value_str, timestamp_tz_t t) -> string_t {
            std::string value = value_str.GetString();

            // The tcbuffer value type is a Cbuffer (point + radius)
            // varlena, parsed from its canonical text form, e.g.
            // 'Cbuffer(Point(1 1), 0.5)'.
            Cbuffer *cb = cbuffer_in(value.c_str());

            if (cb == NULL) {
                throw InvalidInputException("Invalid circular buffer format: " + value);
            }

            timestamp_tz_t meos_timestamp = DuckDBToMeosTimestamp(t);
            // No tcbufferinst_make exists; the generic tinstant_make
            // builds a T_TCBUFFER instant from the Cbuffer Datum.
            TInstant *inst = tinstant_make(Datum(cb), T_TCBUFFER,
                                           static_cast<TimestampTz>(meos_timestamp.value));

            if (inst == NULL) {
                free(cb);
                throw InvalidInputException("Failed to create TInstant");
            }

            size_t data_size = temporal_mem_size((Temporal*)inst);

            uint8_t *data_buffer = (uint8_t *)malloc(data_size);

            if (!data_buffer){
                free(inst);
                free(cb);
                throw InvalidInputException("Failed to allocate memory to circular buffer data");
            }
            memcpy(data_buffer, inst, data_size);

            string_t data_string_t(reinterpret_cast<const char*>(data_buffer),data_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, data_string_t);

            free(data_buffer);
            free(inst);
            free(cb);

            return stored_data;

        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


inline void Tcbuffer_sequence_from_tstzspan(DataChunk &args, ExpressionState &state, Vector &result) {
    const char* default_interp = "step";
    auto count = args.size();
    auto arg_count = args.ColumnCount();

    auto &input_geom_vec = args.data[0];
    auto &span_vec = args.data[1];

    // Check if interpolation parameter is provided
    Vector *interp_vec = nullptr;
    if (arg_count > 2) {
        interp_vec = &args.data[2];
    }

    BinaryExecutor::Execute<string_t, string_t, string_t>(
        input_geom_vec, span_vec, result, count,
        [&](string_t input_geom_str, string_t span_str)-> string_t{
            std::string geom_value = input_geom_str.GetString();

            Cbuffer *cb = cbuffer_in(geom_value.c_str());

            if(cb == NULL){
                throw InvalidInputException("Invalid circular buffer format: "+ geom_value);
            }

            std::string input = span_str.GetString();

            Span *span_cmp = reinterpret_cast<Span*>(const_cast<char*>(input.c_str()));

            // Use default interpolation or provided value
            interpType interp = interptype_from_string(default_interp);
            if (interp_vec) {
                std::string interp_string = default_interp;
                interp = interptype_from_string(interp_string.c_str());
            }

            TSequence *seq = tsequence_from_base_tstzspan(Datum(cb), T_TCBUFFER, span_cmp, interp);

            if (seq == NULL) {
                free(cb);
                throw InvalidInputException("Failed to create TSequence");
            }

            size_t seq_size = temporal_mem_size((Temporal*)seq);

            uint8_t *seq_buffer = (uint8_t *)malloc(seq_size);
            if (!seq_buffer) {
                free(seq);
                free(cb);
                throw InvalidInputException("Failed to allocate memory for sequence data");
            }

            memcpy(seq_buffer, seq, seq_size);

            string_t seq_string_t((char*) seq_buffer, seq_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, seq_string_t);

            free(seq_buffer);
            free(seq);
            free(cb);

            return stored_data;

        });

    if (count == 1){
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

TInstant **temparr_extract_cb(Vector &tcbuffer_arr_vec, list_entry_t list_entry, int *count) {
    auto &child_vector = ListVector::GetEntry(tcbuffer_arr_vec);
    auto list_size = list_entry.length;
    auto list_offset = list_entry.offset;

    if (list_size == 0) {
        *count = 0;
        return nullptr;
    }

    *count = list_size;

    TInstant **instants = (TInstant**)malloc(sizeof(TInstant*) * list_size);
    if (!instants) {
        *count = 0;
        return nullptr;
    }

    for (idx_t i = 0; i < list_size; i++) {
        auto element_idx = list_offset + i;
        string_t tgeom_blob = FlatVector::GetData<string_t>(child_vector)[element_idx];

        const uint8_t *data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
        size_t data_size = tgeom_blob.GetSize();

        if (data_size < sizeof(void*)) {
            for (idx_t j = 0; j < i; j++) {
                if (instants[j]) free(instants[j]);
            }
            free(instants);
            *count = 0;
            return nullptr;
        }

        uint8_t *data_copy = (uint8_t*)malloc(data_size);
        if (!data_copy) {
            for (idx_t j = 0; j < i; j++) {
                if (instants[j]) free(instants[j]);
            }
            free(instants);
            *count = 0;
            return nullptr;
        }
        memcpy(data_copy, data, data_size);

        Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
        if (!temp) {
            free(data_copy);
            for (idx_t j = 0; j < i; j++) {
                if (instants[j]) free(instants[j]);
            }
            free(instants);
            *count = 0;
            return nullptr;
        }

        instants[i] = (TInstant*)temp;
    }

    return instants;
}

inline void Tcbuffer_sequence_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    // Default values
    const char* default_interp = "step";
    bool default_lower_inc = true;
    bool default_upper_inc = true;

    auto count = args.size();
    auto arg_count = args.ColumnCount();


    auto &tcbuffer_arr_vec = args.data[0];
    tcbuffer_arr_vec.Flatten(count);

    Vector *interp_vec = nullptr;
    Vector *lower_vec = nullptr;
    Vector *upper_vec = nullptr;

    if (arg_count > 1) {
        interp_vec = &args.data[1];
        interp_vec->Flatten(count);
    }
    if (arg_count > 2) {
        lower_vec = &args.data[2];
        lower_vec->Flatten(count);
    }
    if (arg_count > 3) {
        upper_vec = &args.data[3];
        upper_vec->Flatten(count);
    }

    result.Flatten(count);

    auto tcbuffer_data = FlatVector::GetData<list_entry_t>(tcbuffer_arr_vec);
    auto result_data = FlatVector::GetData<string_t>(result);

    // Get validity masks
    auto &tcbuffer_validity = FlatVector::Validity(tcbuffer_arr_vec);
    auto &result_validity = FlatVector::Validity(result);

    for (idx_t i = 0; i < count; i++) {
        if (!tcbuffer_validity.RowIsValid(i)) {
            result_validity.SetInvalid(i);
            continue;
        }

        try {
            list_entry_t list_entry = tcbuffer_data[i];

            // Handle interp parameter with default
            std::string interp_str = default_interp;
            if (interp_vec) {
                auto interp_data = FlatVector::GetData<string_t>(*interp_vec);
                auto &interp_validity = FlatVector::Validity(*interp_vec);
                if (interp_validity.RowIsValid(i)) {
                    interp_str = interp_data[i].GetString();
                }
            }
            interpType interp = interptype_from_string(interp_str.c_str());

            bool lower_inc = default_lower_inc;
            bool upper_inc = default_upper_inc;

            if (lower_vec) {
                auto lower_data = FlatVector::GetData<bool>(*lower_vec);
                auto &lower_validity = FlatVector::Validity(*lower_vec);
                if (lower_validity.RowIsValid(i)) {
                    lower_inc = lower_data[i];
                }
            }

            if (upper_vec) {
                auto upper_data = FlatVector::GetData<bool>(*upper_vec);
                auto &upper_validity = FlatVector::Validity(*upper_vec);
                if (upper_validity.RowIsValid(i)) {
                    upper_inc = upper_data[i];
                }
            }

            // Extract array elements
            int element_count;
            TInstant **instants = temparr_extract_cb(tcbuffer_arr_vec, list_entry, &element_count);

            if (!instants || element_count == 0) {
                result_validity.SetInvalid(i);
                continue;
            }

            TSequence *sequence_result = tsequence_make((TInstant **) instants, element_count,
                                                    lower_inc, upper_inc, interp, true);

            if (!sequence_result) {
                for (int j = 0; j < element_count; j++) {
                    if (instants[j]) {
                        free(instants[j]);
                    }
                }
                free(instants);
                result_validity.SetInvalid(i);
                continue;
            }

            size_t data_size = temporal_mem_size(reinterpret_cast<Temporal*>(sequence_result));
            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(sequence_result);
                for (int j = 0; j < element_count; j++) {
                    if (instants[j]) {
                        free(instants[j]);
                    }
                }
                free(instants);
                result_validity.SetInvalid(i);
                continue;
            }

            memcpy(data_buffer, sequence_result, data_size);

            string_t data_string_t(reinterpret_cast<const char*>(data_buffer), data_size);
            result_data[i] = StringVector::AddStringOrBlob(result, data_string_t);

            free(data_buffer);
            free(sequence_result);
            for (int j = 0; j < element_count; j++) {
                if (instants[j]) {
                    free(instants[j]);
                }
            }
            free(instants);

        } catch (const std::exception& e) {
            result_validity.SetInvalid(i);
        }
    }

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}




/*
 * Conversions
*/

inline void Temporal_to_tstzspan(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_geom_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_geom_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));

            if (!temp) {
                throw InvalidInputException("Invalid TCBUFFER data: null pointer");
            }

            Span *timespan = temporal_to_tstzspan(temp);

            if (!timespan) {
                throw InvalidInputException("Failed to extract timespan from TCBUFFER");
            }

            size_t span_size = sizeof(Span);

            uint8_t *span_buffer = (uint8_t*)malloc(span_size);
            if (!span_buffer) {
                free(timespan);
                throw InvalidInputException("Failed to allocate memory for timespan data");
            }

            memcpy(span_buffer, timespan, span_size);

            string_t span_string_t(reinterpret_cast<const char*>(span_buffer), span_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);

            free(span_buffer);
            free(timespan);

            return stored_data;
        }
    );

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/*
 * Transformations
*/

inline void Temporal_to_tinstant(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_geom_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_geom_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));

            if (!temp) {
                throw InvalidInputException("Invalid TCBUFFER data: null pointer");
            }

            TInstant *inst = temporal_to_tinstant(temp);
            if (!inst) {
                throw InvalidInputException("Failed to convert TCBUFFER to TInstant");
            }

            size_t inst_size = temporal_mem_size((Temporal*)inst);

            uint8_t *inst_buffer = (uint8_t*)malloc(inst_size);
            if (!inst_buffer) {
                free(inst);
                throw InvalidInputException("Failed to allocate memory for TInstant data");
            }

            memcpy(inst_buffer, inst, inst_size);

            string_t inst_string_t(reinterpret_cast<const char*>(inst_buffer), inst_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, inst_string_t);

            free(inst_buffer);
            free(inst);

            return stored_data;
        }
    );

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


inline void Temporal_set_interp(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &tgeom_vec = args.data[0];
    auto &interp_vec = args.data[1];

    tgeom_vec.Flatten(count);
    interp_vec.Flatten(count);

    BinaryExecutor::Execute<string_t, string_t, string_t>(
        tgeom_vec, interp_vec, result, count,
        [&](string_t tgeom_str_t, string_t interp_str_t) -> string_t {

            std::string input = tgeom_str_t.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));
            if (!temp) {
                throw InvalidInputException("Invalid TCBUFFER data: null pointer");
            }

            std::string interp_str = interp_str_t.GetString();
            interpType new_interp = interptype_from_string(interp_str.c_str());

            Temporal *result_temp = temporal_set_interp(temp, new_interp);
            if (!result_temp) {
                throw InvalidInputException("Failed to set interpolation");
            }

            // Serialize result back to binary
            size_t result_size = temporal_mem_size(result_temp);
            uint8_t *result_buffer = (uint8_t*)malloc(result_size);
            if (!result_buffer) {
                free(result_temp);
                throw InvalidInputException("Failed to allocate memory for result");
            }

            memcpy(result_buffer, result_temp, result_size);
            string_t result_string_t(reinterpret_cast<const char*>(result_buffer), result_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, result_string_t);

            free(result_buffer);
            free(result_temp);

            return stored_data;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


inline void Temporal_merge(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &tgeom1_vec = args.data[0];
    auto &tgeom2_vec = args.data[1];

    tgeom1_vec.Flatten(count);
    tgeom2_vec.Flatten(count);

    BinaryExecutor::Execute<string_t, string_t, string_t>(
        tgeom1_vec, tgeom2_vec, result, count,
        [&](string_t tgeom1_str_t, string_t tgeom2_str_t) -> string_t {
            std::string tgeom1 = tgeom1_str_t.GetString();

            Temporal *temp1 = reinterpret_cast<Temporal*>(const_cast<char*>(tgeom1.c_str()));
            if (!temp1) {
                throw InvalidInputException("Invalid TCBUFFER data: null pointer");
            }

            std::string tgeom2 = tgeom2_str_t.GetString();

            Temporal *temp2 = reinterpret_cast<Temporal*>(const_cast<char*>(tgeom2.c_str()));
            if (!temp2) {
                throw InvalidInputException("Invalid TCBUFFER data: null pointer");
            }

            Temporal *result_temp = temporal_merge(temp1, temp2);
            if (!result_temp) {
                throw InvalidInputException("Failed to merge temporal circular buffers");
            }

            // Serialize result back to binary
            size_t result_size = temporal_mem_size(result_temp);
            uint8_t *result_buffer = (uint8_t*)malloc(result_size);
            if (!result_buffer) {
                free(result_temp);
                throw InvalidInputException("Failed to allocate memory for result");
            }

            memcpy(result_buffer, result_temp, result_size);
            string_t result_string_t(reinterpret_cast<const char*>(result_buffer), result_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, result_string_t);

            free(result_buffer);
            free(result_temp);

            return stored_data;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


/*
 * Accessor Functions
*/

inline void Temporal_subtype(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &tgeom_vec = args.data[0];

    tgeom_vec.Flatten(count);

    UnaryExecutor::Execute<string_t, string_t>(
        tgeom_vec, result, count,
        [&](string_t tgeom_str_t) -> string_t {
            std::string input = tgeom_str_t.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));
            if (!temp) {
                throw InvalidInputException("Invalid TCBUFFER data: null pointer");
            }

            const char *subtype_str = temporal_subtype(temp);
            if (!subtype_str) {
                throw InvalidInputException("Failed to get temporal subtype");
            }

            std::string result_str(subtype_str);
            string_t stored_result = StringVector::AddString(result, result_str);

            return stored_result;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}




inline void Temporal_interp(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &tgeom_vec = args.data[0];

    tgeom_vec.Flatten(count);

    UnaryExecutor::Execute<string_t, string_t>(
        tgeom_vec, result, count,
        [&](string_t tgeom_str_t) -> string_t {

            std::string input = tgeom_str_t.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));
            if (!temp) {
                throw InvalidInputException("Invalid TCBUFFER data: null pointer");
            }


            const char *interp_str = temporal_interp(temp);
            if (!interp_str) {
                throw InvalidInputException("Failed to get temporal interpolation");
            }

            std::string result_str(interp_str);
            string_t stored_result = StringVector::AddString(result, result_str);

            return stored_result;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

inline void Temporal_mem_size(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &tgeom_vec = args.data[0];

    tgeom_vec.Flatten(count);

    UnaryExecutor::Execute<string_t, int32_t>(
        tgeom_vec, result, count,
        [&](string_t tgeom_str_t) -> int32_t {
           std::string input = tgeom_str_t.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));
            if (!temp) {
                throw InvalidInputException("Invalid TCBUFFER data: null pointer");
            }

            size_t mem_size = temporal_mem_size(temp);


            return static_cast<int32_t>(mem_size);
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// ---- Circular-buffer value accessors ----
// The tcbuffer value type is a Cbuffer (point + radius), which is not
// a registered DuckDB type. getValue / startValue / endValue surface it
// in its canonical text form (`Cbuffer(Point(x y), r)`), mirroring the
// asText output. point(tcbuffer) and radius(tcbuffer) expose the two
// components per the manual model: point() returns the geometry, radius()
// returns the float.

inline Cbuffer *cbuffer_from_instant_value(const TInstant *inst) {
    Datum d = tinstant_value(inst);
    return reinterpret_cast<Cbuffer*>(d);
}

inline void Tinstant_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            TInstant *tinst = reinterpret_cast<TInstant*>(const_cast<char*>(input.c_str()));

            // tinstant_value returns a freshly allocated copy of the
            // Cbuffer value (datum_copy), which the caller owns.
            Cbuffer *cb = cbuffer_from_instant_value(tinst);

            char *str = cbuffer_out(cb, 15);
            if (!str) {
                free(cb);
                throw InvalidInputException("Failed to convert circular buffer value to text");
            }
            std::string output(str);
            string_t stored_result = StringVector::AddString(result, output);

            free(str);
            free(cb);

            return stored_result;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}



inline void Temporal_start_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));

            // temporal_start_value returns a freshly allocated copy of
            // the Cbuffer value (datum_copy), which the caller owns.
            Datum start_datum = temporal_start_value(temp);

            Cbuffer *cb = reinterpret_cast<Cbuffer*>(start_datum);
            char *str = cbuffer_out(cb, 15);
            if (!str) {
                free(cb);
                throw InvalidInputException("Failed to convert circular buffer value to text");
            }
            std::string output(str);
            string_t stored_result = StringVector::AddString(result, output);

            free(str);
            free(cb);

            return stored_result;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


inline void Temporal_end_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));

            // temporal_end_value returns a freshly allocated copy of
            // the Cbuffer value (datum_copy), which the caller owns.
            Datum end_datum = temporal_end_value(temp);

            Cbuffer *cb = reinterpret_cast<Cbuffer*>(end_datum);
            char *str = cbuffer_out(cb, 15);
            if (!str) {
                free(cb);
                throw InvalidInputException("Failed to convert circular buffer value to text");
            }
            std::string output(str);
            string_t stored_result = StringVector::AddString(result, output);

            free(str);
            free(cb);

            return stored_result;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

inline void Tcbuffer_point(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));

            // temporal_start_value returns a freshly allocated copy of
            // the Cbuffer value (datum_copy), which the caller owns.
            Datum start_datum = temporal_start_value(temp);
            Cbuffer *cb = reinterpret_cast<Cbuffer*>(start_datum);

            // cbuffer_point returns a freshly allocated GSERIALIZED copy.
            GSERIALIZED *gs = cbuffer_point(cb);
            if (!gs) {
                free(cb);
                throw InvalidInputException("Failed to extract point from circular buffer");
            }

            string_t geometry_blob = GSerializedToGeometry(gs, state, result);
            string_t stored_result = StringVector::AddStringOrBlob(result, geometry_blob);

            free(gs);
            free(cb);

            return stored_result;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

inline void Tcbuffer_radius(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];

    UnaryExecutor::Execute<string_t, double>(
        input_vec, result, count,
        [&](string_t input_str) -> double {
            std::string input = input_str.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));

            // temporal_start_value returns a freshly allocated copy of
            // the Cbuffer value (datum_copy), which the caller owns.
            Datum start_datum = temporal_start_value(temp);
            Cbuffer *cb = reinterpret_cast<Cbuffer*>(start_datum);

            double r = cbuffer_radius(cb);
            free(cb);
            return r;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


inline void Temporal_lower_inc(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            Temporal* temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));

            bool lower_inc = temporal_lower_inc(temp);

            std::string result_str = lower_inc ? "true" : "false";
            string_t stored_result = StringVector::AddString(result, result_str);
            return stored_result;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

inline void Temporal_upper_inc(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            Temporal* temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));

            bool upper_inc = temporal_upper_inc(temp);

            std::string result_str = upper_inc ? "true" : "false";
            string_t stored_result = StringVector::AddString(result, result_str);
            return stored_result;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

inline void Temporal_start_instant(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));

            TInstant *start_inst = temporal_start_instant(temp);

            if (!start_inst) {
                throw InvalidInputException("Failed to get start_inst from temporal object");
            }

            size_t result_size = temporal_mem_size((Temporal*)start_inst);
            if (result_size == 0) {
                throw InvalidInputException("Invalid result size from temporal object");
            }

            uint8_t *result_buffer = (uint8_t*)malloc(result_size);
            if (!result_buffer) {
                free(start_inst);
                throw InvalidInputException("Failed to allocate memory for result");
            }

            memcpy(result_buffer, start_inst, result_size);
            string_t result_string_t(reinterpret_cast<const char*>(result_buffer), result_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, result_string_t);

            free(result_buffer);
            free(start_inst);
            return stored_result;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

inline void Temporal_end_instant(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));

            TInstant *end_inst = temporal_end_instant(temp);

            if (!end_inst) {
                throw InvalidInputException("Failed to get end_inst from temporal object");
            }

            size_t result_size = temporal_mem_size((Temporal*)end_inst);
            if (result_size == 0) {
                throw InvalidInputException("Invalid result size from temporal object");
            }

            uint8_t *result_buffer = (uint8_t*)malloc(result_size);
            if (!result_buffer) {
                free(end_inst);
                throw InvalidInputException("Failed to allocate memory for result");
            }

            memcpy(result_buffer, end_inst, result_size);
            string_t result_string_t(reinterpret_cast<const char*>(result_buffer), result_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, result_string_t);

            free(result_buffer);
            free(end_inst);
            return stored_result;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}




inline void Temporal_instant_n(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &tgeom_vec = args.data[0];
    auto &n_vec = args.data[1];

    BinaryExecutor::Execute<string_t, int32_t, string_t>(
        tgeom_vec, n_vec, result, count,
        [&](string_t tgeom_str, int32_t n) -> string_t {
            std::string tgeom = tgeom_str.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(tgeom.c_str()));

            TInstant *inst_n = temporal_instant_n(temp, n);
            if (!inst_n) {
                throw InvalidInputException("Failed to get instant n from temporal object");
            }

            size_t result_size = temporal_mem_size((Temporal*)inst_n);
            if (result_size == 0) {
                throw InvalidInputException("Invalid result size from temporal object");
            }

            uint8_t *result_buffer = (uint8_t*)malloc(result_size);
            if (!result_buffer) {
                free(inst_n);
                throw InvalidInputException("Failed to allocate memory for result");
            }

            memcpy(result_buffer, inst_n, result_size);
            string_t result_string_t(reinterpret_cast<const char*>(result_buffer), result_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, result_string_t);

            free(result_buffer);
            free(inst_n);
            return stored_result;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


inline void Tinstant_timestamptz(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_geom_vec = args.data[0];

    UnaryExecutor::Execute<string_t, timestamp_tz_t>(
        input_geom_vec, result, count,
        [&](string_t input_geom_str) -> timestamp_tz_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_geom_str.GetData());
            size_t data_size = input_geom_str.GetSize();

            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid TCBUFFER data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TCBUFFER deserialization");
            }
            memcpy(data_copy, data, data_size);

            TInstant *temp = reinterpret_cast<TInstant*>(data_copy);

            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TCBUFFER data: null pointer");
            }

            TimestampTz meos_t = temp->t;

            timestamp_tz_t meos_timestamp{meos_t};
            timestamp_tz_t duckdb_t = MeosToDuckDBTimestamp(meos_timestamp);

            free(data_copy);

            return duckdb_t;
        }
    );

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


void TCBufferTypes::RegisterScalarFunctions(ExtensionLoader &loader) {

    auto tcbuffer_function = ScalarFunction(
        "TCBUFFER",
        {LogicalType::VARCHAR},
        TCBufferTypes::TCBUFFER(),
        Tcbuffer_constructor
    );
    duckdb::RegisterSerializedScalarFunction(loader,  tcbuffer_function);

    auto tcbuffer_from_timestamp_function = ScalarFunction(
        "TCBUFFER",
        {LogicalType::VARCHAR, LogicalType::TIMESTAMP_TZ},
        TCBufferTypes::TCBUFFER(),
        Tcbufferinst_constructor);
    duckdb::RegisterSerializedScalarFunction(loader,  tcbuffer_from_timestamp_function);

     auto tcbuffer_from_tstzspan_function = ScalarFunction(
        "TCBUFFER",
        {LogicalType::VARCHAR, SpanTypes::TSTZSPAN(), LogicalType::VARCHAR},
        TCBufferTypes::TCBUFFER(),
        Tcbuffer_sequence_from_tstzspan
    );
    duckdb::RegisterSerializedScalarFunction(loader,  tcbuffer_from_tstzspan_function);

    auto tcbuffer_from_tstzspan_default = ScalarFunction(
        "TCBUFFER",
        {LogicalType::VARCHAR, SpanTypes::TSTZSPAN()},
        TCBufferTypes::TCBUFFER(),
        Tcbuffer_sequence_from_tstzspan
    );
    duckdb::RegisterSerializedScalarFunction(loader,  tcbuffer_from_tstzspan_default);

     auto tcbufferseqarr_1param= ScalarFunction(
        "tcbufferSeq",
        {LogicalType::LIST(TCBufferTypes::TCBUFFER())},
        TCBufferTypes::TCBUFFER(),
        Tcbuffer_sequence_constructor
    );
    duckdb::RegisterSerializedScalarFunction(loader,  tcbufferseqarr_1param);

    auto tcbufferseqarr_2params = ScalarFunction(
        "tcbufferSeq",
        {LogicalType::LIST(TCBufferTypes::TCBUFFER()), LogicalType::VARCHAR},
        TCBufferTypes::TCBUFFER(),
        Tcbuffer_sequence_constructor
    );
    duckdb::RegisterSerializedScalarFunction(loader,  tcbufferseqarr_2params);

    auto tcbufferseqarr_3params = ScalarFunction(
        "tcbufferSeq",
        {LogicalType::LIST(TCBufferTypes::TCBUFFER()), LogicalType::VARCHAR, LogicalType::BOOLEAN},
        TCBufferTypes::TCBUFFER(),
        Tcbuffer_sequence_constructor
    );
    duckdb::RegisterSerializedScalarFunction(loader,  tcbufferseqarr_3params);

    auto tcbufferseqarr_4params = ScalarFunction(
        "tcbufferSeq",
        {LogicalType::LIST(TCBufferTypes::TCBUFFER()), LogicalType::VARCHAR, LogicalType::BOOLEAN, LogicalType::BOOLEAN},
        TCBufferTypes::TCBUFFER(),
        Tcbuffer_sequence_constructor
    );
    duckdb::RegisterSerializedScalarFunction(loader,  tcbufferseqarr_4params);

    auto tcbuffer_to_timespan_function = ScalarFunction(
        "timeSpan",
        {TCBufferTypes::TCBUFFER()},
        SpanTypes::TSTZSPAN(),
        Temporal_to_tstzspan);
    duckdb::RegisterSerializedScalarFunction(loader,  tcbuffer_to_timespan_function);

    auto tcbuffer_to_tinstant_function = ScalarFunction(
        "tcbufferInst",
        {TCBufferTypes::TCBUFFER()},
        TCBufferTypes::TCBUFFER(),
        Temporal_to_tinstant);
    duckdb::RegisterSerializedScalarFunction(loader,  tcbuffer_to_tinstant_function);


    auto setInterp_function = ScalarFunction(
        "setInterp",
        {TCBufferTypes::TCBUFFER(), LogicalType::VARCHAR},
        TCBufferTypes::TCBUFFER(),
        Temporal_set_interp
    );
    duckdb::RegisterSerializedScalarFunction(loader,  setInterp_function);


    auto merge_function = ScalarFunction(
        "merge",
        {TCBufferTypes::TCBUFFER(), TCBufferTypes::TCBUFFER()},
        TCBufferTypes::TCBUFFER(),
        Temporal_merge
    );
    duckdb::RegisterSerializedScalarFunction(loader,  merge_function);

    auto tempSubtype_function = ScalarFunction(
        "tempSubtype",
        {TCBufferTypes::TCBUFFER()},
        LogicalType::VARCHAR,
        Temporal_subtype
    );
    duckdb::RegisterSerializedScalarFunction(loader,  tempSubtype_function);

    auto interp_function = ScalarFunction(
        "interp",
        {TCBufferTypes::TCBUFFER()},
        LogicalType::VARCHAR,
        Temporal_interp
    );
    duckdb::RegisterSerializedScalarFunction(loader,  interp_function);

    auto memSize_function = ScalarFunction(
        "memSize",
        {TCBufferTypes::TCBUFFER()},
        LogicalType::INTEGER,
        Temporal_mem_size
    );
    duckdb::RegisterSerializedScalarFunction(loader,  memSize_function);

    auto getValue_function = ScalarFunction(
        "getValue",
        {TCBufferTypes::TCBUFFER()},
        LogicalType::VARCHAR,
        Tinstant_value
    );
    duckdb::RegisterSerializedScalarFunction(loader,  getValue_function);


    auto tcbuffer_start_value_function = ScalarFunction(
        "startValue",
        {TCBufferTypes::TCBUFFER()},
        LogicalType::VARCHAR,
        Temporal_start_value
    );
    duckdb::RegisterSerializedScalarFunction(loader,  tcbuffer_start_value_function);

    auto tcbuffer_end_value_function = ScalarFunction(
        "endValue",
        {TCBufferTypes::TCBUFFER()},
        LogicalType::VARCHAR,
        Temporal_end_value
    );
    duckdb::RegisterSerializedScalarFunction(loader,  tcbuffer_end_value_function);

    auto tcbuffer_point_function = ScalarFunction(
        "point",
        {TCBufferTypes::TCBUFFER()},
        GeoTypes::GEOMETRY(),
        Tcbuffer_point
    );
    duckdb::RegisterSerializedScalarFunction(loader,  tcbuffer_point_function);

    auto tcbuffer_radius_function = ScalarFunction(
        "radius",
        {TCBufferTypes::TCBUFFER()},
        LogicalType::DOUBLE,
        Tcbuffer_radius
    );
    duckdb::RegisterSerializedScalarFunction(loader,  tcbuffer_radius_function);

    auto startInstant_function = ScalarFunction(
        "startInstant",
        {TCBufferTypes::TCBUFFER()},
        TCBufferTypes::TCBUFFER(),
        Temporal_start_instant
    );
    duckdb::RegisterSerializedScalarFunction(loader,  startInstant_function);

    auto endInstant_function = ScalarFunction(
        "endInstant",
        {TCBufferTypes::TCBUFFER()},
        TCBufferTypes::TCBUFFER(),
        Temporal_end_instant
    );
    duckdb::RegisterSerializedScalarFunction(loader,  endInstant_function);

    auto instantN_function = ScalarFunction(
        "instantN",
        {TCBufferTypes::TCBUFFER(), LogicalType::INTEGER},
        TCBufferTypes::TCBUFFER(),
        Temporal_instant_n
    );
    duckdb::RegisterSerializedScalarFunction(loader,  instantN_function);


    auto tcbuffer_gettimestamptz_function = ScalarFunction(
        "getTimestamp",
        {TCBufferTypes::TCBUFFER()},
        LogicalType::TIMESTAMP_TZ,
        Tinstant_timestamptz);
    duckdb::RegisterSerializedScalarFunction(loader,  tcbuffer_gettimestamptz_function);


    // ===================================================================
    // Foundational tcbuffer surface — accessors, time/value-restrict,
    // modifiers, and comparison. The MEOS C functions delegated to here
    // are subtype-agnostic (they take Temporal *), so we reuse the same
    // generic handlers wired for tgeompoint in temporal_functions.cpp.
    // ===================================================================

    const LogicalType TGEOM = TCBufferTypes::TCBUFFER();
    const LogicalType TSTZ  = LogicalType::TIMESTAMP_TZ;
    const LogicalType IVAL  = LogicalType::INTERVAL;

    // ---- Accessors ----
    loader.RegisterFunction(ScalarFunction(
        "valueAtTimestamp", {TGEOM, TSTZ}, LogicalType::VARCHAR,
        Tinstant_value));
    loader.RegisterFunction(ScalarFunction(
        "getTime", {TGEOM}, SpansetTypes::tstzspanset(),
        TemporalFunctions::Temporal_time));
    loader.RegisterFunction(ScalarFunction(
        "duration", {TGEOM}, IVAL,
        TemporalFunctions::Temporal_duration));
    loader.RegisterFunction(ScalarFunction(
        "duration", {TGEOM, LogicalType::BOOLEAN}, IVAL,
        TemporalFunctions::Temporal_duration));
    loader.RegisterFunction(ScalarFunction(
        "lowerInc", {TGEOM}, LogicalType::BOOLEAN,
        TemporalFunctions::Temporal_lower_inc));
    loader.RegisterFunction(ScalarFunction(
        "upperInc", {TGEOM}, LogicalType::BOOLEAN,
        TemporalFunctions::Temporal_upper_inc));
    loader.RegisterFunction(ScalarFunction(
        "numInstants", {TGEOM}, LogicalType::INTEGER,
        TemporalFunctions::Temporal_num_instants));
    loader.RegisterFunction(ScalarFunction(
        "instants", {TGEOM}, LogicalType::LIST(TGEOM),
        TemporalFunctions::Temporal_instants));
    loader.RegisterFunction(ScalarFunction(
        "numSequences", {TGEOM}, LogicalType::INTEGER,
        TemporalFunctions::Temporal_num_sequences));
    loader.RegisterFunction(ScalarFunction(
        "sequences", {TGEOM}, LogicalType::LIST(TGEOM),
        TemporalFunctions::Temporal_sequences));
    loader.RegisterFunction(ScalarFunction(
        "startSequence", {TGEOM}, TGEOM,
        TemporalFunctions::Temporal_start_sequence));
    loader.RegisterFunction(ScalarFunction(
        "endSequence", {TGEOM}, TGEOM,
        TemporalFunctions::Temporal_end_sequence));
    loader.RegisterFunction(ScalarFunction(
        "sequenceN", {TGEOM, LogicalType::INTEGER}, TGEOM,
        TemporalFunctions::Temporal_sequence_n));
    loader.RegisterFunction(ScalarFunction(
        "numTimestamps", {TGEOM}, LogicalType::INTEGER,
        TemporalFunctions::Temporal_num_timestamps));
    loader.RegisterFunction(ScalarFunction(
        "timestamps", {TGEOM}, LogicalType::LIST(TSTZ),
        TemporalFunctions::Temporal_timestamps));
    loader.RegisterFunction(ScalarFunction(
        "startTimestamp", {TGEOM}, TSTZ,
        TemporalFunctions::Temporal_start_timestamptz));
    loader.RegisterFunction(ScalarFunction(
        "endTimestamp", {TGEOM}, TSTZ,
        TemporalFunctions::Temporal_end_timestamptz));
    loader.RegisterFunction(ScalarFunction(
        "timestampN", {TGEOM, LogicalType::INTEGER}, TSTZ,
        TemporalFunctions::Temporal_timestamptz_n));
    loader.RegisterFunction(ScalarFunction(
        "segments", {TGEOM}, LogicalType::LIST(TGEOM),
        TemporalFunctions::Temporal_segments));

    // ---- Time-domain restrict / minus ----
    for (const auto &t : std::vector<std::pair<LogicalType, scalar_function_t>>{
             {TSTZ,                       TemporalFunctions::Temporal_at_timestamptz},
             {SetTypes::tstzset(),        TemporalFunctions::Temporal_at_tstzset},
             {SpanTypes::TSTZSPAN(),      TemporalFunctions::Temporal_at_tstzspan},
             {SpansetTypes::tstzspanset(), TemporalFunctions::Temporal_at_tstzspanset}}) {
        loader.RegisterFunction(ScalarFunction(
            "atTime", {TGEOM, t.first}, TGEOM, t.second));
    }
    for (const auto &t : std::vector<std::pair<LogicalType, scalar_function_t>>{
             {TSTZ,                       TemporalFunctions::Temporal_minus_timestamptz},
             {SetTypes::tstzset(),        TemporalFunctions::Temporal_minus_tstzset},
             {SpanTypes::TSTZSPAN(),      TemporalFunctions::Temporal_minus_tstzspan},
             {SpansetTypes::tstzspanset(), TemporalFunctions::Temporal_minus_tstzspanset}}) {
        loader.RegisterFunction(ScalarFunction(
            "minusTime", {TGEOM, t.first}, TGEOM, t.second));
    }

    // beforeTimestamp / afterTimestamp accept timestamptz
    loader.RegisterFunction(ScalarFunction(
        "beforeTimestamp", {TGEOM, TSTZ}, TGEOM,
        TemporalFunctions::Temporal_before_timestamptz));
    loader.RegisterFunction(ScalarFunction(
        "afterTimestamp", {TGEOM, TSTZ}, TGEOM,
        TemporalFunctions::Temporal_after_timestamptz));

    // ---- Modifiers (shift / scale / shiftScale / append / insert / update /
    // delete) ----
    loader.RegisterFunction(ScalarFunction(
        "shiftTime", {TGEOM, IVAL}, TGEOM,
        TemporalFunctions::Temporal_shift_time));
    loader.RegisterFunction(ScalarFunction(
        "scaleTime", {TGEOM, IVAL}, TGEOM,
        TemporalFunctions::Temporal_scale_time));
    loader.RegisterFunction(ScalarFunction(
        "shiftScaleTime", {TGEOM, IVAL, IVAL}, TGEOM,
        TemporalFunctions::Temporal_shift_scale_time));
    loader.RegisterFunction(ScalarFunction(
        "appendInstant", {TGEOM, TGEOM}, TGEOM,
        TemporalFunctions::Temporal_append_tinstant));
    loader.RegisterFunction(ScalarFunction(
        "appendSequence", {TGEOM, TGEOM}, TGEOM,
        TemporalFunctions::Temporal_append_tsequence));
    loader.RegisterFunction(ScalarFunction(
        "insert", {TGEOM, TGEOM}, TGEOM,
        TemporalFunctions::Temporal_insert));
    loader.RegisterFunction(ScalarFunction(
        "insert", {TGEOM, TGEOM, LogicalType::BOOLEAN}, TGEOM,
        TemporalFunctions::Temporal_insert));
    loader.RegisterFunction(ScalarFunction(
        "update", {TGEOM, TGEOM}, TGEOM,
        TemporalFunctions::Temporal_update));
    loader.RegisterFunction(ScalarFunction(
        "update", {TGEOM, TGEOM, LogicalType::BOOLEAN}, TGEOM,
        TemporalFunctions::Temporal_update));
    loader.RegisterFunction(ScalarFunction(
        "deleteTime", {TGEOM, TSTZ}, TGEOM,
        TemporalFunctions::Temporal_delete_timestamptz));
    loader.RegisterFunction(ScalarFunction(
        "deleteTime", {TGEOM, TSTZ, LogicalType::BOOLEAN}, TGEOM,
        TemporalFunctions::Temporal_delete_timestamptz));
    loader.RegisterFunction(ScalarFunction(
        "deleteTime", {TGEOM, SetTypes::tstzset()}, TGEOM,
        TemporalFunctions::Temporal_delete_tstzset));
    loader.RegisterFunction(ScalarFunction(
        "deleteTime", {TGEOM, SetTypes::tstzset(), LogicalType::BOOLEAN}, TGEOM,
        TemporalFunctions::Temporal_delete_tstzset));
    loader.RegisterFunction(ScalarFunction(
        "deleteTime", {TGEOM, SpanTypes::TSTZSPAN()}, TGEOM,
        TemporalFunctions::Temporal_delete_tstzspan));
    loader.RegisterFunction(ScalarFunction(
        "deleteTime", {TGEOM, SpanTypes::TSTZSPAN(), LogicalType::BOOLEAN}, TGEOM,
        TemporalFunctions::Temporal_delete_tstzspan));
    loader.RegisterFunction(ScalarFunction(
        "deleteTime", {TGEOM, SpansetTypes::tstzspanset()}, TGEOM,
        TemporalFunctions::Temporal_delete_tstzspanset));
    loader.RegisterFunction(ScalarFunction(
        "deleteTime", {TGEOM, SpansetTypes::tstzspanset(), LogicalType::BOOLEAN}, TGEOM,
        TemporalFunctions::Temporal_delete_tstzspanset));

    // ---- Comparison (named functions + operators) ----
    struct CmpEntry {
        const char *name;
        scalar_function_t fn;
    };
    const std::vector<CmpEntry> named_cmps = {
        {"temporal_eq", TemporalFunctions::Temporal_eq},
        {"temporal_ne", TemporalFunctions::Temporal_ne},
        {"temporal_lt", TemporalFunctions::Temporal_lt},
        {"temporal_le", TemporalFunctions::Temporal_le},
        {"temporal_gt", TemporalFunctions::Temporal_gt},
        {"temporal_ge", TemporalFunctions::Temporal_ge},
    };
    for (const auto &c : named_cmps) {
        loader.RegisterFunction(ScalarFunction(
            c.name, {TGEOM, TGEOM}, LogicalType::BOOLEAN, c.fn));
    }
    loader.RegisterFunction(ScalarFunction(
        "temporal_cmp", {TGEOM, TGEOM}, LogicalType::INTEGER,
        TemporalFunctions::Temporal_cmp));

    // Operator forms — mirror the registrations tgeometry.cpp does.
    const std::vector<CmpEntry> op_cmps = {
        {"=",  TemporalFunctions::Temporal_eq},
        {"<>", TemporalFunctions::Temporal_ne},
        {"<",  TemporalFunctions::Temporal_lt},
        {"<=", TemporalFunctions::Temporal_le},
        {">",  TemporalFunctions::Temporal_gt},
        {">=", TemporalFunctions::Temporal_ge},
    };
    for (const auto &c : op_cmps) {
        loader.RegisterFunction(ScalarFunction(
            c.name, {TGEOM, TGEOM}, LogicalType::BOOLEAN, c.fn));
    }
}

void TCBufferTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType( "TCBUFFER", TCBufferTypes::TCBUFFER());
}


}
