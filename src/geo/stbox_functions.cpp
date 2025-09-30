#include "meos_wrapper_simple.hpp"
#include "common.hpp"

#include "geo/stbox_functions.hpp"
#include "time_util.hpp"

#include "duckdb/common/exception.hpp"

namespace duckdb {

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
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
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
                throw InternalException("Failure in Stbox_from_wkb: unable to cast wkb to stbox");
                return string_t();
            }
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
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

void StboxFunctions::Stbox_from_hexwkb(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_hexwkb) -> string_t {
            char *hexwkb = (char*)input_hexwkb.GetData();
            STBox *stbox = stbox_from_hexwkb(hexwkb);
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
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
        [&](string_t wkb_blob, timestamp_tz_t ts_duckdb, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *wkb_data = reinterpret_cast<const uint8_t*>(wkb_blob.GetData());
            size_t wkb_size = wkb_blob.GetSize();
            int32 srid = 0;
            GSERIALIZED *gs = geo_from_ewkb(wkb_data, wkb_size, srid);
            if (!gs) {
                throw InvalidInputException("Invalid geometry format: " + wkb_blob.GetString());
                return string_t();
            }
            timestamp_tz_t ts_meos = DuckDBToMeosTimestamp(ts_duckdb);
            STBox *ret = geo_timestamptz_to_stbox(gs, (TimestampTz)ts_meos.value);
            if (!ret) {
                free(ret);
                free(gs);
                mask.SetInvalid(idx);
                return string_t();
            }
            size_t stbox_size = sizeof(STBox);
            uint8_t *stbox_data = (uint8_t*)malloc(stbox_size);
            if (!stbox_data) {
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
        [&](string_t wkb_blob, string_t span_blob, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *wkb_data = reinterpret_cast<const uint8_t*>(wkb_blob.GetData());
            size_t wkb_size = wkb_blob.GetSize();
            int32 srid = 0;
            GSERIALIZED *gs = geo_from_ewkb(wkb_data, wkb_size, srid);
            if (!gs) {
                throw InvalidInputException("Invalid geometry format: " + wkb_blob.GetString());
                return string_t();
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
                throw InternalException("Failure in Geo_tstzspan_to_stbox: unable to allocate memory for stbox");
                return string_t();
            }
            memcpy(stbox_data, ret, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
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
        [&](string_t wkb_blob, ValidityMask &mask, idx_t idx) -> string_t {
            const uint8_t *wkb_data = reinterpret_cast<const uint8_t*>(wkb_blob.GetData());
            size_t wkb_size = wkb_blob.GetSize();
            int32 srid = 0;
            GSERIALIZED *gs = geo_from_ewkb(wkb_data, wkb_size, srid);
            if (!gs) {
                throw InvalidInputException("Invalid geometry format: " + wkb_blob.GetString());
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
                throw InternalException("Failure in Stbox_expand_space: unable to allocate memory for stbox");
            }
            memcpy(stbox_data, ret, stbox_size);
            string_t ret_str(reinterpret_cast<const char*>(stbox_data), stbox_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, ret_str);
            free(stbox_data);
            free(ret);
            return stored_data;
        }
    );
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

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
            memcpy(data_copy1, data1, data_size1);
            STBox *stbox1 = reinterpret_cast<STBox*>(data_copy1);
            if (!stbox1) {
                free(data_copy1);
                throw InternalException("Failure in Overlaps_stbox_stbox: unable to cast binary to stbox");
            }

            const uint8_t *data2 = reinterpret_cast<const uint8_t*>(input_stbox2.GetData());
            size_t data_size2 = input_stbox2.GetSize();
            if (data_size2 < sizeof(void*)) {
                throw InvalidInputException("Invalid STBOX data: insufficient size");
            }
            uint8_t *data_copy2 = (uint8_t*)malloc(data_size2);
            memcpy(data_copy2, data2, data_size2);
            STBox *stbox2 = reinterpret_cast<STBox*>(data_copy2);
            if (!stbox2) {
                free(data_copy2);
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

} // namespace duckdb