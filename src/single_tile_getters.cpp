// Single-tile getter functions: given an input point in the (value, time,
// space) domain and a tile grid configuration, return the single tile that
// contains the input. Mirrors MobilityDB's `getValueTile` / `getTBoxTimeTile`
// / `getValueTimeTile` (tbox-shaped) and `getSpaceTile` / `getStboxTimeTile`
// / `getSpaceTimeTile` (stbox-shaped).

#include "meos_wrapper_simple.hpp"

#include "common.hpp"
#include "single_tile_getters.hpp"
#include "temporal/tbox.hpp"
#include "geo/stbox.hpp"
#include "geo_util.hpp"
#include "time_util.hpp"
#include "spatial/spatial_types.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

namespace {

// MobilityDB default torigin for time tiles: '2000-01-03' (a Monday).
// In MEOS PG-epoch microseconds: 2 days * 86_400 * 1_000_000.
constexpr int64_t DEFAULT_TIME_ORIGIN_MEOS = 2LL * 86400LL * 1000000LL;

inline string_t MallocBlobToResult(Vector &result, void *buf, size_t sz) {
    string_t blob(reinterpret_cast<const char *>(buf), UnsafeNumericCast<uint32_t>(sz));
    string_t stored = StringVector::AddStringOrBlob(result, blob);
    free(buf);
    return stored;
}

inline string_t TboxToBlob(Vector &result, TBox *tbox) {
    string_t out = StringVector::AddStringOrBlob(
        result, reinterpret_cast<const char *>(tbox), sizeof(TBox));
    free(tbox);
    return out;
}

inline string_t StboxToBlob(Vector &result, STBox *stbox) {
    string_t out = StringVector::AddStringOrBlob(
        result, reinterpret_cast<const char *>(stbox), sizeof(STBox));
    free(stbox);
    return out;
}

// =====================================================================
// getValueTile(double, double [, double=0.0]) -> tbox
// =====================================================================

void GetValueTileExec(DataChunk &args, ExpressionState &, Vector &result) {
    const idx_t count = args.size();
    args.data[0].Flatten(count);
    args.data[1].Flatten(count);
    Vector *vorigin_vec = args.ColumnCount() >= 3 ? &args.data[2] : nullptr;
    if (vorigin_vec) vorigin_vec->Flatten(count);

    auto v_data = FlatVector::GetData<double>(args.data[0]);
    auto vsize_data = FlatVector::GetData<double>(args.data[1]);
    auto &result_validity = FlatVector::Validity(result);

    for (idx_t i = 0; i < count; ++i) {
        if (FlatVector::IsNull(args.data[0], i) || FlatVector::IsNull(args.data[1], i) ||
            (vorigin_vec && FlatVector::IsNull(*vorigin_vec, i))) {
            result_validity.SetInvalid(i);
            continue;
        }
        double v = v_data[i];
        double vsize = vsize_data[i];
        double vorigin = vorigin_vec ? FlatVector::GetData<double>(*vorigin_vec)[i] : 0.0;

        TBox *tbox = tbox_get_value_time_tile(
            Float8GetDatum(v), 0,
            Float8GetDatum(vsize), nullptr,
            Float8GetDatum(vorigin), 0,
            T_FLOAT8, T_FLOATSPAN);
        if (!tbox) {
            result_validity.SetInvalid(i);
            continue;
        }
        FlatVector::GetData<string_t>(result)[i] = StringVector::AddStringOrBlob(
            result, reinterpret_cast<const char *>(tbox), sizeof(TBox));
        free(tbox);
    }
}

// =====================================================================
// getTBoxTimeTile(timestamptz, interval [, timestamptz='2000-01-03']) -> tbox
// =====================================================================

void GetTBoxTimeTileExec(DataChunk &args, ExpressionState &, Vector &result) {
    const idx_t count = args.size();
    args.data[0].Flatten(count);
    args.data[1].Flatten(count);
    Vector *torigin_vec = args.ColumnCount() >= 3 ? &args.data[2] : nullptr;
    if (torigin_vec) torigin_vec->Flatten(count);

    auto t_data = FlatVector::GetData<timestamp_tz_t>(args.data[0]);
    auto dur_data = FlatVector::GetData<interval_t>(args.data[1]);
    auto &result_validity = FlatVector::Validity(result);

    for (idx_t i = 0; i < count; ++i) {
        if (FlatVector::IsNull(args.data[0], i) || FlatVector::IsNull(args.data[1], i) ||
            (torigin_vec && FlatVector::IsNull(*torigin_vec, i))) {
            result_validity.SetInvalid(i);
            continue;
        }
        timestamp_tz_t meos_t = DuckDBToMeosTimestamp(t_data[i]);
        MeosInterval iv = IntervaltToInterval(dur_data[i]);
        TimestampTz torigin = (TimestampTz) DEFAULT_TIME_ORIGIN_MEOS;
        if (torigin_vec) {
            timestamp_tz_t in = FlatVector::GetData<timestamp_tz_t>(*torigin_vec)[i];
            torigin = (TimestampTz) DuckDBToMeosTimestamp(in).value;
        }

        TBox *tbox = tbox_get_value_time_tile(
            (Datum) 0, (TimestampTz) meos_t.value,
            (Datum) 0, &iv,
            (Datum) 0, torigin,
            T_TIMESTAMPTZ, T_TSTZSPAN);
        if (!tbox) {
            result_validity.SetInvalid(i);
            continue;
        }
        FlatVector::GetData<string_t>(result)[i] = StringVector::AddStringOrBlob(
            result, reinterpret_cast<const char *>(tbox), sizeof(TBox));
        free(tbox);
    }
}

// =====================================================================
// getValueTimeTile(double, timestamptz, double, interval
//                  [, double=0.0, timestamptz='2000-01-03']) -> tbox
// =====================================================================

void GetValueTimeTileExec(DataChunk &args, ExpressionState &, Vector &result) {
    const idx_t count = args.size();
    for (idx_t c = 0; c < args.ColumnCount(); ++c) {
        args.data[c].Flatten(count);
    }
    Vector *vorigin_vec = args.ColumnCount() >= 5 ? &args.data[4] : nullptr;
    Vector *torigin_vec = args.ColumnCount() >= 6 ? &args.data[5] : nullptr;

    auto v_data = FlatVector::GetData<double>(args.data[0]);
    auto t_data = FlatVector::GetData<timestamp_tz_t>(args.data[1]);
    auto vsize_data = FlatVector::GetData<double>(args.data[2]);
    auto dur_data = FlatVector::GetData<interval_t>(args.data[3]);
    auto &result_validity = FlatVector::Validity(result);

    for (idx_t i = 0; i < count; ++i) {
        if (FlatVector::IsNull(args.data[0], i) || FlatVector::IsNull(args.data[1], i) ||
            FlatVector::IsNull(args.data[2], i) || FlatVector::IsNull(args.data[3], i) ||
            (vorigin_vec && FlatVector::IsNull(*vorigin_vec, i)) ||
            (torigin_vec && FlatVector::IsNull(*torigin_vec, i))) {
            result_validity.SetInvalid(i);
            continue;
        }
        double v = v_data[i];
        timestamp_tz_t meos_t = DuckDBToMeosTimestamp(t_data[i]);
        double vsize = vsize_data[i];
        MeosInterval iv = IntervaltToInterval(dur_data[i]);
        double vorigin = vorigin_vec ? FlatVector::GetData<double>(*vorigin_vec)[i] : 0.0;
        TimestampTz torigin = (TimestampTz) DEFAULT_TIME_ORIGIN_MEOS;
        if (torigin_vec) {
            timestamp_tz_t in = FlatVector::GetData<timestamp_tz_t>(*torigin_vec)[i];
            torigin = (TimestampTz) DuckDBToMeosTimestamp(in).value;
        }

        TBox *tbox = tbox_get_value_time_tile(
            Float8GetDatum(v), (TimestampTz) meos_t.value,
            Float8GetDatum(vsize), &iv,
            Float8GetDatum(vorigin), torigin,
            T_FLOAT8, T_FLOATSPAN);
        if (!tbox) {
            result_validity.SetInvalid(i);
            continue;
        }
        FlatVector::GetData<string_t>(result)[i] = StringVector::AddStringOrBlob(
            result, reinterpret_cast<const char *>(tbox), sizeof(TBox));
        free(tbox);
    }
}

// =====================================================================
// getSpaceTile / getSpaceTimeTile / getStboxTimeTile
// =====================================================================

// Default sorigin = Point(0 0 0). Built once and reused — geometric origin
// for all space-tile lookups when the user does not pass an explicit one.
static GSERIALIZED *DefaultSpatialOrigin() {
    static GSERIALIZED *cached = nullptr;
    if (!cached) {
        cached = geompoint_make3dz(0, 0.0, 0.0, 0.0);
    }
    return cached;
}

// Geometry blob to GSERIALIZED, requiring SRID info (defaults to 0).
inline GSERIALIZED *DecodeGeometry(string_t geom_blob) {
    return GeometryToGSerialized(geom_blob, 0);
}

// getSpaceTile(geom, xsize, ysize, zsize [, sorigin geom='Point(0 0 0)']) -> stbox
void GetSpaceTileExec(DataChunk &args, ExpressionState &, Vector &result) {
    const idx_t count = args.size();
    for (idx_t c = 0; c < args.ColumnCount(); ++c) args.data[c].Flatten(count);
    Vector *sorigin_vec = args.ColumnCount() >= 5 ? &args.data[4] : nullptr;

    auto geom_data = FlatVector::GetData<string_t>(args.data[0]);
    auto xsize_data = FlatVector::GetData<double>(args.data[1]);
    auto ysize_data = FlatVector::GetData<double>(args.data[2]);
    auto zsize_data = FlatVector::GetData<double>(args.data[3]);
    auto &result_validity = FlatVector::Validity(result);

    for (idx_t i = 0; i < count; ++i) {
        if (FlatVector::IsNull(args.data[0], i) || FlatVector::IsNull(args.data[1], i) ||
            FlatVector::IsNull(args.data[2], i) || FlatVector::IsNull(args.data[3], i) ||
            (sorigin_vec && FlatVector::IsNull(*sorigin_vec, i))) {
            result_validity.SetInvalid(i);
            continue;
        }
        GSERIALIZED *gs = DecodeGeometry(geom_data[i]);
        GSERIALIZED *origin = DefaultSpatialOrigin();
        bool free_origin = false;
        if (sorigin_vec) {
            origin = DecodeGeometry(FlatVector::GetData<string_t>(*sorigin_vec)[i]);
            free_origin = true;
        }
        STBox *stbox = stbox_get_space_tile(
            gs, xsize_data[i], ysize_data[i], zsize_data[i], origin);
        free(gs);
        if (free_origin) free(origin);
        if (!stbox) {
            result_validity.SetInvalid(i);
            continue;
        }
        FlatVector::GetData<string_t>(result)[i] = StringVector::AddStringOrBlob(
            result, reinterpret_cast<const char *>(stbox), sizeof(STBox));
        free(stbox);
    }
}

// getStboxTimeTile(timestamptz, interval [, timestamptz='2000-01-03']) -> stbox
void GetStboxTimeTileExec(DataChunk &args, ExpressionState &, Vector &result) {
    const idx_t count = args.size();
    args.data[0].Flatten(count);
    args.data[1].Flatten(count);
    Vector *torigin_vec = args.ColumnCount() >= 3 ? &args.data[2] : nullptr;
    if (torigin_vec) torigin_vec->Flatten(count);

    auto t_data = FlatVector::GetData<timestamp_tz_t>(args.data[0]);
    auto dur_data = FlatVector::GetData<interval_t>(args.data[1]);
    auto &result_validity = FlatVector::Validity(result);

    for (idx_t i = 0; i < count; ++i) {
        if (FlatVector::IsNull(args.data[0], i) || FlatVector::IsNull(args.data[1], i) ||
            (torigin_vec && FlatVector::IsNull(*torigin_vec, i))) {
            result_validity.SetInvalid(i);
            continue;
        }
        timestamp_tz_t meos_t = DuckDBToMeosTimestamp(t_data[i]);
        MeosInterval iv = IntervaltToInterval(dur_data[i]);
        TimestampTz torigin = (TimestampTz) DEFAULT_TIME_ORIGIN_MEOS;
        if (torigin_vec) {
            timestamp_tz_t in = FlatVector::GetData<timestamp_tz_t>(*torigin_vec)[i];
            torigin = (TimestampTz) DuckDBToMeosTimestamp(in).value;
        }
        STBox *stbox = stbox_get_time_tile((TimestampTz) meos_t.value, &iv, torigin);
        if (!stbox) {
            result_validity.SetInvalid(i);
            continue;
        }
        FlatVector::GetData<string_t>(result)[i] = StringVector::AddStringOrBlob(
            result, reinterpret_cast<const char *>(stbox), sizeof(STBox));
        free(stbox);
    }
}

// getSpaceTimeTile(geom, t, xsize, ysize, zsize, interval
//                  [, sorigin='Point(0 0 0)', torigin='2000-01-03']) -> stbox
void GetSpaceTimeTileExec(DataChunk &args, ExpressionState &, Vector &result) {
    const idx_t count = args.size();
    for (idx_t c = 0; c < args.ColumnCount(); ++c) args.data[c].Flatten(count);
    Vector *sorigin_vec = args.ColumnCount() >= 7 ? &args.data[6] : nullptr;
    Vector *torigin_vec = args.ColumnCount() >= 8 ? &args.data[7] : nullptr;

    auto geom_data = FlatVector::GetData<string_t>(args.data[0]);
    auto t_data = FlatVector::GetData<timestamp_tz_t>(args.data[1]);
    auto xsize_data = FlatVector::GetData<double>(args.data[2]);
    auto ysize_data = FlatVector::GetData<double>(args.data[3]);
    auto zsize_data = FlatVector::GetData<double>(args.data[4]);
    auto dur_data = FlatVector::GetData<interval_t>(args.data[5]);
    auto &result_validity = FlatVector::Validity(result);

    for (idx_t i = 0; i < count; ++i) {
        if (FlatVector::IsNull(args.data[0], i) || FlatVector::IsNull(args.data[1], i) ||
            FlatVector::IsNull(args.data[2], i) || FlatVector::IsNull(args.data[3], i) ||
            FlatVector::IsNull(args.data[4], i) || FlatVector::IsNull(args.data[5], i) ||
            (sorigin_vec && FlatVector::IsNull(*sorigin_vec, i)) ||
            (torigin_vec && FlatVector::IsNull(*torigin_vec, i))) {
            result_validity.SetInvalid(i);
            continue;
        }
        GSERIALIZED *gs = DecodeGeometry(geom_data[i]);
        GSERIALIZED *origin = DefaultSpatialOrigin();
        bool free_origin = false;
        if (sorigin_vec) {
            origin = DecodeGeometry(FlatVector::GetData<string_t>(*sorigin_vec)[i]);
            free_origin = true;
        }
        timestamp_tz_t meos_t = DuckDBToMeosTimestamp(t_data[i]);
        MeosInterval iv = IntervaltToInterval(dur_data[i]);
        TimestampTz torigin = (TimestampTz) DEFAULT_TIME_ORIGIN_MEOS;
        if (torigin_vec) {
            timestamp_tz_t in = FlatVector::GetData<timestamp_tz_t>(*torigin_vec)[i];
            torigin = (TimestampTz) DuckDBToMeosTimestamp(in).value;
        }

        STBox *stbox = stbox_get_space_time_tile(
            gs, (TimestampTz) meos_t.value,
            xsize_data[i], ysize_data[i], zsize_data[i],
            &iv, origin, torigin);
        free(gs);
        if (free_origin) free(origin);
        if (!stbox) {
            result_validity.SetInvalid(i);
            continue;
        }
        FlatVector::GetData<string_t>(result)[i] = StringVector::AddStringOrBlob(
            result, reinterpret_cast<const char *>(stbox), sizeof(STBox));
        free(stbox);
    }
}

// Convenience overload: getSpaceTile(geom, xsize) — uniform xyz, default origin.
void GetSpaceTileUniformExec(DataChunk &args, ExpressionState &state, Vector &result) {
    // Forward to the full-arity exec by synthesising a 4-column DataChunk.
    DataChunk forwarded;
    vector<LogicalType> types = {args.data[0].GetType(), LogicalType::DOUBLE,
                                 LogicalType::DOUBLE, LogicalType::DOUBLE};
    forwarded.Initialize(Allocator::DefaultAllocator(), types);
    forwarded.SetCardinality(args.size());
    forwarded.data[0].Reference(args.data[0]);
    forwarded.data[1].Reference(args.data[1]);
    forwarded.data[2].Reference(args.data[1]);
    forwarded.data[3].Reference(args.data[1]);
    GetSpaceTileExec(forwarded, state, result);
}

} // namespace

