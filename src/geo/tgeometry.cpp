#include "geo/tgeometry.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include <regex>
#include <string>
#include <temporal/span.hpp>
#include<time_util.hpp>

extern "C" {
    #include <meos.h>
    #include <meos_geo.h>
    #include <meos_internal.h>
    #include <meos_internal_geo.h>
}


namespace duckdb {

LogicalType TGeometryTypes::TGEOMETRY() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("TGEOMETRY");
    return type;
}

LogicalType TGeometryTypes::MEOS_WKB_BLOB() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("WKB_BLOB");
    return type;
}
/*
 * Constructors
*/

inline void Tgeo_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_geom_vec = args.data[0];
    
    UnaryExecutor::Execute<string_t, string_t>(
        input_geom_vec, result, count, 
        [&](string_t input_geom_str) -> string_t {
            std::string input = input_geom_str.GetString();
            
            Temporal *tinst = tgeometry_in(input.c_str());
            if (!tinst) {
                throw InvalidInputException("Invalid TGEOMETRY input: " + input);
            }
            
            size_t data_size = temporal_mem_size(tinst);
            
            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(tinst);  
                throw InvalidInputException("Failed to allocate memory for TGEOMETRY data");
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

inline void Tgeoinst_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &value_vec = args.data[0];
    auto &t_vec = args.data[1];

    BinaryExecutor::Execute<string_t, timestamp_tz_t, string_t>(
        value_vec, t_vec, result, count,
        [&](string_t value_str, timestamp_tz_t t) -> string_t {
            std::string value = value_str.GetString();
            
            GSERIALIZED *gs = geom_in(value.c_str(), -1); // -1 for no typmod constraint
            
            if (gs == NULL) {
                throw InvalidInputException("Invalid geometry format: " + value);
            }

            timestamp_tz_t meos_timestamp = DuckDBToMeosTimestamp(t);
            TInstant *inst = tgeoinst_make(gs, static_cast<TimestampTz>(meos_timestamp.value));

            if (inst == NULL) {
                free(gs);
                throw InvalidInputException("Failed to create TInstant");
            }

            size_t data_size = temporal_mem_size((Temporal*)inst);

            uint8_t *data_buffer = (uint8_t *)malloc(data_size);

            if (!data_buffer){
                free(inst);
                throw InvalidInputException("Failed to allocate memory to geometry data");
            }
            memcpy(data_buffer, inst, data_size);

            string_t data_string_t(reinterpret_cast<const char*>(data_buffer),data_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, data_string_t);
            
            free(data_buffer);
            free(inst);  
            
            return stored_data;

        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


inline void Tsequence_from_base_tstzspan(DataChunk &args, ExpressionState &state, Vector &result) {
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

            GSERIALIZED *gs = geom_in(geom_value.c_str(), -1);
            
            if(gs == NULL){
                throw InvalidInputException("Invalid geometry format: "+ geom_value);
            }
            
            std::string input = span_str.GetString();
            
            Span *span_cmp = reinterpret_cast<Span*>(const_cast<char*>(input.c_str()));

            // Use default interpolation or provided value
            interpType interp = interptype_from_string(default_interp);
            if (interp_vec) {
                std::string interp_string = default_interp; 
                interp = interptype_from_string(interp_string.c_str());
            }

            TSequence *seq = tsequence_from_base_tstzspan(Datum(gs), T_TGEOMETRY, span_cmp, interp);

            if (seq == NULL) {
                free(gs);
                throw InvalidInputException("Failed to create TSequence");
            }

            size_t seq_size = temporal_mem_size((Temporal*)seq);

            uint8_t *seq_buffer = (uint8_t *)malloc(seq_size);
            if (!seq_buffer) {
                free(seq);
                free(gs);
                throw InvalidInputException("Failed to allocate memory for sequence data");
            }

            memcpy(seq_buffer, seq, seq_size);

            string_t seq_string_t((char*) seq_buffer, seq_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, seq_string_t);

            free(seq_buffer);
            free(seq);
            free(gs);

            return stored_data;

        });

    if (count == 1){
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

TInstant **temparr_extract(Vector &tgeometry_arr_vec, list_entry_t list_entry, int *count) {
    auto &child_vector = ListVector::GetEntry(tgeometry_arr_vec);
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

inline void Tsequence_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    // Default values
    const char* default_interp = "step";
    bool default_lower_inc = true;
    bool default_upper_inc = true;
    
    auto count = args.size();
    auto arg_count = args.ColumnCount();
    
    
    auto &tgeometry_arr_vec = args.data[0];    
    tgeometry_arr_vec.Flatten(count);
    
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
    
    auto tgeometry_data = FlatVector::GetData<list_entry_t>(tgeometry_arr_vec);
    auto result_data = FlatVector::GetData<string_t>(result);
    
    // Get validity masks
    auto &tgeometry_validity = FlatVector::Validity(tgeometry_arr_vec);
    auto &result_validity = FlatVector::Validity(result);
    
    for (idx_t i = 0; i < count; i++) {
        if (!tgeometry_validity.RowIsValid(i)) {
            result_validity.SetInvalid(i);
            continue;
        }
        
        try {
            list_entry_t list_entry = tgeometry_data[i];
            
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
            TInstant **instants = temparr_extract(tgeometry_arr_vec, list_entry, &element_count);
            
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
                throw InvalidInputException("Invalid TGEOMETRY data: null pointer");
            }

            Span *timespan = temporal_to_tstzspan(temp);
            
            if (!timespan) {
                throw InvalidInputException("Failed to extract timespan from TGEOMETRY");
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
                throw InvalidInputException("Invalid TGEOMETRY data: null pointer");
            }

            TInstant *inst = temporal_to_tinstant(temp);
            if (!inst) {
                throw InvalidInputException("Failed to convert TGEOMETRY to TInstant");
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
                throw InvalidInputException("Invalid TGEOMETRY data: null pointer");
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
                throw InvalidInputException("Invalid TGEOMETRY data: null pointer");
            }

            std::string tgeom2 = tgeom2_str_t.GetString();

            Temporal *temp2 = reinterpret_cast<Temporal*>(const_cast<char*>(tgeom2.c_str()));
            if (!temp2) {
                throw InvalidInputException("Invalid TGEOMETRY data: null pointer");
            }
            
            Temporal *result_temp = temporal_merge(temp1, temp2);
            if (!result_temp) {
                throw InvalidInputException("Failed to merge temporal geometries");
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
                throw InvalidInputException("Invalid TGEOMETRY data: null pointer");
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
                throw InvalidInputException("Invalid TGEOMETRY data: null pointer");
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
                throw InvalidInputException("Invalid TGEOMETRY data: null pointer");
            }
            
            size_t mem_size = temporal_mem_size(temp);
            
            
            return static_cast<int32_t>(mem_size);
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

inline void Tinstant_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];
    
    // Direct geometry to geometry conversion, no string conversion needed
    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            TInstant *tinst = reinterpret_cast<TInstant*>(const_cast<char*>(input.c_str()));
            
            Datum geo = tinstant_value(tinst);
            
            GSERIALIZED *geom = DatumGetGserializedP(geo);
            
            size_t ewkb_size;
            uint8_t *ewkb_data = geo_as_ewkb(geom, NULL, &ewkb_size);

            
            string_t ewkb_string(reinterpret_cast<const char*>(ewkb_data), ewkb_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, ewkb_string);

            free(ewkb_data);
            
            return stored_result;
        });
    
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}



inline void Temporal_start_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];
    
    // Direct geometry to geometry conversion, no string conversion needed
    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();
            
            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));
            
            Datum start_datum = temporal_start_value(temp);
            
            GSERIALIZED *start_geom = DatumGetGserializedP(start_datum);
            
            size_t ewkb_size;
            uint8_t *ewkb_data = geo_as_ewkb(start_geom, NULL, &ewkb_size);

            
            string_t ewkb_string(reinterpret_cast<const char*>(ewkb_data), ewkb_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, ewkb_string);

            free(ewkb_data);
            
            return stored_result;
        });
    
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


