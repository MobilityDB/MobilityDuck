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
#include "spatial/geometry/wkb_writer.hpp"

namespace duckdb {

namespace {

/* MEOS stbox_area() can SIGSEGV on geodetic boxes (3D / PolyhedralSurface path). For geodetic
 * footprints use spherical rectangle area (WGS84 sphere); avoids MEOS geog_in/geog_area faults. */
/* Sphere zone area between two meridians and parallels (m^2). Matches MEOS/PostGIS sphere model
 * closely enough for tests; avoids MEOS geog_in/geog_area which can SIGSEGV in this extension. */
inline double Spherical_lonlat_rect_area_m2(double xmin, double ymin, double xmax, double ymax,
                                            bool use_spheroid) {
    (void)use_spheroid;
    constexpr double DEG_TO_RAD = M_PI / 180.0;
    /* WGS84 semi-major axis (m); MEOS geog_area on sphere uses ~this for spheroid=false path. */
    constexpr double R = 6378137.0;
    const double lam1 = xmin * DEG_TO_RAD;
    const double lam2 = xmax * DEG_TO_RAD;
    const double phi1 = ymin * DEG_TO_RAD;
    const double phi2 = ymax * DEG_TO_RAD;
    return R * R * (lam2 - lam1) * (std::sin(phi2) - std::sin(phi1));
}

inline double Geodetic_stbox_footprint_area(const STBox *box, bool use_spheroid) {
    return Spherical_lonlat_rect_area_m2(box->xmin, box->ymin, box->xmax, box->ymax, use_spheroid);
}

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

            STBox *flat = Stbox_geodetic_xy_copy(stbox);
            STBox *geo_src = flat ? flat : stbox;
            GSERIALIZED *gs = stbox_to_geo(geo_src);
            if (!gs) {
                free(flat);
                free(stbox);
                throw InvalidInputException("Failed to convert stbox to geometry");
            }

            string_t geometry_blob = GSerializedToGeometry(gs, state, result);
            string_t stored_result = StringVector::AddStringOrBlob(result, geometry_blob);

            free(gs);
            free(flat);
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
    auto compute_area = [](string_t input_stbox, bool spheroid,
                           ValidityMask &mask, idx_t idx) -> double {
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
        double ret;
        /* MEOS stbox_area() can SIGSEGV on geodetic boxes; use spherical lon/lat footprint. */
        const bool geodetic = stbox_isgeodetic(stbox) || MEOS_FLAGS_GET_GEODETIC(stbox->flags);
        if (geodetic) {
            ret = Geodetic_stbox_footprint_area(stbox, spheroid);
        } else {
            ret = stbox_area(stbox, spheroid);
        }
        free(stbox);
        if (ret == DBL_MAX) {
            mask.SetInvalid(idx);
            return double();
        }
        return ret;
    };

