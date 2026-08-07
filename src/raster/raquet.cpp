/* MobilityDuck binding for the MEOS raster family (`meos_raster.h`): the
 * `raquet` Web-Mercator raster tile plus the operations that sample a tile —
 * or a set of tiles — along a `tgeompoint` trajectory.
 *
 * RAQUET is stored as a BLOB holding the MEOS WKB serialisation of the tile,
 * so a tile survives a Parquet round trip and matches the hex-encoded text
 * form produced by the VARCHAR cast.  TGEOMPOINT trajectories and the TFLOAT
 * results are Temporal* blobs, as everywhere else in the binding.
 */

#include "raster/raquet.hpp"
#include "geo/stbox.hpp"
#include "geo/tgeompoint.hpp"
#include "quadbin/tquadbin.hpp"
#include "temporal/temporal.hpp"
#include "tydef.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_internal.h>
    #include <meos_raster.h>
}

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace duckdb {

LogicalType RaquetTypes::raquet() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("raquet");
    return type;
}

void RaquetTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType("raquet", raquet());
}

namespace {

/* The Raquet structure is opaque to callers, so a tile travels as its WKB
 * serialisation rather than as raw bytes of the structure. */
inline Raquet *BlobToRaquet(string_t blob) {
    Raquet *rq = raquet_from_wkb(
        reinterpret_cast<const uint8_t *>(blob.GetData()), blob.GetSize());
    if (!rq) {
        throw InvalidInputException("raquet: invalid tile serialisation");
    }
    return rq;
}

inline string_t RaquetToBlob(Vector &result, Raquet *rq) {
    size_t sz = 0;
    uint8_t *wkb = raquet_as_wkb(rq, 0, &sz);
    free(rq);
    if (!wkb) {
        throw InvalidInputException("raquet: cannot serialise tile");
    }
    string_t out = StringVector::AddStringOrBlob(
        result, string_t(reinterpret_cast<const char *>(wkb), sz));
    free(wkb);
    return out;
}

inline Temporal *BlobToTemp(string_t blob) {
    size_t sz = blob.GetSize();
    uint8_t *copy = (uint8_t *) malloc(sz);
    memcpy(copy, blob.GetData(), sz);
    return reinterpret_cast<Temporal *>(copy);
}

inline string_t TempToBlob(Vector &result, Temporal *t) {
    size_t sz = temporal_mem_size(t);
    string_t out = StringVector::AddStringOrBlob(
        result, string_t(reinterpret_cast<const char *>(t), sz));
    free(t);
    return out;
}

/* Pixel type names, as in the MobilityDB raster SQL surface. */
MeosPixType NameToPixType(string_t name) {
    std::string s(name.GetData(), name.GetSize());
    if (s == "UINT8")   return MEOS_PT_UINT8;
    if (s == "INT16")   return MEOS_PT_INT16;
    if (s == "INT32")   return MEOS_PT_INT32;
    if (s == "FLOAT32") return MEOS_PT_FLOAT32;
    if (s == "FLOAT64") return MEOS_PT_FLOAT64;
    throw InvalidInputException(
        "unknown pixel type \"%s\": use UINT8, INT16, INT32, FLOAT32 or FLOAT64",
        s.c_str());
}

size_t PixTypeSize(MeosPixType pixtype) {
    switch (pixtype) {
        case MEOS_PT_UINT8:   return 1;
        case MEOS_PT_INT16:   return 2;
        case MEOS_PT_INT32:   return 4;
        case MEOS_PT_FLOAT32: return 4;
        case MEOS_PT_FLOAT64: return 8;
        default:              return 0;
    }
}

/* MEOS reads the pixel band as `width * height` packed values and cannot know
 * how long the buffer handed to it is, so the dimensions and the buffer length
 * are checked here before the band is passed on. */
void ValidatePixels(string_t pixels, int32_t width, int32_t height,
                    MeosPixType pixtype) {
    if (width <= 0 || height <= 0) {
        throw InvalidInputException(
            "The width and height of a raquet tile must be positive");
    }
    size_t need = (size_t) width * (size_t) height * PixTypeSize(pixtype);
    if (pixels.GetSize() < need) {
        throw InvalidInputException(
            "The pixel array has %llu bytes but %llu are required for a %d x %d tile",
            (unsigned long long) pixels.GetSize(), (unsigned long long) need,
            width, height);
    }
}

} // namespace

/* =====================================================================
 * In / out
 *
 * The text form of a tile is its hex-encoded WKB, the representation
 * MobilityDB's raquet_in / raquet_out use.
 * ===================================================================== */