void SingleTileGetters::RegisterScalarFunctions(ExtensionLoader &loader) {
    LogicalType geometry = GeoTypes::GEOMETRY();

    // ---- tbox getters ----
    loader.RegisterFunction(ScalarFunction(
        "getValueTile",
        {LogicalType::DOUBLE, LogicalType::DOUBLE},
        TboxType::TBOX(), GetValueTileExec));
    loader.RegisterFunction(ScalarFunction(
        "getValueTile",
        {LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::DOUBLE},
        TboxType::TBOX(), GetValueTileExec));

    loader.RegisterFunction(ScalarFunction(
        "getTBoxTimeTile",
        {LogicalType::TIMESTAMP_TZ, LogicalType::INTERVAL},
        TboxType::TBOX(), GetTBoxTimeTileExec));
    loader.RegisterFunction(ScalarFunction(
        "getTBoxTimeTile",
        {LogicalType::TIMESTAMP_TZ, LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ},
        TboxType::TBOX(), GetTBoxTimeTileExec));

    loader.RegisterFunction(ScalarFunction(
        "getValueTimeTile",
        {LogicalType::DOUBLE, LogicalType::TIMESTAMP_TZ,
         LogicalType::DOUBLE, LogicalType::INTERVAL},
        TboxType::TBOX(), GetValueTimeTileExec));
    loader.RegisterFunction(ScalarFunction(
        "getValueTimeTile",
        {LogicalType::DOUBLE, LogicalType::TIMESTAMP_TZ,
         LogicalType::DOUBLE, LogicalType::INTERVAL,
         LogicalType::DOUBLE, LogicalType::TIMESTAMP_TZ},
        TboxType::TBOX(), GetValueTimeTileExec));

    // ---- stbox getters ----
    // 4-arg: full xyz sizes, default sorigin
    loader.RegisterFunction(ScalarFunction(
        "getSpaceTile",
        {geometry, LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::DOUBLE},
        StboxType::STBOX(), GetSpaceTileExec));
    // 5-arg: full xyz sizes + explicit sorigin
    loader.RegisterFunction(ScalarFunction(
        "getSpaceTile",
        {geometry, LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::DOUBLE, geometry},
        StboxType::STBOX(), GetSpaceTileExec));
    // 2-arg: uniform xyz, default sorigin
    loader.RegisterFunction(ScalarFunction(
        "getSpaceTile",
        {geometry, LogicalType::DOUBLE},
        StboxType::STBOX(), GetSpaceTileUniformExec));

    loader.RegisterFunction(ScalarFunction(
        "getStboxTimeTile",
        {LogicalType::TIMESTAMP_TZ, LogicalType::INTERVAL},
        StboxType::STBOX(), GetStboxTimeTileExec));
    loader.RegisterFunction(ScalarFunction(
        "getStboxTimeTile",
        {LogicalType::TIMESTAMP_TZ, LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ},
        StboxType::STBOX(), GetStboxTimeTileExec));

    loader.RegisterFunction(ScalarFunction(
        "getSpaceTimeTile",
        {geometry, LogicalType::TIMESTAMP_TZ,
         LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::DOUBLE,
         LogicalType::INTERVAL},
        StboxType::STBOX(), GetSpaceTimeTileExec));
    loader.RegisterFunction(ScalarFunction(
        "getSpaceTimeTile",
        {geometry, LogicalType::TIMESTAMP_TZ,
         LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::DOUBLE,
         LogicalType::INTERVAL, geometry},
        StboxType::STBOX(), GetSpaceTimeTileExec));
    loader.RegisterFunction(ScalarFunction(
        "getSpaceTimeTile",
        {geometry, LogicalType::TIMESTAMP_TZ,
         LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::DOUBLE,
         LogicalType::INTERVAL, geometry, LogicalType::TIMESTAMP_TZ},
        StboxType::STBOX(), GetSpaceTimeTileExec));
}

} // namespace duckdb
