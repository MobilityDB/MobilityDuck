#include "meos_wrapper_simple.hpp"
#include "common.hpp"

#include "geo/stbox_functions.hpp"
#include "time_util.hpp"
#include "geo_util.hpp"
#include <cfloat>

#include "duckdb/common/exception.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/common/typedefs.hpp"

#include <cmath>
#include <string>

#include "spatial/spatial_types.hpp"

namespace duckdb {

namespace {

/* For stbox_to_geo: 2D geodetic box via MEOS constructor (no Z dimension in output geometry). */
inline STBox *Stbox_geodetic_xy_copy(const STBox *box) {
    if (!stbox_isgeodetic(box) || !stbox_hasz(box)) {
        return nullptr;
    }
    return stbox_make(stbox_hasx(box), false, true, box->srid, box->xmin, box->xmax, box->ymin, box->ymax,
                      0.0, 0.0, stbox_hast(box) ? &box->period : nullptr);
}

inline void Stbox_normalize_geodetic_srid(STBox *box) {
    if ((stbox_isgeodetic(box) || MEOS_FLAGS_GET_GEODETIC(box->flags)) && box->srid == 0) {
        box->srid = 4326;
    }
}

} // namespace

/* ***************************************************
 * In/out functions: VARCHAR <-> stbox
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
 * In/out functions: WKB/HexWKB <-> stbox
 ****************************************************/

void StboxFunctions::Stbox_from_hexwkb(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_hexwkb) -> string_t {
            // string_t::GetData() is not NUL-terminated, but stbox_from_hexwkb()
            // strlen()s its argument; reading the raw pointer overruns the buffer
            // on allocators that leave the trailing byte non-zero (e.g. macOS arm64).
            // Copy into a NUL-terminated std::string first, as the sibling consumers do.
            std::string hexwkb = input_hexwkb.GetString();
            STBox *stbox = stbox_from_hexwkb(hexwkb.c_str());
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

void StboxFunctions::Stbox_as_hexwkb(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input_stbox) -> string_t {
            const uint8_t *data = reinterpret_cast<const uint8_t*>(input_stbox.GetData());
            size_t data_size = input_stbox.GetSize();
            if (data_size < sizeof(void*)) {
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid tstzspan data: insufficient size");
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
                throw InvalidInputException("Invalid tstzspan data: null pointer");
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
 * Conversion functions + cast functions: [TYPE] -> stbox
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

bool StboxFunctions::Geo_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Geo_to_stbox_common(source, result, count);
    return true;
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
            // A set carries its elements after the header, so its blob is at least the header
            // and longer for every element past the first: the guard is a minimum, not a match.
            if (data_size < sizeof(Set)) {
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
                throw InvalidInputException("Invalid tstzspan data: insufficient size");
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
            // Likewise a span set: sizeof covers the header and one span, never a longer one.
            if (data_size < sizeof(SpanSet)) {
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

bool StboxFunctions::Tstzspanset_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    Tstzspanset_to_stbox_common(source, result, count);
    return true;
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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

void StboxFunctions::Intersection_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t input_stbox1, string_t input_stbox2) -> string_t {
            const uint8_t *data1 = reinterpret_cast<const uint8_t*>(input_stbox1.GetData());
            size_t data_size1 = input_stbox1.GetSize();
            if (data_size1 < sizeof(void*)) {
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
                throw InvalidInputException("Invalid stbox data: insufficient size");
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
        TimestampTz torigin = DEFAULT_TIME_ORIGIN_MEOS;
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
        TimestampTz torigin = DEFAULT_TIME_ORIGIN_MEOS;
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
        TimestampTz torigin = DEFAULT_TIME_ORIGIN_MEOS;
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