bool RaquetFunctions::Raquet_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t s) -> string_t {
            std::string str(s.GetData(), s.GetSize());
            Raquet *rq = raquet_in(str.c_str());
            if (!rq) throw InvalidInputException("raquet_in: invalid tile text");
            return RaquetToBlob(result, rq);
        });
    return true;
}

bool RaquetFunctions::Raquet_out_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            Raquet *rq = BlobToRaquet(blob);
            char *str = raquet_out(rq);
            free(rq);
            if (!str) throw InvalidInputException("raquet_out: invalid tile");
            std::string copy(str);
            free(str);
            return StringVector::AddString(result, copy);
        });
    return true;
}

/* =====================================================================
 * Constructors
 * ===================================================================== */

/* raquet(pixels, width, height, cell, pixtype[, nodata]): the six-argument
 * form enables nodata filtering with the given sentinel, the five-argument
 * form leaves the tile without one. */
void RaquetFunctions::Raquet_constructor(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    const idx_t row_count = args.size();
    const bool has_nodata_arg = args.ColumnCount() > 5;
    for (idx_t c = 0; c < args.ColumnCount(); c++) args.data[c].Flatten(row_count);

    auto pixels  = FlatVector::GetData<string_t>(args.data[0]);
    auto width   = FlatVector::GetData<int32_t>(args.data[1]);
    auto height  = FlatVector::GetData<int32_t>(args.data[2]);
    auto cell    = FlatVector::GetData<int64_t>(args.data[3]);
    auto pixtype = FlatVector::GetData<string_t>(args.data[4]);
    auto out     = FlatVector::GetData<string_t>(result);
    auto &out_validity = FlatVector::Validity(result);

    for (idx_t row = 0; row < row_count; row++) {
        bool any_null = false;
        for (idx_t c = 0; c < args.ColumnCount(); c++) {
            if (c == 5) continue;   /* nodata NULL means "no nodata value" */
            if (!FlatVector::Validity(args.data[c]).RowIsValid(row)) any_null = true;
        }
        if (any_null) { out_validity.SetInvalid(row); continue; }

        bool has_nodata = has_nodata_arg &&
            FlatVector::Validity(args.data[5]).RowIsValid(row);
        double nodata = has_nodata
            ? FlatVector::GetData<double>(args.data[5])[row] : 0.0;

        MeosPixType pt = NameToPixType(pixtype[row]);
        ValidatePixels(pixels[row], width[row], height[row], pt);

        Raquet *rq = raquet_make(
            static_cast<uint64_t>(cell[row]),
            static_cast<uint16_t>(width[row]), static_cast<uint16_t>(height[row]),
            pt, nodata, has_nodata,
            reinterpret_cast<const uint8_t *>(pixels[row].GetData()));
        if (!rq) { out_validity.SetInvalid(row); continue; }
        out[row] = RaquetToBlob(result, rq);
    }
    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

/* raquetRead(rasterfile[, cell]): decode a raster file held in memory through
 * GDAL.  A missing or NULL cell derives the tile identifier from the raster
 * geotransform, which requires an EPSG:3857 tile-aligned raster. */
void RaquetFunctions::Raquet_read(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    const idx_t row_count = args.size();
    const bool has_cell_arg = args.ColumnCount() > 1;
    for (idx_t c = 0; c < args.ColumnCount(); c++) args.data[c].Flatten(row_count);

    auto file = FlatVector::GetData<string_t>(args.data[0]);
    auto out  = FlatVector::GetData<string_t>(result);
    auto &out_validity = FlatVector::Validity(result);

    for (idx_t row = 0; row < row_count; row++) {
        if (!FlatVector::Validity(args.data[0]).RowIsValid(row)) {
            out_validity.SetInvalid(row);
            continue;
        }
        uint64_t cell = 0;
        if (has_cell_arg && FlatVector::Validity(args.data[1]).RowIsValid(row)) {
            cell = static_cast<uint64_t>(FlatVector::GetData<int64_t>(args.data[1])[row]);
        }
        Raquet *rq = raquet_read_bytes(
            reinterpret_cast<const uint8_t *>(file[row].GetData()),
            file[row].GetSize(), cell);
        if (!rq) { out_validity.SetInvalid(row); continue; }
        out[row] = RaquetToBlob(result, rq);
    }
    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

/* =====================================================================
 * Accessors
 * ===================================================================== */

void RaquetFunctions::Raquet_quadbin(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    UnaryExecutor::Execute<string_t, int64_t>(
        args.data[0], result, args.size(),
        [&](string_t blob) -> int64_t {
            Raquet *rq = BlobToRaquet(blob);
            uint64_t cell = raquet_quadbin(rq);
            free(rq);
            return static_cast<int64_t>(cell);
        });
}

void RaquetFunctions::Raquet_width(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    UnaryExecutor::Execute<string_t, int32_t>(
        args.data[0], result, args.size(),
        [&](string_t blob) -> int32_t {
            Raquet *rq = BlobToRaquet(blob);
            int w = raquet_width(rq);
            free(rq);
            return w;
        });
}

void RaquetFunctions::Raquet_height(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    UnaryExecutor::Execute<string_t, int32_t>(
        args.data[0], result, args.size(),
        [&](string_t blob) -> int32_t {
            Raquet *rq = BlobToRaquet(blob);
            int h = raquet_height(rq);
            free(rq);
            return h;
        });
}

void RaquetFunctions::Raquet_nodata(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    UnaryExecutor::Execute<string_t, double>(
        args.data[0], result, args.size(),
        [&](string_t blob) -> double {
            Raquet *rq = BlobToRaquet(blob);
            double nd = raquet_nodata(rq);
            free(rq);
            return nd;
        });
}

void RaquetFunctions::Raquet_pixtype(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t blob) -> string_t {
            Raquet *rq = BlobToRaquet(blob);
            char *str = raquet_pixtype(rq);
            free(rq);
            std::string copy(str ? str : "");
            if (str) free(str);
            return StringVector::AddString(result, copy);
        });
}

