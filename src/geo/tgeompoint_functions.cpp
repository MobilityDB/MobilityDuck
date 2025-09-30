#include "meos_wrapper_simple.hpp"
#include "common.hpp"

#include "geo/tgeompoint_functions.hpp"
#include "time_util.hpp"

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
    BinaryExecutor::Execute<string_t, timestamp_tz_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t wkb_blob, timestamp_tz_t ts_duckdb) -> string_t {
            const uint8_t *wkb_data = reinterpret_cast<const uint8_t*>(wkb_blob.GetData());
            size_t wkb_size = wkb_blob.GetSize();
            if (!wkb_data || wkb_size == 0) {
                throw InvalidInputException("Empty WKB_BLOB input");
            }

            int32 srid = 0;
            GSERIALIZED *gs = geo_from_ewkb(wkb_data, wkb_size, srid);
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
            int32 srid = 0;
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

            GSERIALIZED *gs = tpoint_trajectory(temp, true);
            if (!gs) {
                free(temp);
                throw InvalidInputException("Failed to get trajectory from TGEOMPOINT");
            }

            // NOTE: using geo_as_ewkb() results in unpredictable segmentation faults
            // Using geo_as_ewkt() as a workaround
            char *ewkt_data = geo_as_ewkt(gs, 10);
            size_t ewkt_size = strlen(ewkt_data);
            if (!ewkt_data) {
                free(gs);
                free(temp);
                throw InvalidInputException("Failed to convert trajectory to EWKB");
            }

            string_t ewkb_string(ewkt_data, ewkt_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ewkb_string);

            free(ewkt_data);
            free(gs);
            free(temp);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

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
            int32 srid = 0;
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

} // namespace duckdb