#include "meos_wrapper_simple.hpp"
#include "common.hpp"

#include "geo/stbox_functions.hpp"
#include "time_util.hpp"
#include "geo_util.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/common/typedefs.hpp"

#include <string>

#include "spatial/spatial_types.hpp"
#include "spatial/geometry/wkb_writer.hpp"

namespace duckdb {

namespace {

inline void Stbox_normalize_geodetic_srid(STBox *box) {
    if ((stbox_isgeodetic(box) || MEOS_FLAGS_GET_GEODETIC(box->flags)) && box->srid == 0) {
        box->srid = 4326;
    }
}

} // namespace

/* ***************************************************
 * In/out functions: VARCHAR <-> STBOX
 ****************************************************/

inline void Stbox_in_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(
        source, result, count,
        [&](string_t input_string, ValidityMask &mask, idx_t idx) -> string_t {
            std::string input_str = input_string.GetString();
            STBox *stbox = stbox_in(input_str.c_str());
            if (!stbox) {
                throw InternalException("Failure in Stbox_in: unable to cast string to stbox");
                return string_t();
            }
            if (input_str.find("GEODSTBOX") != std::string::npos) {
                MEOS_FLAGS_SET_GEODETIC(stbox->flags, true);
            }
            Stbox_normalize_geodetic_srid(stbox);
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(stbox);
                throw InternalException("Failure in Stbox_in: unable to allocate memory for stbox");
                return string_t();
            }
            memcpy(stbox_data, stbox, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(stbox_data);
            free(stbox);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

bool StboxFunctions::Stbox_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Stbox_in_common(source, result, count);
    return true;
}

void StboxFunctions::Stbox_in(DataChunk &args, ExpressionState &state, Vector &result) {
    Stbox_in_common(args.data[0], result, args.size());
}

bool StboxFunctions::Stbox_out(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    bool success = true;
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_blob) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_blob.GetData());
            size_t data_size = input_blob.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_out: unable to cast binary to stbox");
            }
            char *ret = stbox_out(stbox, OUT_DEFAULT_DECIMAL_DIGITS);
            if (!ret) {
                free(data_copy);
                throw InternalException("Failure in Stbox_out: unable to cast binary to stbox");
            }
            std::string ret_str(ret);
            free(ret);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);

            free(stbox);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
    return success;
}

/* ***************************************************
 * In/out functions: WKB/HexWKB <-> STBOX
 ****************************************************/