inline void Temporal_end_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];
    
    // Direct geometry to geometry conversion, no string conversion needed
    UnaryExecutor::Execute<string_t, string_t>(
        input_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();
            
            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));
            
            Datum start_datum = temporal_end_value(temp);
            
            GSERIALIZED *start_geom = DatumGetGserializedP(start_datum);
            
            size_t ewkb_size;
            uint8_t *ewkb_data = geo_as_ewkb(start_geom, NULL, &ewkb_size);

            
            string_t ewkb_string(reinterpret_cast<const char*>(ewkb_data), ewkb_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, ewkb_string);

            free(ewkb_data);
            
            return stored_result;
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
                throw InvalidInputException("Failed to allocate memory for result");
            }
            
            memcpy(result_buffer, start_inst, result_size);
            string_t result_string_t(reinterpret_cast<const char*>(result_buffer), result_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, result_string_t);
            
            free(result_buffer);
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
                throw InvalidInputException("Failed to allocate memory for result");
            }
            
            memcpy(result_buffer, end_inst, result_size);
            string_t result_string_t(reinterpret_cast<const char*>(result_buffer), result_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, result_string_t);
            
            free(result_buffer);
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
                throw InvalidInputException("Failed to allocate memory for result");
            }
            
            memcpy(result_buffer, inst_n, result_size);
            string_t result_string_t(reinterpret_cast<const char*>(result_buffer), result_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, result_string_t);
            
            free(result_buffer);
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
                throw InvalidInputException("Invalid TGEOMETRY data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TGEOMETRY deserialization");
            }
            memcpy(data_copy, data, data_size);

            TInstant *temp = reinterpret_cast<TInstant*>(data_copy);
            
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMETRY data: null pointer");
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