void RaquetFunctions::Raquet_to_stbox(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
            Raquet *rq = BlobToRaquet(blob);
            STBox *box = raquet_to_stbox(rq);
            free(rq);
            if (!box) { mask.SetInvalid(idx); return string_t(); }
            string_t out = StringVector::AddStringOrBlob(
                result, string_t(reinterpret_cast<const char *>(box), sizeof(STBox)));
            free(box);
            return out;
        });
}

/* =====================================================================
 * Comparisons
 * ===================================================================== */

#define RAQUET_COMPARISON(FnName, meos_fn)                                     \
    void RaquetFunctions::FnName(                                              \
        DataChunk &args, ExpressionState &state, Vector &result)               \
    {                                                                          \
        BinaryExecutor::Execute<string_t, string_t, bool>(                     \
            args.data[0], args.data[1], result, args.size(),                   \
            [&](string_t l, string_t r) -> bool {                              \
                Raquet *rq1 = BlobToRaquet(l);                                 \
                Raquet *rq2 = BlobToRaquet(r);                                 \
                bool res = meos_fn(rq1, rq2);                                  \
                free(rq1); free(rq2);                                          \
                return res;                                                    \
            });                                                                \
    }

RAQUET_COMPARISON(Raquet_eq, raquet_eq)
RAQUET_COMPARISON(Raquet_ne, raquet_ne)
RAQUET_COMPARISON(Raquet_lt, raquet_lt)
RAQUET_COMPARISON(Raquet_le, raquet_le)
RAQUET_COMPARISON(Raquet_ge, raquet_ge)
RAQUET_COMPARISON(Raquet_gt, raquet_gt)

#undef RAQUET_COMPARISON

void RaquetFunctions::Raquet_cmp(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    BinaryExecutor::Execute<string_t, string_t, int32_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t l, string_t r) -> int32_t {
            Raquet *rq1 = BlobToRaquet(l);
            Raquet *rq2 = BlobToRaquet(r);
            int res = raquet_cmp(rq1, rq2);
            free(rq1); free(rq2);
            return res;
        });
}

void RaquetFunctions::Raquet_hash(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    UnaryExecutor::Execute<string_t, int32_t>(
        args.data[0], result, args.size(),
        [&](string_t blob) -> int32_t {
            Raquet *rq = BlobToRaquet(blob);
            uint32_t h = raquet_hash(rq);
            free(rq);
            return static_cast<int32_t>(h);
        });
}

void RaquetFunctions::Raquet_hash_extended(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    BinaryExecutor::Execute<string_t, int64_t, int64_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t blob, int64_t seed) -> int64_t {
            Raquet *rq = BlobToRaquet(blob);
            uint64_t h = raquet_hash_extended(rq, static_cast<uint64_t>(seed));
            free(rq);
            return static_cast<int64_t>(h);
        });
}

/* =====================================================================
 * Sampling along a trajectory
 * ===================================================================== */

void RaquetFunctions::Raster_tile_value(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t tile, string_t traj, ValidityMask &mask, idx_t idx) -> string_t {
            Raquet *rq = BlobToRaquet(tile);
            Temporal *t = BlobToTemp(traj);
            Temporal *res = raster_tile_value(rq, t);
            free(rq); free(t);
            if (!res) { mask.SetInvalid(idx); return string_t(); }
            return TempToBlob(result, res);
        });
}