void StboxFunctions::Stbox_from_wkb(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_wkb) -> string_t {
            uint8_t *wkb = nullptr;
            if (input_wkb.GetSize() > 0) {
                wkb = (uint8_t*)malloc(input_wkb.GetSize());
                memcpy(wkb, input_wkb.GetData(), input_wkb.GetSize());
            }
            if (!wkb) {
                throw InternalException("Failure in Stbox_from_wkb: unable to allocate memory for wkb");
                return string_t();
            }
            STBox *stbox = stbox_from_wkb(wkb, input_wkb.GetSize());
            if (!stbox) {
                free(wkb);
                throw InternalException("Failure in Stbox_from_wkb: unable to cast wkb to stbox");
                return string_t();
            }
            Stbox_normalize_geodetic_srid(stbox);
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(stbox);
                free(wkb);
                throw InternalException("Failure in Stbox_in: unable to allocate memory for stbox");
                return string_t();
            }
            memcpy(stbox_data, stbox, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(stbox_data);
            free(stbox);
            free(wkb);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_from_hexwkb(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_hexwkb) -> string_t {
            char *hexwkb = (char*)input_hexwkb.GetData();
            STBox *stbox = stbox_from_hexwkb(hexwkb);
            if (!stbox) {
                throw InternalException("Failure in Stbox_from_hexwkb: unable to cast hexwkb to stbox");
                return string_t();
            }
            Stbox_normalize_geodetic_srid(stbox);
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(stbox);
                throw InternalException("Failure in Stbox_in: unable to allocate memory for stbox");
                return string_t();
            }
            memcpy(stbox_data, stbox, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(stbox_data);
            free(stbox);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_as_text(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_as_text: unable to cast binary to stbox");
            }
            int dbl_dig_for_wkt = OUT_DEFAULT_DECIMAL_DIGITS;
            char *str = stbox_out(stbox, dbl_dig_for_wkt);
            if (!str) {
                free(stbox);
                throw InternalException("Failure in Stbox_as_text: stbox_out returned null");
            }
            std::string ret_str(str);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(str);
            free(stbox);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_as_wkb(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_as_wkb: unable to cast binary to stbox");
            }
            size_t wkb_size = sizeof(STBox);
            uint8_t *wkb = stbox_as_wkb(stbox, WKB_EXTENDED, &wkb_size);
            if (!wkb) {
                free(stbox);
                throw InternalException("Failure in Stbox_as_wkb: unable to cast stbox to wkb");
                return string_t();
            }
            string_t ret_str(reinterpret_cast<const char*>(wkb), wkb_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(wkb);
            free(stbox);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_as_hexwkb(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_as_hexwkb: unable to cast binary to stbox");
            }
            size_t wkb_size = sizeof(STBox);
            char *wkb = stbox_as_hexwkb(stbox, WKB_EXTENDED, &wkb_size);
            if (!wkb) {
                free(stbox);
                throw InternalException("Failure in Stbox_as_hexwkb: unable to cast stbox to hexwkb");
                return string_t();
            }
            string_t ret_str(wkb);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(wkb);
            free(stbox);
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

void StboxFunctions::Geo_timestamptz_to_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t geometry_blob, timestamp_tz_t ts_duckdb, ValidityMask &mask, idx_t idx) -> string_t {
            int32 srid = 0;
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
                return string_t();
            }
            timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
            STBox *ret = geo_timestamptz_to_stbox(gs, (TimestampTz)ts_meos.value);
            if (!ret) {
                free(gs);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(ret);
                free(gs);
                throw InternalException("Failure in Geo_timestamptz_to_stbox: unable to allocate memory for stbox");
                return string_t();
            }
            memcpy(stbox_data, ret, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(stbox_data);
            free(ret);
            free(gs);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Geo_tstzspan_to_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t geometry_blob, string_t span_blob, ValidityMask &mask, idx_t idx) -> string_t {
            int32 srid = 0;
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
                return string_t();
            }
            const uint8_t *span_data = reinterpret_cast<const uint8_t*>(span_blob.GetData());
            size_t span_data_size = span_blob.GetSize();
            if (span_data_size < sizeof(Span)) {
                free(gs);
                throw InvalidInputException("Invalid TSTZSPAN data: insufficient size");
            }
            uint8_t *span_data_copy = (uint8_t*)malloc(span_data_size);
            if (!span_data_copy) {
                free(gs);
                throw InternalException("Failure in Geo_tstzspan_to_stbox: unable to allocate span copy");
            }
            memcpy(span_data_copy, span_data, span_data_size);
            Span *span = reinterpret_cast<Span*>(span_data_copy);
            if (!span) {
                free(gs);
                free(span_data_copy);
                throw InvalidInputException("Invalid TSTZSPAN data: null pointer");
            }

            STBox *ret = geo_tstzspan_to_stbox(gs, span);
            if (!ret) {
                free(gs);
                free(span);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(ret);
                free(span);
                free(gs);
                throw InternalException("Failure in Geo_tstzspan_to_stbox: unable to allocate memory for stbox");
                return string_t();
            }
            memcpy(stbox_data, ret, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(stbox_data);
            free(ret);
            free(span);
            free(gs);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Conversion functions + cast functions: [TYPE] -> STBOX
 ****************************************************/

void StboxFunctions::Geo_to_stbox_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(
        source, result, count,
        [&](string_t geometry_blob, ValidityMask &mask, idx_t idx) -> string_t {
            int32 srid = 0;
            GSERIALIZED *gs = GeometryToGSerialized(geometry_blob, srid);
            if (!gs) {
                throw InvalidInputException("Invalid geometry format: " + geometry_blob.GetString());
                return string_t();
            }
            STBox *ret = geo_to_stbox(gs);
            if (!ret) {
                free(gs);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(ret);
                free(gs);
                throw InternalException("Failure in Geo_to_stbox: unable to allocate memory for stbox");
                return string_t();
            }
            memcpy(stbox_data, ret, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(stbox_data);
            free(ret);
            free(gs);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Geo_to_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    Geo_to_stbox_common(args.data[0], result, args.size());
}

bool StboxFunctions::Geo_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Geo_to_stbox_common(source, result, count);
    return true;
}

/* ***************************************************
 * Conversion functions + cast functions: STBOX -> [TYPE]
 ****************************************************/

 void StboxFunctions::Stbox_to_geo(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_expand_space: unable to cast binary to stbox");
            }

            GSERIALIZED *gs = stbox_to_geo(stbox);
            if (!gs) {
                free(stbox);
                throw InvalidInputException("Failed to convert stbox to geometry");
            }

            string_t geometry_blob = GSerializedToGeometry(gs, state, result);
            string_t stored_result = StringVector::AddStringOrBlob(result, geometry_blob);

            free(gs);
            free(stbox);
            return stored_result;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

static void Timestamptz_to_stbox_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<timestamp_tz_t, string_t>(
        source, result, count,
        [&](timestamp_tz_t ts_duckdb) -> string_t {
            timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
            STBox *ret = timestamptz_to_stbox((TimestampTz)ts_meos.value);
            if (!ret) {
                throw InternalException("Failure in Timestamptz_to_stbox: unable to convert timestamptz to stbox");
            }
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(ret);
                throw InternalException("Failure in Timestamptz_to_stbox: unable to allocate memory for stbox");
            }
            memcpy(stbox_data, ret, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(stbox_data);
            free(ret);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Timestamptz_to_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    Timestamptz_to_stbox_common(args.data[0], result, args.size());
}

bool StboxFunctions::Timestamptz_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Timestamptz_to_stbox_common(source, result, count);
    return true;
}

static void Tstzset_to_stbox_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_stbox) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(Set)) {
                throw InvalidInputException("Invalid TSTZSET data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Set *set = reinterpret_cast<Set*>(data_copy);
            if (!set) {
                free(data_copy);
                throw InternalException("Failure in Tstzset_to_stbox: unable to cast binary to set");
            }
            STBox *ret = tstzset_to_stbox(set);
            if (!ret) {
                free(set);
                throw InternalException("Failure in Tstzset_to_stbox: unable to convert tstzset to stbox");
            }
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(ret);
                free(set);
                throw InternalException("Failure in Tstzset_to_stbox: unable to allocate memory for stbox");
            }
            memcpy(stbox_data, ret, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(stbox_data);
            free(ret);
            free(set);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Tstzset_to_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    Tstzset_to_stbox_common(args.data[0], result, args.size());
}

bool StboxFunctions::Tstzset_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Tstzset_to_stbox_common(source, result, count);
    return true;
}

static void Tstzspan_to_stbox_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_stbox) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(Span)) {
                throw InvalidInputException("Invalid TSTZSPAN data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            Span *span = reinterpret_cast<Span*>(data_copy);
            if (!span) {
                free(data_copy);
                throw InternalException("Failure in Tstzspan_to_stbox: unable to cast binary to span");
            }
            STBox *ret = tstzspan_to_stbox(span);
            if (!ret) {
                free(span);
                throw InternalException("Failure in Tstzspan_to_stbox: unable to convert tstzspan to stbox");
            }
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(ret);
                free(span);
                throw InternalException("Failure in Tstzspan_to_stbox: unable to allocate memory for stbox");
            }
            memcpy(stbox_data, ret, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(stbox_data);
            free(ret);
            free(span);
            return stored_data;
        }
    );
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Tstzspan_to_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    Tstzspan_to_stbox_common(args.data[0], result, args.size());
}

bool StboxFunctions::Tstzspan_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Tstzspan_to_stbox_common(source, result, count);
    return true;
}   

static void Tstzspanset_to_stbox_common(Vector &source, Vector &result, idx_t count) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input_stbox) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(SpanSet)) {
                throw InvalidInputException("Invalid TSTZSPANSET data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            SpanSet *set = reinterpret_cast<SpanSet*>(data_copy);
            if (!set) {
                free(data_copy);
                throw InternalException("Failure in Tstzspanset_to_stbox: unable to cast binary to span set");
            }
            STBox *ret = tstzspanset_to_stbox(set);
            if (!ret) {
                free(set);
                throw InternalException("Failure in Tstzspanset_to_stbox: unable to convert tstzspanset to stbox");
            }
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(ret);
                free(set);
                throw InternalException("Failure in Tstzspanset_to_stbox: unable to allocate memory for stbox");
            }
            memcpy(stbox_data, ret, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(stbox_data);
            free(ret);
            free(set);
            return stored_data;
        }
    );
}