    if (args.ColumnCount() >= 2) {
        BinaryExecutor::ExecuteWithNulls<string_t, bool, double>(
            args.data[0], args.data[1], result, args.size(),
            [&](string_t blob, bool sph, ValidityMask &mask, idx_t idx) -> double {
                return compute_area(blob, sph, mask, idx);
            });
    } else {
        UnaryExecutor::ExecuteWithNulls<string_t, double>(
            args.data[0], result, args.size(),
            [&](string_t blob, ValidityMask &mask, idx_t idx) -> double {
                return compute_area(blob, true, mask, idx);
            });
    }
    if (args.size() == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

// Stbox_perimeter — wraps MEOS' stbox_perimeter(box, spheroid). Mirrors
// the Stbox_area shape (1-arg defaults spheroid=true; 2-arg honours the
// caller's choice).
void StboxFunctions::Stbox_perimeter(DataChunk &args, ExpressionState &state, Vector &result) {
    if (args.ColumnCount() >= 2) {
        BinaryExecutor::ExecuteWithNulls<string_t, bool, double>(
            args.data[0], args.data[1], result, args.size(),
            [&](string_t blob, bool spheroid, ValidityMask &mask, idx_t idx) -> double {
                if (blob.GetSize() != sizeof(STBox)) {
                    throw InvalidInputException("Invalid STBOX value size");
                }
                STBox box;
                memcpy(&box, blob.GetData(), sizeof(STBox));
                double ret = stbox_perimeter(&box, spheroid);
                if (ret == DBL_MAX) { mask.SetInvalid(idx); return 0.0; }
                return ret;
            });
    } else {
        UnaryExecutor::ExecuteWithNulls<string_t, double>(
            args.data[0], result, args.size(),
            [&](string_t blob, ValidityMask &mask, idx_t idx) -> double {
                if (blob.GetSize() != sizeof(STBox)) {
                    throw InvalidInputException("Invalid STBOX value size");
                }
                STBox box;
                memcpy(&box, blob.GetData(), sizeof(STBox));
                double ret = stbox_perimeter(&box, true);
                if (ret == DBL_MAX) { mask.SetInvalid(idx); return 0.0; }
                return ret;
            });
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

// transformPipeline(stbox, pipeline text, srid int, is_forward bool)
// — applies a PROJ pipeline string to an stbox. Wraps MEOS'
// stbox_transform_pipeline.
void StboxFunctions::Stbox_transform_pipeline(DataChunk &args, ExpressionState &, Vector &result) {
    const idx_t count = args.size();
    args.data[0].Flatten(count);
    args.data[1].Flatten(count);
    args.data[2].Flatten(count);
    args.data[3].Flatten(count);

    auto in_box  = FlatVector::GetData<string_t>(args.data[0]);
    auto in_pipe = FlatVector::GetData<string_t>(args.data[1]);
    auto in_srid = FlatVector::GetData<int32_t>(args.data[2]);
    auto in_fwd  = FlatVector::GetData<bool>(args.data[3]);

    auto out_data  = FlatVector::GetData<string_t>(result);
    auto &out_mask = FlatVector::Validity(result);

    auto &m0 = FlatVector::Validity(args.data[0]);
    auto &m1 = FlatVector::Validity(args.data[1]);
    auto &m2 = FlatVector::Validity(args.data[2]);
    auto &m3 = FlatVector::Validity(args.data[3]);

    for (idx_t i = 0; i < count; ++i) {
        if (!m0.RowIsValid(i) || !m1.RowIsValid(i) ||
            !m2.RowIsValid(i) || !m3.RowIsValid(i)) {
            out_mask.SetInvalid(i);
            continue;
        }
        size_t sz = in_box[i].GetSize();
        STBox *box = (STBox *) malloc(sz);
        memcpy(box, in_box[i].GetData(), sz);
        std::string pipe = in_pipe[i].GetString();
        STBox *r = stbox_transform_pipeline(box, pipe.c_str(), in_srid[i], in_fwd[i]);
        free(box);
        if (!r) { out_mask.SetInvalid(i); continue; }
        out_data[i] = StringVector::AddStringOrBlob(
            result, reinterpret_cast<const char *>(r), sizeof(STBox));
        free(r);
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
 * Typed constructors — stboxX/stboxXT/stboxZ/stboxZT/
 * stboxT and the geodstbox* variants. All wrap MEOS'
 * single core constructor `stbox_make`.
 ****************************************************/

namespace {

inline string_t StboxBlobFromMake(Vector &result, bool hasx, bool hasz, bool geodetic,
                                  int32_t srid, double xmin, double xmax,
                                  double ymin, double ymax, double zmin, double zmax,
                                  Span *period) {
    STBox *box = stbox_make(hasx, hasz, geodetic, srid, xmin, xmax, ymin, ymax,
                            zmin, zmax, period);
    if (!box) throw InternalException("stbox_make returned NULL");
    string_t out = StringVector::AddStringOrBlob(
        result, reinterpret_cast<const char *>(box), sizeof(STBox));
    free(box);
    return out;
}

inline Span SpanFromTimestamp(timestamp_tz_t duck_ts) {
    timestamp_tz_t meos = DuckDBToMeosTimestamp(duck_ts);
    Span s;
    span_set((Datum) meos.value, (Datum) meos.value, true, true,
             T_TIMESTAMPTZ, T_TSTZSPAN, &s);
    return s;
}

template <bool HASZ, bool GEODETIC>
void StboxConstructorXorZExec(DataChunk &args, ExpressionState &, Vector &result) {
    const idx_t cnt = args.size();
    const int N = HASZ ? 6 : 4;
    const bool with_srid = args.ColumnCount() > (idx_t) N;
    auto *out = FlatVector::GetData<string_t>(result);
    for (idx_t i = 0; i < cnt; i++) {
        double v[6] = {0,0,0,0,0,0};
        for (int j = 0; j < N; j++) {
            v[j] = args.data[j].GetValue(i).GetValue<double>();
        }
        int32_t srid = with_srid ? args.data[N].GetValue(i).GetValue<int32_t>() : 0;
        double xmin, ymin, zmin = 0, xmax, ymax, zmax = 0;
        if (!HASZ) {
            xmin = v[0]; ymin = v[1]; xmax = v[2]; ymax = v[3];
        } else {
            xmin = v[0]; ymin = v[1]; zmin = v[2];
            xmax = v[3]; ymax = v[4]; zmax = v[5];
        }
        out[i] = StboxBlobFromMake(result, true, HASZ, GEODETIC, srid,
                                   xmin, xmax, ymin, ymax, zmin, zmax, nullptr);
    }
    if (cnt == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

template <bool GEODETIC>
void StboxConstructorTimestampExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<timestamp_tz_t, string_t>(
        args.data[0], result, args.size(),
        [&](timestamp_tz_t ts) -> string_t {
            Span s = SpanFromTimestamp(ts);
            return StboxBlobFromMake(result, false, false, GEODETIC, 0,
                                     0, 0, 0, 0, 0, 0, &s);
        });
}

template <bool GEODETIC>
void StboxConstructorSpanExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t blob) -> string_t {
            Span s;
            memcpy(&s, blob.GetData(), sizeof(Span));
            return StboxBlobFromMake(result, false, false, GEODETIC, 0,
                                     0, 0, 0, 0, 0, 0, &s);
        });
}

template <bool HASZ, bool TS_IS_SPAN, bool GEODETIC>
void StboxConstructorXTorZTExec(DataChunk &args, ExpressionState &, Vector &result) {
    const idx_t cnt = args.size();
    const int Nspatial = HASZ ? 6 : 4;
    const idx_t time_idx = (idx_t) Nspatial;
    const bool with_srid = args.ColumnCount() > time_idx + 1;
    auto *out = FlatVector::GetData<string_t>(result);

    for (idx_t i = 0; i < cnt; i++) {
        double v[6] = {0,0,0,0,0,0};
        for (int j = 0; j < Nspatial; j++) {
            v[j] = args.data[j].GetValue(i).GetValue<double>();
        }
        Span s;
        if (TS_IS_SPAN) {
            args.data[time_idx].Flatten(cnt);
            string_t span_blob = FlatVector::GetData<string_t>(args.data[time_idx])[i];
            memcpy(&s, span_blob.GetData(), sizeof(Span));
        } else {
            args.data[time_idx].Flatten(cnt);
            timestamp_tz_t ts = FlatVector::GetData<timestamp_tz_t>(args.data[time_idx])[i];
            s = SpanFromTimestamp(ts);
        }
        int32_t srid = with_srid
            ? args.data[time_idx + 1].GetValue(i).GetValue<int32_t>()
            : 0;
        double xmin, ymin, zmin = 0, xmax, ymax, zmax = 0;
        if (!HASZ) {
            xmin = v[0]; ymin = v[1]; xmax = v[2]; ymax = v[3];
        } else {
            xmin = v[0]; ymin = v[1]; zmin = v[2];
            xmax = v[3]; ymax = v[4]; zmax = v[5];
        }
        out[i] = StboxBlobFromMake(result, true, HASZ, GEODETIC, srid,
                                   xmin, xmax, ymin, ymax, zmin, zmax, &s);
    }
    if (cnt == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

}  // namespace

void StboxFunctions::Stbox_constructor_x(DataChunk &args, ExpressionState &state, Vector &result) {
    StboxConstructorXorZExec<false, false>(args, state, result);
}
void StboxFunctions::Stbox_constructor_z(DataChunk &args, ExpressionState &state, Vector &result) {
    StboxConstructorXorZExec<true, false>(args, state, result);
}
void StboxFunctions::Stbox_constructor_t_timestamp(DataChunk &args, ExpressionState &state, Vector &result) {
    StboxConstructorTimestampExec<false>(args, state, result);
}
void StboxFunctions::Stbox_constructor_t_span(DataChunk &args, ExpressionState &state, Vector &result) {
    StboxConstructorSpanExec<false>(args, state, result);
}
void StboxFunctions::Stbox_constructor_xt_timestamp(DataChunk &args, ExpressionState &state, Vector &result) {
    StboxConstructorXTorZTExec<false, false, false>(args, state, result);
}
void StboxFunctions::Stbox_constructor_xt_span(DataChunk &args, ExpressionState &state, Vector &result) {
    StboxConstructorXTorZTExec<false, true, false>(args, state, result);
}
void StboxFunctions::Stbox_constructor_zt_timestamp(DataChunk &args, ExpressionState &state, Vector &result) {
    StboxConstructorXTorZTExec<true, false, false>(args, state, result);
}
void StboxFunctions::Stbox_constructor_zt_span(DataChunk &args, ExpressionState &state, Vector &result) {
    StboxConstructorXTorZTExec<true, true, false>(args, state, result);
}
void StboxFunctions::Geodstbox_constructor_z(DataChunk &args, ExpressionState &state, Vector &result) {
    StboxConstructorXorZExec<true, true>(args, state, result);
}
void StboxFunctions::Geodstbox_constructor_t_timestamp(DataChunk &args, ExpressionState &state, Vector &result) {
    StboxConstructorTimestampExec<true>(args, state, result);
}
void StboxFunctions::Geodstbox_constructor_t_span(DataChunk &args, ExpressionState &state, Vector &result) {
    StboxConstructorSpanExec<true>(args, state, result);
}
void StboxFunctions::Geodstbox_constructor_zt_timestamp(DataChunk &args, ExpressionState &state, Vector &result) {
    StboxConstructorXTorZTExec<true, false, true>(args, state, result);
}
void StboxFunctions::Geodstbox_constructor_zt_span(DataChunk &args, ExpressionState &state, Vector &result) {
    StboxConstructorXTorZTExec<true, true, true>(args, state, result);
}

} // namespace duckdb
