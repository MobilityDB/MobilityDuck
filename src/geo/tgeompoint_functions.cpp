#include "meos_wrapper_simple.hpp"
#include "common.hpp"

#include "geo/tgeompoint_functions.hpp"
#include "temporal/temporal_functions.hpp"
#include "time_util.hpp"
#include "duckdb/common/types/timestamp.hpp"

#include "duckdb/common/exception.hpp"

namespace duckdb {

/* ***************************************************
 * In/out functions
 ****************************************************/

bool TgeompointFunctions::Tpoint_in(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_geom_str) -> string_t {
            std::string input_str = input_geom_str.GetString();

            Temporal *temp = tgeompoint_in(input_str.c_str());
            if (!temp) {
                throw InvalidInputException("Invalid TGEOMPOINT input: " + input_str);
            }

            size_t data_size = temporal_mem_size(temp);
            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(temp);
                throw InvalidInputException("Failed to allocate memory for TGEOMPOINT");
            }

            memcpy(data_buffer, temp, data_size);

            string_t output(reinterpret_cast<const char *>(data_buffer), data_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, output);

            free(data_buffer);
            free(temp);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
    return true;
}

void TgeompointFunctions::Tspatial_as_text(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid TGEOMPOINT data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT input: " + input_blob.GetString());
            }

            char* ret = tspatial_as_text(temp, 15);
            if (!ret) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TGEOMPOINT to text: " + input_blob.GetString());
            }
            std::string ret_string(ret);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_string);

            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tspatial_as_ewkt(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid TGEOMPOINT data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT input: " + input_blob.GetString());
            }

            char *ewkt = tspatial_as_ewkt(temp, OUT_DEFAULT_DECIMAL_DIGITS);
            if (!ewkt) {
                free(data_copy);
                throw InvalidInputException("Failed to convert TGEOMPOINT to EWKT: " + input_blob.GetString());
            }
            std::string ret_string(ewkt);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_string);

            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
* Constructor functions
****************************************************/

void TgeompointFunctions::Tpointinst_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto row_count = args.size();
    auto arg_count = args.ColumnCount();
    int32_t srid = 0;
    if (arg_count > 2) {
        auto &srid_child = args.data[2];
        srid_child.Flatten(row_count);
        srid = srid_child.GetValue(0).GetValue<int32_t>();
    }
    
    BinaryExecutor::Execute<string_t, timestamp_tz_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t wkb_blob, timestamp_tz_t ts_duckdb) -> string_t {
            const uint8_t *wkb_data = reinterpret_cast<const uint8_t*>(wkb_blob.GetData());
            size_t wkb_size = wkb_blob.GetSize();
            if (!wkb_data || wkb_size == 0) {
                throw InvalidInputException("Empty WKB_BLOB input");
            }

            GSERIALIZED *gs = geo_from_ewkb(wkb_data, wkb_size, (int32)srid);
            if (!gs) {
                throw InvalidInputException("Failed to parse WKB_BLOB into a geometry");
            }

            timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
            Temporal *ret = (Temporal *) tpointinst_make(gs, static_cast<TimestampTz>(ts_meos.value));
            if (ret == NULL) {
                free(gs);
                throw InvalidInputException("Failed to create TGEOMPOINT from geometry and timestamp");
            }

            size_t ret_size = temporal_mem_size(ret);
            char *ret_data = (char *)malloc(ret_size);
            if (!ret_data) {
                free(ret);
                free(gs);
                throw InvalidInputException("Failed to allocate memory for TGEOMPOINT");
            }
            memcpy(ret_data, ret, ret_size);

            string_t temp_str(ret_data, ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, temp_str);

            free(ret);
            free(gs);
            free(ret_data);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

inline void Tspatial_to_stbox_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
                return string_t();
            }
            STBox *stbox = tspatial_to_stbox(temp);
            if (!stbox) {
                free(temp);
                throw InvalidInputException("Failed to convert TGEOMPOINT to STBOX");
                return string_t();
            }

            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(temp);
                throw InvalidInputException("Failed to allocate memory for STBOX");
                return string_t();
            }
            memcpy(stbox_data, stbox, stbox_size);
            string_t stbox_string(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, stbox_string);

            free(stbox_data);
            free(stbox);
            free(temp);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tspatial_to_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    Tspatial_to_stbox_common(args.data[0], result, args.size());
}