void StboxFunctions::Tstzspanset_to_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    Tstzspanset_to_stbox_common(args.data[0], result, args.size());
}

bool StboxFunctions::Tstzspanset_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Tstzspanset_to_stbox_common(source, result, count);
    return true;
}

/* ***************************************************
 * Accessor functions
 ****************************************************/

void StboxFunctions::Stbox_hasx(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, bool>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox) -> bool {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_hasx: unable to cast binary to stbox");
            }
            bool ret = stbox_hasx(stbox);
            free(stbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_hasz(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, bool>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox) -> bool {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_hasz: unable to cast binary to stbox");
            }
            bool ret = stbox_hasz(stbox);
            free(stbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_hast(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, bool>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox) -> bool {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_hast: unable to cast binary to stbox");
            }
            bool ret = stbox_hast(stbox);
            free(stbox);
            return ret;
        }
    );
}

void StboxFunctions::Stbox_isgeodetic(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, bool>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox) -> bool {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_isgeodetic: unable to cast binary to stbox");
            }
            bool ret = stbox_isgeodetic(stbox);
            free(stbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_xmin(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, double>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox, ValidityMask &mask, idx_t idx) -> double {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_xmin: unable to cast binary to stbox");
            }
            double ret;
            if (!stbox_xmin(stbox, &ret)) {
                free(stbox);
                mask.SetInvalid(idx);
                return double();
            }
            free(stbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_xmax(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, double>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox, ValidityMask &mask, idx_t idx) -> double {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_xmax: unable to cast binary to stbox");
            }
            double ret;
            if (!stbox_xmax(stbox, &ret)) {
                free(stbox);
                mask.SetInvalid(idx);
                return double();
            }
            free(stbox);
            return ret;
        }
    );

    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_ymin(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, double>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox, ValidityMask &mask, idx_t idx) -> double {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_ymin: unable to cast binary to stbox");
            }
            double ret;
            if (!stbox_ymin(stbox, &ret)) {
                free(stbox);
                mask.SetInvalid(idx);
                return double();
            }
            free(stbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_ymax(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, double>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox, ValidityMask &mask, idx_t idx) -> double {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_ymax: unable to cast binary to stbox");
            }
            double ret;
            if (!stbox_ymax(stbox, &ret)) {
                free(stbox);
                mask.SetInvalid(idx);
                return double();
            }
            free(stbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_zmin(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, double>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox, ValidityMask &mask, idx_t idx) -> double {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_zmin: unable to cast binary to stbox");
            }
            double ret;
            if (!stbox_zmin(stbox, &ret)) {
                free(stbox);
                mask.SetInvalid(idx);
                return double();
            }
            free(stbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_zmax(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, double>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox, ValidityMask &mask, idx_t idx) -> double {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_zmax: unable to cast binary to stbox");
            }
            double ret;
            if (!stbox_zmax(stbox, &ret)) {
                free(stbox);
                mask.SetInvalid(idx);
                return double();
            }
            free(stbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_tmin(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox, ValidityMask &mask, idx_t idx) -> timestamp_tz_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_tmin: unable to cast binary to stbox");
            }
            TimestampTz ret_meos;
            if (!stbox_tmin(stbox, &ret_meos)) {
                free(stbox);
                mask.SetInvalid(idx);
                return timestamp_tz_t();
            }
            timestamp_tz_t ret = MeosToDuckDBTimestamp((timestamp_tz_t)ret_meos);
            free(stbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_tmax(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox, ValidityMask &mask, idx_t idx) -> timestamp_tz_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_tmax: unable to cast binary to stbox");
            }
            TimestampTz ret_meos;
            if (!stbox_tmax(stbox, &ret_meos)) {
                free(stbox);
                mask.SetInvalid(idx);
                return timestamp_tz_t();
            }
            timestamp_tz_t ret = MeosToDuckDBTimestamp((timestamp_tz_t)ret_meos);
            free(stbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_tmin_inc(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, bool>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_tmin_inc: unable to cast binary to stbox");
            }
            bool ret;
            if (!stbox_tmin_inc(stbox, &ret)) {
                free(stbox);
                mask.SetInvalid(idx);
                return bool();
            }
            free(stbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_tmax_inc(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, bool>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox, ValidityMask &mask, idx_t idx) -> bool {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_tmax_inc: unable to cast binary to stbox");
            }
            bool ret;
            if (!stbox_tmax_inc(stbox, &ret)) {
                free(stbox);
                mask.SetInvalid(idx);
                return bool();
            }
            free(stbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_area(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, double>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox, ValidityMask &mask, idx_t idx) -> double {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_area: unable to cast binary to stbox");
            }
            bool spheroid = true; // default value, TODO: handle argument
            double ret = stbox_area(stbox, spheroid);
            free(stbox);
            /* MEOS stbox_area returns -1.0 on error (null box or no X dimension). */
            if (ret < 0) {
                mask.SetInvalid(idx);
                return double();
            }
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_volume(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::ExecuteWithNulls<string_t, double>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox, ValidityMask &mask, idx_t idx) -> double {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_volume: unable to cast binary to stbox");
            }
            double ret = stbox_volume(stbox);
            if (!stbox_volume(stbox)) {
                free(stbox);
                mask.SetInvalid(idx);
                return double();
            }
            free(stbox);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Transformation functions
 ****************************************************/
void StboxFunctions::Stbox_shift_time(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, interval_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox, interval_t interval, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_shift_time: unable to cast binary to stbox");
            }
            MeosInterval shift = IntervaltToInterval(interval);
            STBox *ret = stbox_shift_scale_time(stbox, &shift, NULL);
            if (!ret) {
                free(stbox);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = sizeof(STBox);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(stbox);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_scale_time(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, interval_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox, interval_t interval, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_scale_time: unable to cast binary to stbox");
            }
            MeosInterval duration = IntervaltToInterval(interval);
            STBox *ret = stbox_shift_scale_time(stbox, NULL, &duration);
            if (!ret) {
                free(stbox);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = sizeof(STBox);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(stbox);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_shift_scale_time(DataChunk &args, ExpressionState &state, Vector &result) {
    TernaryExecutor::Execute<string_t, interval_t, interval_t, string_t>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t input_stbox, interval_t duckdb_shift, interval_t duckdb_duration) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_shift_scale_time: unable to cast binary to stbox");
            }
            MeosInterval shift = IntervaltToInterval(duckdb_shift);
            MeosInterval duration = IntervaltToInterval(duckdb_duration);
            STBox *ret = stbox_shift_scale_time(stbox, &shift, &duration);
            if (!ret) {
                free(stbox);
                throw InternalException("Failure in Stbox_shift_scale_time: stbox_shift_scale_time returned null");
            }
            size_t ret_size = sizeof(STBox);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(stbox);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_get_space(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_get_space: unable to cast binary to stbox");
            }
            STBox *ret = stbox_get_space(stbox);
            if (!ret) {
                free(stbox);
                throw InternalException("Failure in Stbox_get_space: unable to get space");
            }
            size_t ret_size = sizeof(STBox);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(stbox);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_expand_time(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, interval_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox, interval_t interval, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size != sizeof(STBox)) {
                throw InvalidInputException("Invalid STBOX value size (MEOS ABI mismatch or corrupt value)");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_expand_time: unable to cast binary to stbox");
            }
            MeosInterval duration = IntervaltToInterval(interval);
            STBox *ret = stbox_expand_time(stbox, &duration);
            if (!ret) {
                free(stbox);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t ret_size = sizeof(STBox);
            string_t ret_str(reinterpret_cast<const char*>(ret), ret_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(ret);
            free(stbox);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_expand_space(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, double, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox, double d, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy = (uint8_t*)malloc(data_size);
            memcpy(data_copy, data, data_size);
            STBox *stbox = reinterpret_cast<STBox*>(data_copy);
            if (!stbox) {
                free(data_copy);
                throw InternalException("Failure in Stbox_expand_space: unable to cast binary to stbox");
            }

            STBox *ret = stbox_expand_space(stbox, d);
            if (!ret) {
                free(stbox);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(ret);
                free(stbox);
                throw InternalException("Failure in Stbox_expand_space: unable to allocate memory for stbox");
            }
            memcpy(stbox_data, ret, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(stbox_data);
            free(ret);
            free(stbox);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Topological operators
 ****************************************************/

void StboxFunctions::Overlaps_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Overlaps_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Overlaps_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Overlaps_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Overlaps_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = overlaps_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Contains_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Contains_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Contains_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Contains_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Contains_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = contains_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Contained_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Contained_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Contained_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Contained_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Contained_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = contained_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Same_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Same_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Same_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Same_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Same_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = same_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Adjacent_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Adjacent_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Adjacent_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Adjacent_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Adjacent_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = adjacent_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Left_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Left_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Left_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Left_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Left_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = left_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Overleft_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Overleft_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Overleft_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Overleft_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Overleft_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = overleft_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Right_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = right_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Overright_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = overright_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Below_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = below_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Overbelow_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = overbelow_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Above_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = above_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Overabove_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = overabove_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Before_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = before_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Overbefore_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = overbefore_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::After_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = after_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Overafter_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = overafter_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Front_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = front_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Overfront_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = overfront_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Back_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = back_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Overback_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> bool {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Right_stbox_stbox: unable to cast binary to stbox");
            }

            bool ret = overback_stbox_stbox(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Union_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> string_t {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Union_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Union_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Union_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Union_stbox_stbox: unable to cast binary to stbox");
            }

            STBox *ret = union_stbox_stbox(stbox1, stbox2, true);
            if (!ret) {
                free(stbox1);
                free(stbox2);
                throw InternalException("Failure in Union_stbox_stbox: unable to union stboxes");
            }
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(ret);
                free(stbox1);
                free(stbox2);
                throw InternalException("Failure in Union_stbox_stbox: unable to allocate memory for stbox");
            }
            memcpy(stbox_data, ret, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(stbox_data);
            free(ret);
            free(stbox1);
            free(stbox2);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Intersection_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> string_t {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy1 = (uint8_t*)malloc(data_size1);
            if (!data_copy1) {
                throw InternalException("Failure in Union_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Union_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                free(data_copy1);
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            if (!data_copy2) {
                free(data_copy1);
                throw InternalException("Failure in Union_stbox_stbox: unable to allocate stbox copy");
            }
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
                free(data_copy1);
                throw InternalException("Failure in Union_stbox_stbox: unable to cast binary to stbox");
            }

            STBox *ret = intersection_stbox_stbox(stbox1, stbox2);
            if (!ret) {
                free(stbox1);
                free(stbox2);
                throw InternalException("Failure in Union_stbox_stbox: unable to union stboxes");
            }
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
                free(ret);
                free(stbox1);
                free(stbox2);
                throw InternalException("Failure in Union_stbox_stbox: unable to allocate memory for stbox");
            }
            memcpy(stbox_data, ret, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(stbox_data);
            free(ret);
            free(stbox1);
            free(stbox2);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// Comparison operators
void StboxFunctions::Stbox_eq(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t stbox1_str, string_t stbox2_str) {
            STBox *stbox1 = nullptr;
            if (stbox1_str.GetSize() > 0) {
                stbox1 = (STBox*)malloc(stbox1_str.GetSize());
                memcpy(stbox1, stbox1_str.GetDataUnsafe(), stbox1_str.GetSize());
            }
            if (!stbox1) {
                throw InternalException("Failure in Stbox_eq: unable to cast binary to stbox");
            }
            STBox *stbox2 = nullptr;
            if (stbox2_str.GetSize() > 0) {
                stbox2 = (STBox*)malloc(stbox2_str.GetSize());
                memcpy(stbox2, stbox2_str.GetDataUnsafe(), stbox2_str.GetSize());
            }
            if (!stbox2) {
                free(stbox1);
                throw InternalException("Failure in Stbox_eq: unable to cast binary to stbox");
            }
            bool ret = stbox_eq(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_ne(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t stbox1_str, string_t stbox2_str) {
            STBox *stbox1 = nullptr;
            if (stbox1_str.GetSize() > 0) {
                stbox1 = (STBox*)malloc(stbox1_str.GetSize());
                memcpy(stbox1, stbox1_str.GetDataUnsafe(), stbox1_str.GetSize());
            }
            if (!stbox1) {
                throw InternalException("Failure in Stbox_ne: unable to cast binary to stbox");
            }
            STBox *stbox2 = nullptr;
            if (stbox2_str.GetSize() > 0) {
                stbox2 = (STBox*)malloc(stbox2_str.GetSize());
                memcpy(stbox2, stbox2_str.GetDataUnsafe(), stbox2_str.GetSize());
            }
            if (!stbox2) {
                free(stbox1);
                throw InternalException("Failure in Stbox_ne: unable to cast binary to stbox");
            }
            bool ret = stbox_ne(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_le(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t stbox1_str, string_t stbox2_str) {
            STBox *stbox1 = nullptr;
            if (stbox1_str.GetSize() > 0) {
                stbox1 = (STBox*)malloc(stbox1_str.GetSize());
                memcpy(stbox1, stbox1_str.GetDataUnsafe(), stbox1_str.GetSize());
            }
            if (!stbox1) {
                throw InternalException("Failure in Stbox_le: unable to cast binary to stbox");
            }
            STBox *stbox2 = nullptr;
            if (stbox2_str.GetSize() > 0) {
                stbox2 = (STBox*)malloc(stbox2_str.GetSize());
                memcpy(stbox2, stbox2_str.GetDataUnsafe(), stbox2_str.GetSize());
            }
            if (!stbox2) {
                free(stbox1);
                throw InternalException("Failure in Stbox_le: unable to cast binary to stbox");
            }
            bool ret = stbox_le(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_lt(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t stbox1_str, string_t stbox2_str) {
            STBox *stbox1 = nullptr;
            if (stbox1_str.GetSize() > 0) {
                stbox1 = (STBox*)malloc(stbox1_str.GetSize());
                memcpy(stbox1, stbox1_str.GetDataUnsafe(), stbox1_str.GetSize());
            }
            if (!stbox1) {
                throw InternalException("Failure in Stbox_lt: unable to cast binary to stbox");
            }
            STBox *stbox2 = nullptr;
            if (stbox2_str.GetSize() > 0) {
                stbox2 = (STBox*)malloc(stbox2_str.GetSize());
                memcpy(stbox2, stbox2_str.GetDataUnsafe(), stbox2_str.GetSize());
            }
            if (!stbox2) {
                free(stbox1);
                throw InternalException("Failure in Stbox_lt: unable to cast binary to stbox");
            }
            bool ret = stbox_lt(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_ge(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t stbox1_str, string_t stbox2_str) {
            STBox *stbox1 = nullptr;
            if (stbox1_str.GetSize() > 0) {
                stbox1 = (STBox*)malloc(stbox1_str.GetSize());
                memcpy(stbox1, stbox1_str.GetDataUnsafe(), stbox1_str.GetSize());
            }
            if (!stbox1) {
                throw InternalException("Failure in Stbox_ge: unable to cast binary to stbox");
            }
            STBox *stbox2 = nullptr;
            if (stbox2_str.GetSize() > 0) {
                stbox2 = (STBox*)malloc(stbox2_str.GetSize());
                memcpy(stbox2, stbox2_str.GetDataUnsafe(), stbox2_str.GetSize());
            }
            if (!stbox2) {
                free(stbox1);
                throw InternalException("Failure in Stbox_ge: unable to cast binary to stbox");
            }
            bool ret = stbox_ge(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_gt(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, bool>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t stbox1_str, string_t stbox2_str) {
            STBox *stbox1 = nullptr;
            if (stbox1_str.GetSize() > 0) {
                stbox1 = (STBox*)malloc(stbox1_str.GetSize());
                memcpy(stbox1, stbox1_str.GetDataUnsafe(), stbox1_str.GetSize());
            }
            if (!stbox1) {
                throw InternalException("Failure in Stbox_gt: unable to cast binary to stbox");
            }
            STBox *stbox2 = nullptr;
            if (stbox2_str.GetSize() > 0) {
                stbox2 = (STBox*)malloc(stbox2_str.GetSize());
                memcpy(stbox2, stbox2_str.GetDataUnsafe(), stbox2_str.GetSize());
            }
            if (!stbox2) {
                free(stbox1);
                throw InternalException("Failure in Stbox_gt: unable to cast binary to stbox");
            }
            bool ret = stbox_gt(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

void StboxFunctions::Stbox_cmp(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, int32_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t stbox1_str, string_t stbox2_str) {
            STBox *stbox1 = nullptr;
            if (stbox1_str.GetSize() > 0) {
                stbox1 = (STBox*)malloc(stbox1_str.GetSize());
                memcpy(stbox1, stbox1_str.GetDataUnsafe(), stbox1_str.GetSize());
            }
            if (!stbox1) {
                throw InternalException("Failure in Stbox_cmp: unable to cast binary to stbox");
            }
            STBox *stbox2 = nullptr;
            if (stbox2_str.GetSize() > 0) {
                stbox2 = (STBox*)malloc(stbox2_str.GetSize());
                memcpy(stbox2, stbox2_str.GetDataUnsafe(), stbox2_str.GetSize());
            }
            if (!stbox2) {
                free(stbox1);
                throw InternalException("Failure in Stbox_cmp: unable to cast binary to stbox");
            }
            int32_t ret = stbox_cmp(stbox1, stbox2);
            free(stbox1);
            free(stbox2);
            return ret;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/* ***************************************************
 * Tile / box emitters, single-tile getters, and tspatial topological predicates
 ****************************************************/

namespace {

inline STBox *BlobToStboxTile(string_t b) {
    size_t sz = b.GetSize();
    uint8_t *copy = (uint8_t *)malloc(sz);
    memcpy(copy, b.GetData(), sz);
    return reinterpret_cast<STBox *>(copy);
}

inline Temporal *BlobToTempTile(string_t b) {
    size_t sz = b.GetSize();
    uint8_t *copy = (uint8_t *)malloc(sz);
    memcpy(copy, b.GetData(), sz);
    return reinterpret_cast<Temporal *>(copy);
}

inline Temporal *BlobToTemp(string_t b) {
    size_t sz = b.GetSize();
    uint8_t *copy = (uint8_t *)malloc(sz);
    memcpy(copy, b.GetData(), sz);
    return reinterpret_cast<Temporal *>(copy);
}

string_t StboxToResultBlob(Vector &result, const STBox *box) {
    string_t blob(reinterpret_cast<const char *>(box), sizeof(STBox));
    return StringVector::AddStringOrBlob(result, blob);
}

void EmitStboxList(Vector &result, idx_t row, list_entry_t *list_entries,
                   STBox *boxes, int count, idx_t &total) {
    if (!boxes || count <= 0) {
        list_entries[row] = list_entry_t{total, 0};
        if (boxes) free(boxes);
        return;
    }
    ListVector::Reserve(result, total + count);
    ListVector::SetListSize(result, total + count);
    list_entries[row] = list_entry_t{total, static_cast<uint64_t>(count)};
    auto &child = ListVector::GetEntry(result);
    auto child_data = FlatVector::GetData<string_t>(child);
    for (int k = 0; k < count; k++) {
        string_t one(reinterpret_cast<const char *>(&boxes[k]), sizeof(STBox));
        child_data[total + k] = StringVector::AddStringOrBlob(child, one);
    }
    total += count;
    free(boxes);
}

GSERIALIZED *DefaultOriginPoint() {
    /* MEOS exports geompoint_make3dz; the SRID 0 / (0,0,0) origin matches
     * MobilityDB's `Point(0 0 0)` SQL DEFAULT for sorigin. */
    return geompoint_make3dz(0, 0.0, 0.0, 0.0);
}

inline STBox *BlobToStbox(string_t b) {
    size_t sz = b.GetSize();
    uint8_t *copy = (uint8_t *)malloc(sz);
    memcpy(copy, b.GetData(), sz);
    return reinterpret_cast<STBox *>(copy);
}

} // namespace

void StboxFunctions::Stbox_space_tiles(DataChunk &args, ExpressionState &state, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t i = 0; i < args.ColumnCount(); i++) args.data[i].Flatten(row_count);
    const idx_t cc = args.ColumnCount();
    auto in_box = FlatVector::GetData<string_t>(args.data[0]);
    auto in_xsz = FlatVector::GetData<double>(args.data[1]);
    auto in_ysz = FlatVector::GetData<double>(args.data[2]);
    auto in_zsz = FlatVector::GetData<double>(args.data[3]);
    const bool has_origin = cc > 4;
    const bool has_border = cc > 5;
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &out_validity = FlatVector::Validity(result);

    idx_t total = 0;
    for (idx_t row = 0; row < row_count; row++) {
        if (!FlatVector::Validity(args.data[0]).RowIsValid(row)) {
            out_validity.SetInvalid(row);
            list_entries[row] = list_entry_t{total, 0};
            continue;
        }
        STBox *bounds = BlobToStboxTile(in_box[row]);
        GSERIALIZED *origin = nullptr;
        if (has_origin) {
            origin = GeometryToGSerialized(FlatVector::GetData<string_t>(args.data[4])[row], bounds->srid);
        }
        if (!origin) origin = DefaultOriginPoint();
        bool border = has_border ? FlatVector::GetData<bool>(args.data[5])[row] : true;
        int count = 0;
        STBox *boxes = stbox_space_tiles(bounds, in_xsz[row], in_ysz[row], in_zsz[row],
                                          origin, border, &count);
        free(bounds); free(origin);
        EmitStboxList(result, row, list_entries, boxes, count, total);
    }
    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

void StboxFunctions::Stbox_time_tiles(DataChunk &args, ExpressionState &state, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t i = 0; i < args.ColumnCount(); i++) args.data[i].Flatten(row_count);
    const idx_t cc = args.ColumnCount();
    auto in_box = FlatVector::GetData<string_t>(args.data[0]);
    auto in_dur = FlatVector::GetData<interval_t>(args.data[1]);
    const bool has_torigin = cc > 2;
    const bool has_border  = cc > 3;
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &out_validity = FlatVector::Validity(result);

    idx_t total = 0;
    for (idx_t row = 0; row < row_count; row++) {
        if (!FlatVector::Validity(args.data[0]).RowIsValid(row)) {
            out_validity.SetInvalid(row);
            list_entries[row] = list_entry_t{total, 0};
            continue;
        }
        STBox *bounds = BlobToStboxTile(in_box[row]);
        MeosInterval mi = IntervaltToInterval(in_dur[row]);
        TimestampTz torigin = 0;
        if (has_torigin) {
            timestamp_tz_t t = FlatVector::GetData<timestamp_tz_t>(args.data[2])[row];
            torigin = (TimestampTz) DuckDBToMeosTimestamp(t).value;
        }
        bool border = has_border ? FlatVector::GetData<bool>(args.data[3])[row] : true;
        int count = 0;
        STBox *boxes = stbox_time_tiles(bounds, &mi, torigin, border, &count);
        free(bounds);
        EmitStboxList(result, row, list_entries, boxes, count, total);
    }
    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

void StboxFunctions::Stbox_space_time_tiles(DataChunk &args, ExpressionState &state, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t i = 0; i < args.ColumnCount(); i++) args.data[i].Flatten(row_count);
    const idx_t cc = args.ColumnCount();
    auto in_box = FlatVector::GetData<string_t>(args.data[0]);
    auto in_xsz = FlatVector::GetData<double>(args.data[1]);
    auto in_ysz = FlatVector::GetData<double>(args.data[2]);
    auto in_zsz = FlatVector::GetData<double>(args.data[3]);
    auto in_dur = FlatVector::GetData<interval_t>(args.data[4]);
    const bool has_origin  = cc > 5;
    const bool has_torigin = cc > 6;
    const bool has_border  = cc > 7;
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &out_validity = FlatVector::Validity(result);

    idx_t total = 0;
    for (idx_t row = 0; row < row_count; row++) {
        if (!FlatVector::Validity(args.data[0]).RowIsValid(row)) {
            out_validity.SetInvalid(row);
            list_entries[row] = list_entry_t{total, 0};
            continue;
        }
        STBox *bounds = BlobToStboxTile(in_box[row]);
        MeosInterval mi = IntervaltToInterval(in_dur[row]);
        GSERIALIZED *origin = nullptr;
        if (has_origin) {
            origin = GeometryToGSerialized(FlatVector::GetData<string_t>(args.data[5])[row], bounds->srid);
        }
        if (!origin) origin = DefaultOriginPoint();
        TimestampTz torigin = 0;
        if (has_torigin) {
            timestamp_tz_t t = FlatVector::GetData<timestamp_tz_t>(args.data[6])[row];
            torigin = (TimestampTz) DuckDBToMeosTimestamp(t).value;
        }
        bool border = has_border ? FlatVector::GetData<bool>(args.data[7])[row] : true;
        int count = 0;
        STBox *boxes = stbox_space_time_tiles(bounds, in_xsz[row], in_ysz[row], in_zsz[row],
                                               &mi, origin, torigin, border, &count);
        free(bounds); free(origin);
        EmitStboxList(result, row, list_entries, boxes, count, total);
    }
    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

void StboxFunctions::Tgeo_space_boxes(DataChunk &args, ExpressionState &state, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t i = 0; i < args.ColumnCount(); i++) args.data[i].Flatten(row_count);
    const idx_t cc = args.ColumnCount();
    auto in_temp = FlatVector::GetData<string_t>(args.data[0]);
    auto in_xsz  = FlatVector::GetData<double>(args.data[1]);
    auto in_ysz  = FlatVector::GetData<double>(args.data[2]);
    auto in_zsz  = FlatVector::GetData<double>(args.data[3]);
    const bool has_origin = cc > 4;
    const bool has_border = cc > 5;
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &out_validity = FlatVector::Validity(result);

    idx_t total = 0;
    for (idx_t row = 0; row < row_count; row++) {
        if (!FlatVector::Validity(args.data[0]).RowIsValid(row)) {
            out_validity.SetInvalid(row);
            list_entries[row] = list_entry_t{total, 0};
            continue;
        }
        Temporal *temp = BlobToTempTile(in_temp[row]);
        int32 srid = tspatial_srid(temp);
        GSERIALIZED *origin = nullptr;
        if (has_origin) {
            origin = GeometryToGSerialized(FlatVector::GetData<string_t>(args.data[4])[row], srid);
        }
        if (!origin) origin = DefaultOriginPoint();
        bool border = has_border ? FlatVector::GetData<bool>(args.data[5])[row] : true;
        int count = 0;
        STBox *boxes = tgeo_space_boxes(temp, in_xsz[row], in_ysz[row], in_zsz[row],
                                         origin, false, border, &count);
        free(temp); free(origin);
        EmitStboxList(result, row, list_entries, boxes, count, total);
    }
    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

void StboxFunctions::Tgeo_space_time_boxes(DataChunk &args, ExpressionState &state, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t i = 0; i < args.ColumnCount(); i++) args.data[i].Flatten(row_count);
    const idx_t cc = args.ColumnCount();
    auto in_temp = FlatVector::GetData<string_t>(args.data[0]);
    auto in_xsz  = FlatVector::GetData<double>(args.data[1]);
    auto in_ysz  = FlatVector::GetData<double>(args.data[2]);
    auto in_zsz  = FlatVector::GetData<double>(args.data[3]);
    auto in_dur  = FlatVector::GetData<interval_t>(args.data[4]);
    const bool has_origin  = cc > 5;
    const bool has_torigin = cc > 6;
    const bool has_border  = cc > 7;
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &out_validity = FlatVector::Validity(result);

    idx_t total = 0;
    for (idx_t row = 0; row < row_count; row++) {
        if (!FlatVector::Validity(args.data[0]).RowIsValid(row)) {
            out_validity.SetInvalid(row);
            list_entries[row] = list_entry_t{total, 0};
            continue;
        }
        Temporal *temp = BlobToTempTile(in_temp[row]);
        int32 srid = tspatial_srid(temp);
        MeosInterval mi = IntervaltToInterval(in_dur[row]);
        GSERIALIZED *origin = nullptr;
        if (has_origin) {
            origin = GeometryToGSerialized(FlatVector::GetData<string_t>(args.data[5])[row], srid);
        }
        if (!origin) origin = DefaultOriginPoint();
        TimestampTz torigin = 0;
        if (has_torigin) {
            timestamp_tz_t t = FlatVector::GetData<timestamp_tz_t>(args.data[6])[row];
            torigin = (TimestampTz) DuckDBToMeosTimestamp(t).value;
        }
        bool border = has_border ? FlatVector::GetData<bool>(args.data[7])[row] : true;
        int count = 0;
        STBox *boxes = tgeo_space_time_boxes(temp, in_xsz[row], in_ysz[row], in_zsz[row],
                                              &mi, origin, torigin, false, border, &count);
        free(temp); free(origin);
        EmitStboxList(result, row, list_entries, boxes, count, total);
    }
    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

void StboxFunctions::Stbox_get_space_tile(DataChunk &args, ExpressionState &state, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t i = 0; i < args.ColumnCount(); i++) args.data[i].Flatten(row_count);
    const idx_t cc = args.ColumnCount();
    auto in_pt  = FlatVector::GetData<string_t>(args.data[0]);
    auto in_xsz = FlatVector::GetData<double>(args.data[1]);
    auto in_ysz = FlatVector::GetData<double>(args.data[2]);
    auto in_zsz = FlatVector::GetData<double>(args.data[3]);
    const bool has_origin = cc > 4;
    auto out_data = FlatVector::GetData<string_t>(result);
    auto &out_validity = FlatVector::Validity(result);

    for (idx_t row = 0; row < row_count; row++) {
        if (!FlatVector::Validity(args.data[0]).RowIsValid(row)) {
            out_validity.SetInvalid(row);
            continue;
        }
        GSERIALIZED *pt = GeometryToGSerialized(in_pt[row], 0);
        if (!pt) {
            throw InvalidInputException("getSpaceTile: invalid point geometry");
        }
        GSERIALIZED *origin = nullptr;
        if (has_origin) {
            origin = GeometryToGSerialized(FlatVector::GetData<string_t>(args.data[4])[row], 0);
        }
        if (!origin) origin = DefaultOriginPoint();
        STBox *box = stbox_get_space_tile(pt, in_xsz[row], in_ysz[row], in_zsz[row], origin);
        free(pt); free(origin);
        if (!box) {
            out_validity.SetInvalid(row);
            continue;
        }
        out_data[row] = StboxToResultBlob(result, box);
        free(box);
    }
    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

void StboxFunctions::Stbox_get_time_tile(DataChunk &args, ExpressionState &state, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t i = 0; i < args.ColumnCount(); i++) args.data[i].Flatten(row_count);
    const idx_t cc = args.ColumnCount();
    auto in_t   = FlatVector::GetData<timestamp_tz_t>(args.data[0]);
    auto in_dur = FlatVector::GetData<interval_t>(args.data[1]);
    const bool has_torigin = cc > 2;
    auto out_data = FlatVector::GetData<string_t>(result);
    auto &out_validity = FlatVector::Validity(result);

    for (idx_t row = 0; row < row_count; row++) {
        if (!FlatVector::Validity(args.data[0]).RowIsValid(row)) {
            out_validity.SetInvalid(row);
            continue;
        }
        TimestampTz t = (TimestampTz) DuckDBToMeosTimestamp(in_t[row]).value;
        MeosInterval mi = IntervaltToInterval(in_dur[row]);
        TimestampTz torigin = 0;
        if (has_torigin) {
            timestamp_tz_t to = FlatVector::GetData<timestamp_tz_t>(args.data[2])[row];
            torigin = (TimestampTz) DuckDBToMeosTimestamp(to).value;
        }
        STBox *box = stbox_get_time_tile(t, &mi, torigin);
        if (!box) {
            out_validity.SetInvalid(row);
            continue;
        }
        out_data[row] = StboxToResultBlob(result, box);
        free(box);
    }
    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

void StboxFunctions::Stbox_get_space_time_tile(DataChunk &args, ExpressionState &state, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t i = 0; i < args.ColumnCount(); i++) args.data[i].Flatten(row_count);
    const idx_t cc = args.ColumnCount();
    auto in_pt  = FlatVector::GetData<string_t>(args.data[0]);
    auto in_t   = FlatVector::GetData<timestamp_tz_t>(args.data[1]);
    auto in_xsz = FlatVector::GetData<double>(args.data[2]);
    auto in_ysz = FlatVector::GetData<double>(args.data[3]);
    auto in_zsz = FlatVector::GetData<double>(args.data[4]);
    auto in_dur = FlatVector::GetData<interval_t>(args.data[5]);
    const bool has_origin  = cc > 6;
    const bool has_torigin = cc > 7;
    auto out_data = FlatVector::GetData<string_t>(result);
    auto &out_validity = FlatVector::Validity(result);

    for (idx_t row = 0; row < row_count; row++) {
        if (!FlatVector::Validity(args.data[0]).RowIsValid(row)) {
            out_validity.SetInvalid(row);
            continue;
        }
        GSERIALIZED *pt = GeometryToGSerialized(in_pt[row], 0);
        if (!pt) {
            throw InvalidInputException("getSpaceTimeTile: invalid point geometry");
        }
        TimestampTz t = (TimestampTz) DuckDBToMeosTimestamp(in_t[row]).value;
        MeosInterval mi = IntervaltToInterval(in_dur[row]);
        GSERIALIZED *origin = nullptr;
        if (has_origin) {
            origin = GeometryToGSerialized(FlatVector::GetData<string_t>(args.data[6])[row], 0);
        }
        if (!origin) origin = DefaultOriginPoint();
        TimestampTz torigin = 0;
        if (has_torigin) {
            timestamp_tz_t to = FlatVector::GetData<timestamp_tz_t>(args.data[7])[row];
            torigin = (TimestampTz) DuckDBToMeosTimestamp(to).value;
        }
        STBox *box = stbox_get_space_time_tile(pt, t, in_xsz[row], in_ysz[row], in_zsz[row],
                                                &mi, origin, torigin);
        free(pt); free(origin);
        if (!box) {
            out_validity.SetInvalid(row);
            continue;
        }
        out_data[row] = StboxToResultBlob(result, box);
        free(box);
    }
    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

#define DEFINE_TSPATIAL_TOPO(OP, MEOS)                                                                                                       \
void StboxFunctions::OP##_tspatial_stbox(DataChunk &args, ExpressionState &state, Vector &result) {                                          \
    BinaryExecutor::Execute<string_t, string_t, bool>(                                                                                       \
        args.data[0], args.data[1], result, args.size(),                                                                                     \
        [](string_t bt, string_t bs) -> bool {                                                                                               \
            Temporal *t = BlobToTemp(bt);                                                                                                    \
            STBox    *s = BlobToStbox(bs);                                                                                                   \
            bool r = MEOS##_tspatial_stbox(t, s);                                                                                            \
            free(t); free(s);                                                                                                                \
            return r;                                                                                                                        \
        });                                                                                                                                   \
}                                                                                                                                             \
void StboxFunctions::OP##_stbox_tspatial(DataChunk &args, ExpressionState &state, Vector &result) {                                          \
    BinaryExecutor::Execute<string_t, string_t, bool>(                                                                                       \
        args.data[0], args.data[1], result, args.size(),                                                                                     \
        [](string_t bs, string_t bt) -> bool {                                                                                               \
            STBox    *s = BlobToStbox(bs);                                                                                                   \
            Temporal *t = BlobToTemp(bt);                                                                                                    \
            bool r = MEOS##_stbox_tspatial(s, t);                                                                                            \
            free(s); free(t);                                                                                                                \
            return r;                                                                                                                        \
        });                                                                                                                                   \
}                                                                                                                                             \
void StboxFunctions::OP##_tspatial_tspatial(DataChunk &args, ExpressionState &state, Vector &result) {                                       \
    BinaryExecutor::Execute<string_t, string_t, bool>(                                                                                       \
        args.data[0], args.data[1], result, args.size(),                                                                                     \
        [](string_t b1, string_t b2) -> bool {                                                                                               \
            Temporal *t1 = BlobToTemp(b1);                                                                                                   \
            Temporal *t2 = BlobToTemp(b2);                                                                                                   \
            bool r = MEOS##_tspatial_tspatial(t1, t2);                                                                                       \
            free(t1); free(t2);                                                                                                              \
            return r;                                                                                                                        \
        });                                                                                                                                   \
}

DEFINE_TSPATIAL_TOPO(Contains,  contains)
DEFINE_TSPATIAL_TOPO(Contained, contained)
DEFINE_TSPATIAL_TOPO(Overlaps,  overlaps)
DEFINE_TSPATIAL_TOPO(Same,      same)
DEFINE_TSPATIAL_TOPO(Adjacent,  adjacent)

#undef DEFINE_TSPATIAL_TOPO

} // namespace duckdb