inline void ExecuteTGeometrySeq(DataChunk &args, ExpressionState &state, Vector &result) {
    const char* default_interp = "step";
    auto count = args.size();
    auto &tgeometry_vec = args.data[0];
    
    Vector interp_vec(LogicalType::VARCHAR, count);
    if (args.data.size() > 1) {
        interp_vec.Reference(args.data[1]);
    } else {
        for (idx_t i = 0; i < count; i++) {
            FlatVector::GetData<string_t>(interp_vec)[i] = StringVector::AddString(interp_vec, default_interp);
        }
    }
    
    BinaryExecutor::Execute<string_t, string_t, string_t>(
        tgeometry_vec, interp_vec, result, count,
        [&](string_t tgeom_blob, string_t interp_str) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t data_size = tgeom_blob.GetSize();
            
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid TGEOMETRY data: insufficient size");
            }

            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            if (!data_copy) {
                throw InvalidInputException("Failed to allocate memory for TGEOMETRY deserialization");
            }
            memcpy(data_copy, data, data_size);

            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMETRY data: null pointer");
            }
            
            std::string interp_string = interp_str.GetString();
            if (interp_string.empty()) {
                interp_string = default_interp;
            }
            
            interpType interp = interptype_from_string(interp_string.c_str());
            TSequence *seq = temporal_to_tsequence(temp, interp);
            
            if (!seq) {
                free(data_copy);
                throw InvalidInputException("Failed to create TSequence");
            }
            
            size_t seq_data_size = temporal_mem_size(reinterpret_cast<Temporal*>(seq));
            uint8_t *seq_data_buffer = (uint8_t*)malloc(seq_data_size);
            if (!seq_data_buffer) {
                free(data_copy);
                free(seq);
                throw InvalidInputException("Failed to allocate memory for TSequence data");
            }
            
            memcpy(seq_data_buffer, seq, seq_data_size);
            
            string_t seq_data_string_t(reinterpret_cast<const char*>(seq_data_buffer), seq_data_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, seq_data_string_t);
            
            free(seq_data_buffer);
            free(data_copy);
            free(seq);
            
            return stored_data;
        });
    
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}