bool TgeompointFunctions::Tspatial_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Tspatial_to_stbox_common(source, result, count);
    return true;
}

void TgeompointFunctions::Tgeompoint_start_value(DataChunk &args, ExpressionState &state, Vector &result) {
    // Adapted from Temporal_start_value
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
                return string_t();
            }

            Datum start_datum = temporal_start_value(temp);
            GSERIALIZED *start_geom = DatumGetGserializedP(start_datum);
            if (!start_geom) {
                free(temp);
                throw InvalidInputException("Failed to get start value from TGEOMPOINT");
            }

            size_t ewkb_size;
            uint8_t *ewkb_data = geo_as_ewkb(start_geom, NULL, &ewkb_size);
            if (!ewkb_data) {
                free(temp);
                throw InvalidInputException("Failed to convert start geometry to EWKB");
            }

            string_t ewkb_string(reinterpret_cast<const char*>(ewkb_data), ewkb_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, ewkb_string);

            free(ewkb_data);
            free(temp);
            return stored_result;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tgeompoint_end_value(DataChunk &args, ExpressionState &state, Vector &result) {
    // Adapted from Temporal_end_value
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
                return string_t();
            }

            Datum end_datum = temporal_end_value(temp);
            GSERIALIZED *end_geom = DatumGetGserializedP(end_datum);
            if (!end_geom) {
                free(temp);
                throw InvalidInputException("Failed to get end value from TGEOMPOINT");
            }

            size_t ewkb_size;
            uint8_t *ewkb_data = geo_as_ewkb(end_geom, NULL, &ewkb_size);
            if (!ewkb_data) {
                free(temp);
                throw InvalidInputException("Failed to convert end geometry to EWKB");
            }

            string_t ewkb_string(reinterpret_cast<const char*>(ewkb_data), ewkb_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, ewkb_string);

            free(ewkb_data);
            free(temp);
            return stored_result;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tgeompoint_sequence_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &child_vec = ListVector::GetEntry(args.data[0]);
    auto child_count = ListVector::GetListSize(args.data[0]);

    UnifiedVectorFormat input_vdata;
    child_vec.ToUnifiedFormat(child_count, input_vdata);
    
    auto arg_count = args.ColumnCount();
    auto row_count = args.size();
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

    UnaryExecutor::Execute<list_entry_t, string_t>(
        args.data[0], result, args.size(),
        [&](const list_entry_t &entry) {
            const auto offset = entry.offset;
            const auto length = entry.length;

            int32_t valid_count = 0;
            for (idx_t out_idx = offset; out_idx < offset + length; out_idx++) {
                const auto row_idx = input_vdata.sel->get_index(out_idx);
                if (!input_vdata.validity.RowIsValid(row_idx)) {
                    continue;
                }

                auto &blob = UnifiedVectorFormat::GetData<string_t>(input_vdata)[row_idx];
                size_t data_size = blob.GetSize();
                if (data_size < sizeof(void*)) {
                    continue;
                }
                valid_count++;
            }

            TInstant **instants = (TInstant **)malloc(valid_count * sizeof(TInstant *));
            if (!instants) {
                throw InternalException("Memory allocation failed in Tgeompoint_sequence_constructor");
            }

            idx_t valid_idx = 0;
            for (idx_t out_idx = offset; out_idx < offset + length; out_idx++) {
                const auto row_idx = input_vdata.sel->get_index(out_idx);
                if (!input_vdata.validity.RowIsValid(row_idx)) {
                    continue;
                }

                auto &blob = UnifiedVectorFormat::GetData<string_t>(input_vdata)[row_idx];
                const uint8_t *data = reinterpret_cast<const uint8_t*>(blob.GetData());
                size_t data_size = blob.GetSize();
                if (data_size < sizeof(void*)) {
                    continue;
                }
                uint8_t *data_copy = (uint8_t*)malloc(data_size);
                memcpy(data_copy, data, data_size);
                Temporal *temp = reinterpret_cast<Temporal*>(data_copy);

                if (!temp) {
                    free(data_copy);
                    throw InvalidInputException("Failure in Tgeompoint_sequence_constructor: unable to convert WKB to temporal");
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
                throw InternalException("Failure in Tgeompoint_sequence_constructor: unable to create sequence");
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
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Conversion functions
 ****************************************************/

inline void Temporal_to_tstzspan_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid Temporal data: insufficient size");
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
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Temporal_to_tstzspan(DataChunk &args, ExpressionState &state, Vector &result) {
    Temporal_to_tstzspan_common(args.data[0], result, args.size());
}

bool TgeompointFunctions::Temporal_to_tstzspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Temporal_to_tstzspan_common(source, result, count);
    return true;
}

/* ***************************************************
 * Accessor functions
 ****************************************************/

void TgeompointFunctions::Tgeompoint_value(DataChunk &args, ExpressionState &state, Vector &result) {
    // Adapted from Tinstant_value
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t tgeom_blob) -> string_t {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!temp) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            if (temp->subtype != TINSTANT) {
                free(temp);
                throw InvalidInputException("The temporal value must be of subtype Instant");
            }
            Datum ret = tinstant_value((TInstant*)temp);
            GSERIALIZED *gs = DatumGetGserializedP(ret);
            if (!gs) {
                free(temp);
                throw InvalidInputException("Failed to get geometry from datum");
            }

            size_t ewkb_size;
            uint8_t *ewkb_data = geo_as_ewkb(gs, NULL, &ewkb_size);
            if (!ewkb_data) {
                free(temp);
                free(gs);
                throw InvalidInputException("Failed to convert geometry to EWKB");
            }

            string_t ewkb_string(reinterpret_cast<const char*>(ewkb_data), ewkb_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ewkb_string);

            free(ewkb_data);
            free(gs);
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

void TgeompointFunctions::Tgeompoint_at_value(DataChunk &args, ExpressionState &state, Vector &result) {
    // Adapted from Temporal_at_value
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t wkb_blob, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!temp) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            const uint8_t *wkb_data = reinterpret_cast<const uint8_t*>(wkb_blob.GetData());
            size_t wkb_size = wkb_blob.GetSize();
            int32 srid = tspatial_srid(temp);
            GSERIALIZED *gs = geo_from_ewkb(wkb_data, wkb_size, srid);
            if (!gs) {
                free(temp);
                throw InvalidInputException("Invalid geometry format: " + wkb_blob.GetString());
            }

            Temporal *ret = temporal_restrict_value(temp, (Datum)gs, true);
            if (!ret) {
                free(temp);
                free(gs);
                mask.SetInvalid(idx);
                return string_t();
            }

            size_t ret_size = temporal_mem_size(ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);

            string_t ret_string(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_string);
            
            free(ret_data);
            free(ret);
            free(temp);
            free(gs);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tgeompoint_value_at_timestamptz(DataChunk &args, ExpressionState &state, Vector &result) {
    // Adapted from Temporal_value_at_timestamptz
    BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_blob, timestamp_tz_t ts_duckdb, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
            Datum ret;
            bool found = temporal_value_at_timestamptz(temp, (TimestampTz)ts_meos.value, true, &ret);
            free(temp);
            if (!found) {
                mask.SetInvalid(idx);
                return string_t();
            }

            GSERIALIZED *gs = DatumGetGserializedP(ret);
            if (!gs) {
                throw InvalidInputException("Failed to get geometry from datum");
            }

            size_t ewkb_size;
            uint8_t *ewkb_data = geo_as_ewkb(gs, NULL, &ewkb_size);
            if (!ewkb_data) {
                free(gs);
                throw InvalidInputException("Failed to convert geometry to EWKB");
            }

            string_t ewkb_string(reinterpret_cast<const char*>(ewkb_data), ewkb_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ewkb_string);

            free(ewkb_data);
            free(gs);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Spatial functions
 ****************************************************/

void TgeompointFunctions::Tpoint_length(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, double>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> double {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            double ret = tpoint_length(temp);
            free(temp);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tpoint_trajectory(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
            if (!temp) {
                free(data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            GSERIALIZED *gs = tpoint_trajectory(temp, false);
            if (!gs) {
                free(temp);
                throw InvalidInputException("Failed to get trajectory from TGEOMPOINT");
            }

            size_t ewkb_size = 0;
            uint8_t *ewkb_data = geo_as_ewkb(gs, NULL, &ewkb_size);
            // uint8_t *ewkb_data = geo_as_ewkb_duckdb(gs, NULL, &ewkb_size);
            if (!ewkb_data) {
                free(temp);
                throw InvalidInputException("Failed to convert trajectory to EWKB");
            }
            string_t ewkb_string(reinterpret_cast<const char*>(ewkb_data), ewkb_size);
            string_t stored_result = StringVector::AddStringOrBlob(result, ewkb_string);

            free(ewkb_data);
            free(gs);
            free(temp);
            return stored_result;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// void TgeompointFunctions::Tpoint_trajectory_gs(DataChunk &args, ExpressionState &state, Vector &result) {
//     UnaryExecutor::Execute<string_t, string_t>(
//         args.data[0], result, args.size(),
//         [&](string_t input_blob) -> string_t {
//             const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
//             size_t data_size = input_blob.GetSize();
//             uint8_t *data_copy = (uint8_t*)malloc(data_size);
//             memcpy(data_copy, data, data_size);
//             Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
//             if (!temp) {
//                 free(data_copy);
//                 throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
//             }

//             GSERIALIZED *gs = tpoint_trajectory(temp, false);
//             if (!gs) {
//                 free(temp);
//                 throw InvalidInputException("Failed to get trajectory from TGEOMPOINT");
//             }

//             size_t gs_size = VARSIZE(gs);
//             uint8_t *gs_data = (uint8_t*)malloc(gs_size);
//             memcpy(gs_data, gs, gs_size);
//             string_t gs_string(reinterpret_cast<const char*>(gs_data), gs_size);
//             string_t stored_result = StringVector::AddStringOrBlob(result, gs_string);
//             free(gs_data);
//             free(temp);
//             return stored_result;
//         }
//     );
//     if (args.size() == 1) {
//         result.SetVectorType(VectorType::CONSTANT_VECTOR);
//     }
// }

void TgeompointFunctions::Tgeo_at_geom(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t wkb_blob, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            const uint8_t *wkb_data = reinterpret_cast<const uint8_t*>(wkb_blob.GetData());
            size_t wkb_size = wkb_blob.GetSize();
            int32 srid = tspatial_srid(tgeom);
            GSERIALIZED *gs = geo_from_ewkb(wkb_data, wkb_size, srid);
            if (!gs) {
                free(tgeom);
                throw InvalidInputException("Invalid geometry format: " + wkb_blob.GetString());
            }

            Temporal *ret = tgeo_at_geom(tgeom, gs);
            if (!ret) {
                free(tgeom);
                free(gs);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            string_t ret_string(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_string);
            free(ret_data);
            free(ret);
            free(gs);
            free(tgeom);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Tspatial_transform(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, int32_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, int32_t srid) {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            Temporal *ret = tspatial_transform(tgeom, srid);
            if (ret == NULL) {
                free(tgeom);
                throw InvalidInputException("Failed to transform TGEOMPOINT");
            }

            size_t ret_size = temporal_mem_size(ret);
            char *ret_data = (char *)malloc(ret_size);
            if (!ret_data) {
                free(ret);
                free(tgeom);
                throw InvalidInputException("Failed to allocate memory for TGEOMPOINT");
            }
            memcpy(ret_data, ret, ret_size);

            string_t ret_str(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);

            free(ret_data);
            free(ret);
            free(tgeom);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Spatial relationships
 ****************************************************/

void TgeompointFunctions::Adisjoint_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom1_blob, string_t tgeom2_blob, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom1_data = reinterpret_cast<const uint8_t*>(tgeom1_blob.GetData());
            size_t tgeom1_data_size = tgeom1_blob.GetSize();
            uint8_t *tgeom1_data_copy = (uint8_t*)malloc(tgeom1_data_size);
            memcpy(tgeom1_data_copy, tgeom1_data, tgeom1_data_size);
            Temporal *tgeom1 = reinterpret_cast<Temporal*>(tgeom1_data_copy);
            if (!tgeom1) {
                free(tgeom1_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            
            const uint8_t *tgeom2_data = reinterpret_cast<const uint8_t*>(tgeom2_blob.GetData());
            size_t tgeom2_data_size = tgeom2_blob.GetSize();
            uint8_t *tgeom2_data_copy = (uint8_t*)malloc(tgeom2_data_size);
            memcpy(tgeom2_data_copy, tgeom2_data, tgeom2_data_size);
            Temporal *tgeom2 = reinterpret_cast<Temporal*>(tgeom2_data_copy);
            if (!tgeom2) {
                free(tgeom2_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            // extern int adisjoint_tgeo_tgeo(const Temporal *temp1, const Temporal *temp2);
            int ret = adisjoint_tgeo_tgeo(tgeom1, tgeom2);
            free(tgeom1);
            free(tgeom2);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Edwithin_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    TernaryExecutor::ExecuteWithNulls<string_t, string_t, double, bool>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t tgeom1_blob, string_t tgeom2_blob, double dist, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom1_data = reinterpret_cast<const uint8_t*>(tgeom1_blob.GetData());
            size_t tgeom1_data_size = tgeom1_blob.GetSize();
            uint8_t *tgeom1_data_copy = (uint8_t*)malloc(tgeom1_data_size);
            memcpy(tgeom1_data_copy, tgeom1_data, tgeom1_data_size);
            Temporal *tgeom1 = reinterpret_cast<Temporal*>(tgeom1_data_copy);
            if (!tgeom1) {
                free(tgeom1_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            const uint8_t *tgeom2_data = reinterpret_cast<const uint8_t*>(tgeom2_blob.GetData());
            size_t tgeom2_data_size = tgeom2_blob.GetSize();
            uint8_t *tgeom2_data_copy = (uint8_t*)malloc(tgeom2_data_size);
            memcpy(tgeom2_data_copy, tgeom2_data, tgeom2_data_size);
            Temporal *tgeom2 = reinterpret_cast<Temporal*>(tgeom2_data_copy);
            if (!tgeom2) {
                free(tgeom2_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            int ret = edwithin_tgeo_tgeo(tgeom1, tgeom2, dist);
            free(tgeom1);
            free(tgeom2);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Eintersects_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t wkb_blob, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            const uint8_t *wkb_data = reinterpret_cast<const uint8_t*>(wkb_blob.GetData());
            size_t wkb_size = wkb_blob.GetSize();
            if (!wkb_data || wkb_size == 0) {
                throw InvalidInputException("Empty WKB_BLOB input");
            }

            int32 srid = tspatial_srid(tgeom);
            GSERIALIZED *gs = geo_from_ewkb(wkb_data, wkb_size, (int32)srid);
            if (!gs) {
                throw InvalidInputException("Failed to parse WKB_BLOB into a geometry");
            }

            int ret = eintersects_tgeo_geo(tgeom, gs);
            free(tgeom);
            free(gs);
            if (ret < 0) {
                mask.SetInvalid(idx);
                return false;
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Temporal-spatial relationships
 ****************************************************/

void TgeompointFunctions::Tdwithin_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    TernaryExecutor::ExecuteWithNulls<string_t, string_t, double, string_t>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t tgeom1_blob, string_t tgeom2_blob, double dist, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *tgeom1_data = reinterpret_cast<const uint8_t*>(tgeom1_blob.GetData());
            size_t tgeom1_data_size = tgeom1_blob.GetSize();
            uint8_t *tgeom1_data_copy = (uint8_t*)malloc(tgeom1_data_size);
            memcpy(tgeom1_data_copy, tgeom1_data, tgeom1_data_size);
            Temporal *tgeom1 = reinterpret_cast<Temporal*>(tgeom1_data_copy);
            if (!tgeom1) {
                free(tgeom1_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            const uint8_t *tgeom2_data = reinterpret_cast<const uint8_t*>(tgeom2_blob.GetData());
            size_t tgeom2_data_size = tgeom2_blob.GetSize();
            uint8_t *tgeom2_data_copy = (uint8_t*)malloc(tgeom2_data_size);
            memcpy(tgeom2_data_copy, tgeom2_data, tgeom2_data_size);
            Temporal *tgeom2 = reinterpret_cast<Temporal*>(tgeom2_data_copy);
            if (!tgeom2) {
                free(tgeom2_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            // extern Temporal *tdwithin_tgeo_tgeo(const Temporal *temp1, const Temporal *temp2, double dist, bool restr, bool atvalue);
            Temporal *ret = tdwithin_tgeo_tgeo(tgeom1, tgeom2, dist, false, false);
            if (!ret) {
                free(tgeom1);
                free(tgeom2);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            string_t ret_string(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_string);
            free(ret_data);
            free(ret);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Operators (workaround as functions)
 ****************************************************/

void TgeompointFunctions::Temporal_overlaps_tgeompoint_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t stbox_blob) -> bool {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            const uint8_t *stbox_data = reinterpret_cast<const uint8_t*>(stbox_blob.GetData());
            size_t stbox_data_size = stbox_blob.GetSize();
            uint8_t *stbox_data_copy = (uint8_t*)malloc(stbox_data_size);
            memcpy(stbox_data_copy, stbox_data, stbox_data_size);
            STBox *stbox = reinterpret_cast<STBox*>(stbox_data_copy);
            if (!stbox) {
                free(stbox_data_copy);
                throw InvalidInputException("Invalid STBOX data: null pointer");
            }
            bool ret = overlaps_tspatial_stbox(tgeom, stbox);
            free(tgeom);
            free(stbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Temporal_overlaps_tgeompoint_tstzspan(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t span_blob) -> bool {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            
            const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
            size_t span_data_size = span_blob.GetSize();
            uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
            memcpy(span_data_copy, span_data, span_data_size);
            Span *span = reinterpret_cast<Span*>(span_data_copy);
            if (!span) {
                free(span_data_copy);
                throw InvalidInputException("Invalid TSTZSPAN data: null pointer");
            }
            bool ret = overlaps_tstzspan_temporal(span, tgeom);
            free(tgeom);
            free(span);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void TgeompointFunctions::Temporal_contains_tgeompoint_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom_blob, string_t stbox_blob) -> bool {
            const uint8_t *tgeom_data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
            size_t tgeom_data_size = tgeom_blob.GetSize();
            uint8_t *tgeom_data_copy = (uint8_t*)malloc(tgeom_data_size);
            memcpy(tgeom_data_copy, tgeom_data, tgeom_data_size);
            Temporal *tgeom = reinterpret_cast<Temporal*>(tgeom_data_copy);
            if (!tgeom) {
                free(tgeom_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            const uint8_t *stbox_data = reinterpret_cast<const uint8_t*>(stbox_blob.GetData());
            size_t stbox_data_size = stbox_blob.GetSize();
            uint8_t *stbox_data_copy = (uint8_t*)malloc(stbox_data_size);
            memcpy(stbox_data_copy, stbox_data, stbox_data_size);
            STBox *stbox = reinterpret_cast<STBox*>(stbox_data_copy);
            if (!stbox) {
                free(stbox_data_copy);
                throw InvalidInputException("Invalid STBOX data: null pointer");
            }
            bool ret = contains_tspatial_stbox(tgeom, stbox);
            free(tgeom);
            free(stbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Distance functions
 ****************************************************/

void TgeompointFunctions::Tdistance_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tgeom1_blob, string_t tgeom2_blob, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *tgeom1_data = reinterpret_cast<const uint8_t*>(tgeom1_blob.GetData());
            size_t tgeom1_data_size = tgeom1_blob.GetSize();
            uint8_t *tgeom1_data_copy = (uint8_t*)malloc(tgeom1_data_size);
            memcpy(tgeom1_data_copy, tgeom1_data, tgeom1_data_size);
            Temporal *tgeom1 = reinterpret_cast<Temporal*>(tgeom1_data_copy);
            if (!tgeom1) {
                free(tgeom1_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }
            
            const uint8_t *tgeom2_data = reinterpret_cast<const uint8_t*>(tgeom2_blob.GetData());
            size_t tgeom2_data_size = tgeom2_blob.GetSize();
            uint8_t *tgeom2_data_copy = (uint8_t*)malloc(tgeom2_data_size);
            memcpy(tgeom2_data_copy, tgeom2_data, tgeom2_data_size);
            Temporal *tgeom2 = reinterpret_cast<Temporal*>(tgeom2_data_copy);
            if (!tgeom2) {
                free(tgeom2_data_copy);
                throw InvalidInputException("Invalid TGEOMPOINT data: null pointer");
            }

            Temporal *ret = tdistance_tgeo_tgeo(tgeom1, tgeom2);
            if (!ret) {
                free(tgeom1);
                free(tgeom2);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = temporal_mem_size(ret);
            uint8_t *ret_data = (uint8_t*)malloc(ret_size);
            memcpy(ret_data, ret, ret_size);
            string_t ret_string(reinterpret_cast<const char*>(ret_data), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_string);
            free(ret_data);
            free(ret);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// void TgeompointFunctions::gs_as_text(DataChunk &args, ExpressionState &state, Vector &result) {
//     UnaryExecutor::Execute<string_t, string_t>(
//         args.data[0], result, args.size(),
//         [&](string_t blob) -> string_t {
//             const uint8_t *data = reinterpret_cast<const uint8_t*>(blob.GetData());
//             size_t data_size = blob.GetSize();
//             uint8_t *data_copy = (uint8_t*)malloc(data_size);
//             memcpy(data_copy, data, data_size);
//             GSERIALIZED *gs = reinterpret_cast<GSERIALIZED*>(data_copy);
//             if (!gs) {
//                 free(data_copy);
//                 throw InvalidInputException("Invalid GSERIALIZED data: null pointer");
//             }
//             char *gs_text = geo_as_ewkt(gs, 10);
//             if (!gs_text) {
//                 free(data_copy);
//                 throw InvalidInputException("Failed to convert GSERIALIZED to text");
//             }
//             string_t gs_text_string(gs_text);
//             string_t stored_result = StringVector::AddStringOrBlob(result, gs_text_string);
//             free(gs);
//             return stored_result;
//         }
//     );
//     if (args.size() == 1) {
//         result.SetVectorType(VectorType::CONSTANT_VECTOR);
//     }
// }

// void TgeompointFunctions::collect_gs(DataChunk &args, ExpressionState &state, Vector &result) {
//     auto &array_vec = args.data[0];
//     array_vec.Flatten(args.size());
//     auto *list_entries = ListVector::GetData(array_vec);
//     auto &child_vec = ListVector::GetEntry(array_vec);
//     child_vec.Flatten(ListVector::GetListSize(array_vec));
//     auto child_data = FlatVector::GetData<string_t>(child_vec);

//     UnaryExecutor::Execute<list_entry_t, string_t>(
//         array_vec, result, args.size(),
//         [&](const list_entry_t &list) {
//             auto offset = list.offset;
//             auto length = list.length;
//             GSERIALIZED **gsarr = (GSERIALIZED **)malloc(length * sizeof(GSERIALIZED *));
//             if (!gsarr) {
//                 throw InternalException("Failed to allocate memory for GSERIALIZED array");
//             }
//             for (idx_t i = 0; i < length; i++) {
//                 idx_t child_idx = offset + i;
//                 auto wkb_data = child_data[child_idx];
//                 size_t data_size = wkb_data.GetSize();
//                 if (data_size < sizeof(void*)) {
//                     free(gsarr);
//                     throw InvalidInputException("Invalid BLOB data: insufficient size");
//                 }
//                 uint8_t *data_copy = (uint8_t*)malloc(data_size);
//                 memcpy(data_copy, wkb_data.GetData(), data_size);
//                 GSERIALIZED *gs = reinterpret_cast<GSERIALIZED*>(data_copy);
//                 if (!gs) {
//                     free(data_copy);
//                     throw InvalidInputException("Invalid GSERIALIZED data: null pointer");
//                 }
//                 gsarr[i] = gs;
//                 // char *gs_text = geo_as_ewkt(gs, 10);
//                 // printf("GS %ld: %s\n", i, gs_text);
//             }

//             GSERIALIZED *gs_out = geo_collect_garray(gsarr, (int)length);
//             if (!gs_out) {
//                 free(gsarr);
//                 throw InvalidInputException("Failed to collect GSERIALIZED array");
//             }

//             size_t gs_out_size = VARSIZE(gs_out);
//             uint8_t *gs_out_data = (uint8_t*)malloc(gs_out_size);
//             memcpy(gs_out_data, gs_out, gs_out_size);
//             string_t gs_out_string(reinterpret_cast<const char*>(gs_out_data), gs_out_size);
//             string_t stored_result = StringVector::AddStringOrBlob(result, gs_out_string);
//             free(gs_out_data);
//             free(gs_out);
//             for (idx_t i = 0; i < length; i++) {
//                 free(gsarr[i]);
//             }
//             free(gsarr);
//             return stored_result;
//         }
//     );
//     if (args.size() == 1) {
//         result.SetVectorType(VectorType::CONSTANT_VECTOR);
//     }
// }

// void TgeompointFunctions::distance_geo_geo(DataChunk &args, ExpressionState &state, Vector &result) {
//     BinaryExecutor::Execute<string_t, string_t, double>(
//         args.data[0], args.data[1], result, args.size(),
//         [&](string_t blob1, string_t blob2) -> double {
//             const uint8_t *data1 = reinterpret_cast<const uint8_t*>(blob1.GetData());
//             size_t data1_size = blob1.GetSize();
//             uint8_t *data1_copy = (uint8_t*)malloc(data1_size);
//             memcpy(data1_copy, data1, data1_size);
//             GSERIALIZED *gs1 = reinterpret_cast<GSERIALIZED*>(data1_copy);
//             if (!gs1) {
//                 free(data1_copy);
//                 throw InvalidInputException("Invalid GSERIALIZED data 1: null pointer");
//             }

//             const uint8_t *data2 = reinterpret_cast<const uint8_t*>(blob2.GetData());
//             size_t data2_size = blob2.GetSize();
//             uint8_t *data2_copy = (uint8_t*)malloc(data2_size);
//             memcpy(data2_copy, data2, data2_size);
//             GSERIALIZED *gs2 = reinterpret_cast<GSERIALIZED*>(data2_copy);
//             if (!gs2) {
//                 free(data2_copy);
//                 throw InvalidInputException("Invalid GSERIALIZED data 2: null pointer");
//             }

//             double distance = geom_distance2d(gs1, gs2);
//             free(gs1);
//             free(gs2);
//             return distance;
//         }
//     );
//     if (args.size() == 1) {
//         result.SetVectorType(VectorType::CONSTANT_VECTOR);
//     }
// }

} // namespace duckdb