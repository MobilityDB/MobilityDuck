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

} // namespace duckdb