void TGeometryTypes::RegisterScalarFunctions(DatabaseInstance &instance) {

    auto tgeometry_function = ScalarFunction(
        "TGEOMETRY", 
        {LogicalType::VARCHAR}, 
        TGeometryTypes::TGEOMETRY(),
        Tgeo_constructor
    );
    ExtensionUtil::RegisterFunction(instance, tgeometry_function);
        
    auto tgeometry_from_timestamp_function = ScalarFunction(
        "TGEOMETRY",
        {LogicalType::VARCHAR, LogicalType::TIMESTAMP_TZ}, 
        TGeometryTypes::TGEOMETRY(), 
        Tgeoinst_constructor);
    ExtensionUtil::RegisterFunction(instance, tgeometry_from_timestamp_function);

     auto tgeometry_from_tstzspan_function = ScalarFunction(
        "TGEOMETRY", 
        {LogicalType::VARCHAR, SpanTypes::TSTZSPAN(), LogicalType::VARCHAR}, 
        TGeometryTypes::TGEOMETRY(),  
        Tsequence_from_base_tstzspan
    );
    ExtensionUtil::RegisterFunction(instance, tgeometry_from_tstzspan_function);

    auto tgeometry_from_tstzspan_default = ScalarFunction(
        "TGEOMETRY", 
        {LogicalType::VARCHAR, SpanTypes::TSTZSPAN()}, 
        TGeometryTypes::TGEOMETRY(),  
        Tsequence_from_base_tstzspan
    );
    ExtensionUtil::RegisterFunction(instance, tgeometry_from_tstzspan_default);

     auto tgeometryseqarr_1param= ScalarFunction(
        "tgeometrySeq", 
        {LogicalType::LIST(TGeometryTypes::TGEOMETRY())},
        TGeometryTypes::TGEOMETRY(),
        Tsequence_constructor
    );
    ExtensionUtil::RegisterFunction(instance, tgeometryseqarr_1param);

    auto tgeometryseqarr_2params = ScalarFunction(
        "tgeometrySeq", 
        {LogicalType::LIST(TGeometryTypes::TGEOMETRY()), LogicalType::VARCHAR},
        TGeometryTypes::TGEOMETRY(),
        Tsequence_constructor
    );
    ExtensionUtil::RegisterFunction(instance, tgeometryseqarr_2params);

    auto tgeometryseqarr_3params = ScalarFunction(
        "tgeometrySeq", 
        {LogicalType::LIST(TGeometryTypes::TGEOMETRY()), LogicalType::VARCHAR, LogicalType::BOOLEAN},
        TGeometryTypes::TGEOMETRY(),
        Tsequence_constructor
    );
    ExtensionUtil::RegisterFunction(instance, tgeometryseqarr_3params);

    auto tgeometryseqarr_4params = ScalarFunction(
        "tgeometrySeq", 
        {LogicalType::LIST(TGeometryTypes::TGEOMETRY()), LogicalType::VARCHAR, LogicalType::BOOLEAN, LogicalType::BOOLEAN},
        TGeometryTypes::TGEOMETRY(),
        Tsequence_constructor
    );
    ExtensionUtil::RegisterFunction(instance, tgeometryseqarr_4params);

    auto tgeometry_to_timespan_function = ScalarFunction(
        "timeSpan",
        {TGeometryTypes::TGEOMETRY()},     
        SpanTypes::TSTZSPAN(),               
        Temporal_to_tstzspan);
    ExtensionUtil::RegisterFunction(instance, tgeometry_to_timespan_function);

    auto tgeometry_to_tinstant_function = ScalarFunction(
        "tgeometryInst",
        {TGeometryTypes::TGEOMETRY()},
        TGeometryTypes::TGEOMETRY(),  
        Temporal_to_tinstant);
    ExtensionUtil::RegisterFunction(instance, tgeometry_to_tinstant_function);


    auto setInterp_function = ScalarFunction(
        "setInterp",
        {TGeometryTypes::TGEOMETRY(), LogicalType::VARCHAR},
        TGeometryTypes::TGEOMETRY(),
        Temporal_set_interp
    );
    ExtensionUtil::RegisterFunction(instance, setInterp_function);


    auto merge_function = ScalarFunction(
        "merge",
        {TGeometryTypes::TGEOMETRY(), TGeometryTypes::TGEOMETRY()},
        TGeometryTypes::TGEOMETRY(),
        Temporal_merge
    );
    ExtensionUtil::RegisterFunction(instance, merge_function);

    auto tempSubtype_function = ScalarFunction(
        "tempSubtype",
        {TGeometryTypes::TGEOMETRY()},
        LogicalType::VARCHAR,
        Temporal_subtype
    );
    ExtensionUtil::RegisterFunction(instance, tempSubtype_function);

    auto interp_function = ScalarFunction(
        "interp",
        {TGeometryTypes::TGEOMETRY()},
        LogicalType::VARCHAR,
        Temporal_interp
    );
    ExtensionUtil::RegisterFunction(instance, interp_function);

    auto memSize_function = ScalarFunction(
        "memSize",
        {TGeometryTypes::TGEOMETRY()},
        LogicalType::INTEGER,
        Temporal_mem_size
    );
    ExtensionUtil::RegisterFunction(instance, memSize_function);

    auto getValue_function = ScalarFunction(
        "getValue",
        {TGeometryTypes::TGEOMETRY()},
        TGeometryTypes::MEOS_WKB_BLOB(),
        Tinstant_value
    );
    ExtensionUtil::RegisterFunction(instance, getValue_function);
    

    auto tgeometry_start_value_function = ScalarFunction(
        "startValue", 
        {TGeometryTypes::TGEOMETRY()},
        TGeometryTypes::MEOS_WKB_BLOB(),
        Temporal_start_value
    );
    ExtensionUtil::RegisterFunction(instance, tgeometry_start_value_function);

    auto tgeometry_end_value_function = ScalarFunction(
        "endValue", 
        {TGeometryTypes::TGEOMETRY()},
        TGeometryTypes::MEOS_WKB_BLOB(),
        Temporal_end_value
    );
    ExtensionUtil::RegisterFunction(instance, tgeometry_end_value_function);

    auto startInstant_function = ScalarFunction(
        "startInstant",
        {TGeometryTypes::TGEOMETRY()},
        TGeometryTypes::TGEOMETRY(), 
        Temporal_start_instant
    );
    ExtensionUtil::RegisterFunction(instance, startInstant_function);

    auto endInstant_function = ScalarFunction(
        "endInstant",
        {TGeometryTypes::TGEOMETRY()},
        TGeometryTypes::TGEOMETRY(), 
        Temporal_end_instant
    );
    ExtensionUtil::RegisterFunction(instance, endInstant_function);

    auto instantN_function = ScalarFunction(
        "instantN",
        {TGeometryTypes::TGEOMETRY(), LogicalType::INTEGER},
        TGeometryTypes::TGEOMETRY(),  
        Temporal_instant_n
    );
    ExtensionUtil::RegisterFunction(instance, instantN_function);


    auto tgeometry_gettimestamptz_function = ScalarFunction(
        "getTimestamp",
        {TGeometryTypes::TGEOMETRY()},
        LogicalType::TIMESTAMP_TZ,  
        Tinstant_timestamptz);
    ExtensionUtil::RegisterFunction(instance, tgeometry_gettimestamptz_function);
    

}

void TGeometryTypes::RegisterTypes(DatabaseInstance &instance) {
    ExtensionUtil::RegisterType(instance, "TGEOMETRY", TGeometryTypes::TGEOMETRY());
}


}