/* rasterTileValue(LIST(raquet), tgeompoint): where tiles overlap, the value of
 * the tile of highest zoom wins. */
void RaquetFunctions::Raster_tile_value_array(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    const idx_t row_count = args.size();
    auto &tiles = args.data[0];
    auto &trajs = args.data[1];
    tiles.Flatten(row_count);
    trajs.Flatten(row_count);
    auto list_entries = FlatVector::GetData<list_entry_t>(tiles);
    auto &child = ListVector::GetEntry(tiles);
    child.Flatten(ListVector::GetListSize(tiles));
    auto child_data = FlatVector::GetData<string_t>(child);
    auto &child_validity = FlatVector::Validity(child);
    auto traj_data = FlatVector::GetData<string_t>(trajs);
    auto out = FlatVector::GetData<string_t>(result);
    auto &out_validity = FlatVector::Validity(result);

    for (idx_t row = 0; row < row_count; row++) {
        if (!FlatVector::Validity(tiles).RowIsValid(row) ||
            !FlatVector::Validity(trajs).RowIsValid(row)) {
            out_validity.SetInvalid(row);
            continue;
        }
        list_entry_t entry = list_entries[row];
        std::vector<Raquet *> arr;
        arr.reserve(entry.length);
        for (idx_t k = 0; k < entry.length; k++) {
            idx_t pos = entry.offset + k;
            if (!child_validity.RowIsValid(pos)) continue;
            arr.push_back(BlobToRaquet(child_data[pos]));
        }
        if (arr.empty()) { out_validity.SetInvalid(row); continue; }

        Temporal *t = BlobToTemp(traj_data[row]);
        Temporal *res = raster_tile_value_array(
            const_cast<const Raquet **>(arr.data()), (int) arr.size(), t);
        free(t);
        for (Raquet *rq : arr) free(rq);
        if (!res) { out_validity.SetInvalid(row); continue; }
        out[row] = TempToBlob(result, res);
    }
    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

/* rasterTileValueQuadbin: sample a bare pixel band, georeferenced by the cell,
 * without building a raquet tile first — the form a Raquet Parquet row feeds
 * directly. */
void RaquetFunctions::Raster_tile_value_quadbin(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    const idx_t row_count = args.size();
    for (idx_t c = 0; c < args.ColumnCount(); c++) args.data[c].Flatten(row_count);

    auto pixels     = FlatVector::GetData<string_t>(args.data[0]);
    auto width      = FlatVector::GetData<int32_t>(args.data[1]);
    auto height     = FlatVector::GetData<int32_t>(args.data[2]);
    auto cell       = FlatVector::GetData<int64_t>(args.data[3]);
    auto pixtype    = FlatVector::GetData<string_t>(args.data[4]);
    auto nodata     = FlatVector::GetData<double>(args.data[5]);
    auto has_nodata = FlatVector::GetData<bool>(args.data[6]);
    auto traj       = FlatVector::GetData<string_t>(args.data[7]);
    auto out        = FlatVector::GetData<string_t>(result);
    auto &out_validity = FlatVector::Validity(result);

    for (idx_t row = 0; row < row_count; row++) {
        bool any_null = false;
        for (idx_t c = 0; c < args.ColumnCount(); c++) {
            if (!FlatVector::Validity(args.data[c]).RowIsValid(row)) any_null = true;
        }
        if (any_null) { out_validity.SetInvalid(row); continue; }

        MeosPixType pt = NameToPixType(pixtype[row]);
        ValidatePixels(pixels[row], width[row], height[row], pt);

        Temporal *t = BlobToTemp(traj[row]);
        Temporal *res = raster_tile_value_quadbin(
            reinterpret_cast<const uint8_t *>(pixels[row].GetData()),
            static_cast<uint16_t>(width[row]), static_cast<uint16_t>(height[row]),
            static_cast<uint64_t>(cell[row]), pt,
            nodata[row], has_nodata[row], t);
        free(t);
        if (!res) { out_validity.SetInvalid(row); continue; }
        out[row] = TempToBlob(result, res);
    }
    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

/* trajectoryQuadbins(traj, zoom): the distinct cells at a zoom level the
 * trajectory covers, a join key against a Raquet table. */
void RaquetFunctions::Trajectory_quadbins(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    const idx_t row_count = args.size();
    auto &trajs = args.data[0];
    auto &zooms = args.data[1];
    trajs.Flatten(row_count);
    zooms.Flatten(row_count);
    auto traj_data = FlatVector::GetData<string_t>(trajs);
    auto zoom_data = FlatVector::GetData<int32_t>(zooms);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &out_validity = FlatVector::Validity(result);

    idx_t off = 0;
    for (idx_t row = 0; row < row_count; row++) {
        if (!FlatVector::Validity(trajs).RowIsValid(row) ||
            !FlatVector::Validity(zooms).RowIsValid(row)) {
            out_validity.SetInvalid(row);
            list_entries[row] = list_entry_t{off, 0};
            continue;
        }
        Temporal *t = BlobToTemp(traj_data[row]);
        int count = 0;
        uint64_t *cells = trajectory_quadbins(
            t, static_cast<uint32_t>(zoom_data[row]), &count);
        free(t);

        idx_t n = (cells && count > 0) ? (idx_t) count : 0;
        ListVector::Reserve(result, off + n);
        ListVector::SetListSize(result, off + n);
        list_entries[row] = list_entry_t{off, (uint64_t) n};
        if (n > 0) {
            auto cell_data = FlatVector::GetData<int64_t>(ListVector::GetEntry(result));
            for (idx_t k = 0; k < n; k++) {
                cell_data[off + k] = static_cast<int64_t>(cells[k]);
            }
            off += n;
        }
        if (cells) free(cells);
    }
    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

/* =====================================================================
 * Registration
 * ===================================================================== */

void RaquetTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, raquet(),
        RaquetFunctions::Raquet_in_cast);
    RegisterMeosCastFunction(loader, raquet(), LogicalType::VARCHAR,
        RaquetFunctions::Raquet_out_cast);
}

void RaquetTypes::RegisterScalarFunctions(ExtensionLoader &loader) {
    const auto RQ  = raquet();
    const auto QB  = QuadbinTypes::quadbin();
    const auto TG  = TgeompointType::tgeompoint();
    const auto TF  = TemporalTypes::tfloat();
    const auto BX  = StboxType::stbox();
    const auto BLB = LogicalType::BLOB;
    const auto I32 = LogicalType::INTEGER;
    const auto I64 = LogicalType::BIGINT;
    const auto D   = LogicalType::DOUBLE;
    const auto B   = LogicalType::BOOLEAN;
    const auto V   = LogicalType::VARCHAR;

    /* Constructors */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "raquet", {BLB, I32, I32, QB, V}, RQ,
        RaquetFunctions::Raquet_constructor));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "raquet", {BLB, I32, I32, QB, V, D}, RQ,
        RaquetFunctions::Raquet_constructor));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "raquetRead", {BLB}, RQ, RaquetFunctions::Raquet_read));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "raquetRead", {BLB, QB}, RQ, RaquetFunctions::Raquet_read));

    /* Accessors */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "quadbin", {RQ}, QB, RaquetFunctions::Raquet_quadbin));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "width", {RQ}, I32, RaquetFunctions::Raquet_width));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "height", {RQ}, I32, RaquetFunctions::Raquet_height));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "nodata", {RQ}, D, RaquetFunctions::Raquet_nodata));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "pixtype", {RQ}, V, RaquetFunctions::Raquet_pixtype));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "stbox", {RQ}, BX, RaquetFunctions::Raquet_to_stbox));

    /* Comparisons */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "eq", {RQ, RQ}, B, RaquetFunctions::Raquet_eq));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "ne", {RQ, RQ}, B, RaquetFunctions::Raquet_ne));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "lt", {RQ, RQ}, B, RaquetFunctions::Raquet_lt));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "le", {RQ, RQ}, B, RaquetFunctions::Raquet_le));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "ge", {RQ, RQ}, B, RaquetFunctions::Raquet_ge));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "gt", {RQ, RQ}, B, RaquetFunctions::Raquet_gt));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "cmp", {RQ, RQ}, I32, RaquetFunctions::Raquet_cmp));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "hash", {RQ}, I32, RaquetFunctions::Raquet_hash));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "hashExtended", {RQ, I64}, I64, RaquetFunctions::Raquet_hash_extended));

    /* Sampling */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "rasterTileValue", {RQ, TG}, TF, RaquetFunctions::Raster_tile_value));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "rasterTileValue", {LogicalType::LIST(RQ), TG}, TF,
        RaquetFunctions::Raster_tile_value_array));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "rasterTileValueQuadbin", {BLB, I32, I32, QB, V, D, B, TG}, TF,
        RaquetFunctions::Raster_tile_value_quadbin));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "trajectoryQuadbins", {TG, I32}, LogicalType::LIST(QB),
        RaquetFunctions::Trajectory_quadbins));
}

} // namespace duckdb
