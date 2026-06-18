/* MobilityDuck binding for MEOS Raquet raster chip-sampling functions.
 *
 * rasterTileValueQuadbin — sample a Raquet band BLOB along a tgeompoint
 *   trajectory, returning the sampled values as TFLOAT.
 * trajectoryQuadbins — list the QUADBIN cells covered by a tgeompoint
 *   trajectory at a given zoom level, for Raquet table joins.
 *
 * DuckDB camelCase naming: raster_tile_value_quadbin → rasterTileValueQuadbin,
 * trajectory_quadbins → trajectoryQuadbins.
 */

#include "quadbin/raster_quadbin.hpp"

#include "quadbin/tquadbin.hpp"
#include "geo/tgeompoint.hpp"
#include "temporal/temporal.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/function/scalar_function.hpp"

extern "C" {
    #include <meos.h>       /* MeosPixType, raster_tile_value_quadbin,
                               trajectory_quadbins */
    #include <meos_internal.h> /* temporal_mem_size */
}

#include <cstdlib>
#include <cstring>

namespace duckdb {

namespace {

inline Temporal *BlobToTemp(string_t blob) {
    size_t sz = blob.GetSize();
    uint8_t *copy = static_cast<uint8_t *>(malloc(sz));
    memcpy(copy, blob.GetData(), sz);
    return reinterpret_cast<Temporal *>(copy);
}

inline string_t TempToBlob(Vector &result, Temporal *t) {
    if (!t) return string_t();
    size_t sz = temporal_mem_size(t);
    string_t out = StringVector::AddStringOrBlob(
        result, string_t(reinterpret_cast<const char *>(t), sz));
    free(t);
    return out;
}

/* Map a VARCHAR pixtype name to its MeosPixType enum value.
 * Sets *ok=false on an unknown name. */
static MeosPixType
str_to_pixtype(const char *s, size_t len, bool *ok) {
    *ok = true;
    if (len == 5 && memcmp(s, "UINT8",   5) == 0) return MEOS_PT_UINT8;
    if (len == 5 && memcmp(s, "INT16",   5) == 0) return MEOS_PT_INT16;
    if (len == 5 && memcmp(s, "INT32",   5) == 0) return MEOS_PT_INT32;
    if (len == 7 && memcmp(s, "FLOAT32", 7) == 0) return MEOS_PT_FLOAT32;
    if (len == 7 && memcmp(s, "FLOAT64", 7) == 0) return MEOS_PT_FLOAT64;
    *ok = false;
    return MEOS_PT_UINT8;
}

} // namespace

/* =========================================================================
 * rasterTileValueQuadbin
 * ========================================================================= */

void RasterQuadbinFunctions::Raster_tile_value_quadbin(
    DataChunk &args, ExpressionState &, Vector &result)
{
    /* args[0] pixels     BLOB           – raw row-major pixel bytes
     * args[1] width      INTEGER        – tile width in pixels
     * args[2] height     INTEGER        – tile height in pixels
     * args[3] cell       BIGINT(QUADBIN)– CARTO QUADBIN cell id
     * args[4] pixtype    VARCHAR        – "UINT8"|"INT16"|"INT32"|"FLOAT32"|"FLOAT64"
     * args[5] nodata     DOUBLE         – nodata sentinel value
     * args[6] has_nodata BOOLEAN        – enable nodata filtering
     * args[7] traj       BLOB(TGEOMPOINT) – trajectory, SRID 4326
     * Returns BLOB (TFLOAT) */
    const idx_t row_count = args.size();
    for (idx_t c = 0; c < args.ColumnCount(); ++c)
        args.data[c].Flatten(row_count);

    auto px_data      = FlatVector::GetData<string_t>(args.data[0]);
    auto width_data   = FlatVector::GetData<int32_t> (args.data[1]);
    auto height_data  = FlatVector::GetData<int32_t> (args.data[2]);
    auto cell_data    = FlatVector::GetData<int64_t> (args.data[3]);
    auto pixtype_data = FlatVector::GetData<string_t>(args.data[4]);
    auto nodata_data  = FlatVector::GetData<double>  (args.data[5]);
    auto hasnd_data   = FlatVector::GetData<bool>    (args.data[6]);
    auto traj_data    = FlatVector::GetData<string_t>(args.data[7]);

    auto &out_validity = FlatVector::Validity(result);
    auto  out_data     = FlatVector::GetData<string_t>(result);

    for (idx_t i = 0; i < row_count; ++i) {
        /* NULL propagation */
        bool any_null = false;
        for (idx_t c = 0; c < args.ColumnCount(); ++c) {
            if (!FlatVector::Validity(args.data[c]).RowIsValid(i)) {
                any_null = true;
                break;
            }
        }
        if (any_null) { out_validity.SetInvalid(i); continue; }

        string_t pt = pixtype_data[i];
        bool pixtype_ok;
        MeosPixType pixtype = str_to_pixtype(pt.GetData(), pt.GetSize(), &pixtype_ok);
        if (!pixtype_ok) { out_validity.SetInvalid(i); continue; }

        const uint8_t *pixels =
            reinterpret_cast<const uint8_t *>(px_data[i].GetData());
        Temporal *traj = BlobToTemp(traj_data[i]);

        Temporal *res = raster_tile_value_quadbin(
            pixels,
            static_cast<uint16_t>(width_data[i]),
            static_cast<uint16_t>(height_data[i]),
            static_cast<uint64_t>(cell_data[i]),
            pixtype,
            nodata_data[i],
            hasnd_data[i],
            traj);

        free(traj);

        if (!res) {
            out_validity.SetInvalid(i);
            continue;
        }
        out_data[i] = TempToBlob(result, res);
    }
}

/* =========================================================================
 * trajectoryQuadbins
 * ========================================================================= */

void RasterQuadbinFunctions::Trajectory_quadbins(
    DataChunk &args, ExpressionState &, Vector &result)
{
    /* args[0] traj  BLOB (TGEOMPOINT)
     * args[1] zoom  INTEGER  (0–26)
     * Returns LIST<BIGINT> (QUADBIN cell ids as signed BIGINT) */
    const idx_t row_count = args.size();
    for (idx_t c = 0; c < args.ColumnCount(); ++c)
        args.data[c].Flatten(row_count);

    auto traj_data = FlatVector::GetData<string_t>(args.data[0]);
    auto zoom_data = FlatVector::GetData<int32_t> (args.data[1]);

    auto &out_validity = FlatVector::Validity(result);
    auto  list_entries = FlatVector::GetData<list_entry_t>(result);
    idx_t total = 0;

    for (idx_t i = 0; i < row_count; ++i) {
        if (!FlatVector::Validity(args.data[0]).RowIsValid(i) ||
            !FlatVector::Validity(args.data[1]).RowIsValid(i)) {
            out_validity.SetInvalid(i);
            list_entries[i] = list_entry_t{total, 0};
            continue;
        }

        Temporal *traj = BlobToTemp(traj_data[i]);
        int ncells = 0;
        uint64_t *cells = trajectory_quadbins(
            traj, static_cast<uint32_t>(zoom_data[i]), &ncells);
        free(traj);

        if (!cells || ncells <= 0) {
            if (cells) free(cells);
            list_entries[i] = list_entry_t{total, 0};
            continue;
        }

        ListVector::Reserve(result, total + (idx_t) ncells);
        ListVector::SetListSize(result, total + (idx_t) ncells);
        list_entries[i] = list_entry_t{total, static_cast<uint64_t>(ncells)};

        auto child = FlatVector::GetData<int64_t>(ListVector::GetEntry(result));
        for (int j = 0; j < ncells; j++)
            child[total + j] = static_cast<int64_t>(cells[j]);
        total += (idx_t) ncells;
        free(cells);
    }
}

/* =========================================================================
 * Registration
 * ========================================================================= */

void RasterQuadbinFunctions::RegisterScalarFunctions(ExtensionLoader &loader)
{
    const auto QB   = QuadbinTypes::QUADBIN();
    const auto TGMP = TgeompointType::TGEOMPOINT();
    const auto TF   = TemporalTypes::TFLOAT();
    const auto I32  = LogicalType::INTEGER;
    const auto DBL  = LogicalType::DOUBLE;
    const auto BOOL = LogicalType::BOOLEAN;
    const auto V    = LogicalType::VARCHAR;

    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "rasterTileValueQuadbin",
        {LogicalType::BLOB, I32, I32, QB, V, DBL, BOOL, TGMP}, TF,
        RasterQuadbinFunctions::Raster_tile_value_quadbin));

    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "trajectoryQuadbins",
        {TGMP, I32}, LogicalType::LIST(QB),
        RasterQuadbinFunctions::Trajectory_quadbins));
}

} // namespace duckdb
