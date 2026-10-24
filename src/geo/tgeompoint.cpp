#include "meos_wrapper_simple.hpp"
#include "duckdb_version_compat.hpp"

#include "common.hpp"
#include "geo/tgeompoint.hpp"
#include "geo/tgeompoint_functions.hpp"
#include "geo/geoset.hpp"
#include "temporal/temporal_functions.hpp"
#include "geo/stbox.hpp"
#include "temporal/spanset.hpp"
#include "temporal/temporal.hpp"
#include "temporal/set.hpp"
#include "temporal/span.hpp"

#include "duckdb/common/types/blob.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "spatial/spatial_types.hpp"
#include "mobilityduck/meos_exec_serial.hpp"
#include "geo_util.hpp"
#include "time_util.hpp"

namespace duckdb {

LogicalType TgeompointType::tgeompoint() {
    LogicalType type(LogicalTypeId::BLOB);
    type.SetAlias("tgeompoint");
    return type;
}

void TgeompointType::RegisterType(ExtensionLoader &loader) {
    loader.RegisterType( "tgeompoint", tgeompoint());
}

void TgeompointType::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, 
        LogicalType::VARCHAR,
        tgeompoint(),
        TgeompointFunctions::Tpoint_in
    );

    RegisterMeosCastFunction(loader, 
        tgeompoint(),
        LogicalType::VARCHAR,
        TemporalFunctions::Temporal_out
    );

    RegisterMeosCastFunction(loader, 
        tgeompoint(),
        StboxType::stbox(),
        TgeompointFunctions::Tspatial_to_stbox_cast
    );

    RegisterMeosCastFunction(loader, 
        tgeompoint(),
        SpanTypes::tstzspan(),
        TgeompointFunctions::Temporal_to_tstzspan_cast
    );
}

void TgeompointType::RegisterScalarFunctions(ExtensionLoader &loader) {

    /* ***************************************************
     * In/out functions
     ****************************************************/

    const auto varchar_list = LogicalType::LIST(LogicalType::VARCHAR);

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asText",
            {LogicalType::LIST(tgeompoint())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asText",
            {LogicalType::LIST(tgeompoint()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asText",
            {LogicalType::LIST(MobilityDuckGeometryType())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asText",
            {LogicalType::LIST(MobilityDuckGeometryType()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(tgeompoint())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(tgeompoint()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(MobilityDuckGeometryType())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader, 
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(MobilityDuckGeometryType()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );

    // stbox(tgeompoint), shiftScaleTime(tgeompoint, interval, interval), rotate and scale over
    // tgeompoint, and collect(geometry[]) and distance(geometry, geometry), the native geometry
    // operations, are generated from the catalog (meos_geo_conversion, meos_temporal_transf,
    // meos_geo_transf, meos_geo_base_spatial, meos_geo_base_dist).
}

/* ***************************************************
 * Round-trip I/O for tgeompoint: asEWKB / asHexWKB / asHexEWKB /
 * asMFJSON and the matching tgeompointFromText / FromBinary / FromEWKB /
 * FromHexWKB / FromHexEWKB / FromMFJSON constructors.
 *
 * spaceSplit / spaceTimeSplit — set-returning splitters that bucket a
 * tgeompoint trajectory into spatial (and optionally temporal) bins and
 * return one row per bin, mirroring MobilityDB's
 *
 *   RETURNS TABLE(spaceBin geometry, [timeBin timestamptz,] tpoint tgeompoint)
 *
 * asMVTGeom for tgeompoint
 *
 *   asMVTGeom(tgeompoint, stbox bounds[, extent int[, buffer int[, clip bool]]])
 *     RETURNS STRUCT(geom geometry, times bigint[])
 ****************************************************/

namespace {

/* MEOS WKB variant flag from meos_geo.h: 0 = base, 0x04 = extended (with SRID). */
constexpr uint8_t WKB_BASE = 0x00;
/* WKB_EXTENDED is provided by meos_geo.h as #define WKB_EXTENDED 0x04 */

inline Temporal *BlobToTempMVT(string_t b) {
    size_t sz = b.GetSize();
    uint8_t *copy = (uint8_t *)malloc(sz);
    memcpy(copy, b.GetData(), sz);
    return reinterpret_cast<Temporal *>(copy);
}

inline STBox *BlobToStboxMVT(string_t b) {
    size_t sz = b.GetSize();
    uint8_t *copy = (uint8_t *)malloc(sz);
    memcpy(copy, b.GetData(), sz);
    return reinterpret_cast<STBox *>(copy);
}

void TgeoAsMVTGeomExec(DataChunk &args, ExpressionState &state, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t i = 0; i < args.ColumnCount(); i++) args.data[i].Flatten(row_count);
    const idx_t cc = args.ColumnCount();

    auto in_temp   = FlatVector::GetData<string_t>(args.data[0]);
    auto in_bounds = FlatVector::GetData<string_t>(args.data[1]);
    auto &v0 = FlatVector::Validity(args.data[0]);
    auto &v1 = FlatVector::Validity(args.data[1]);
    auto &out_validity = FlatVector::Validity(result);

    auto &struct_children = StructVector::GetEntries(result);
    auto &geom_col = *struct_children[0];
    auto &times_col = *struct_children[1];
    auto times_entries = FlatVector::GetData<list_entry_t>(times_col);
    auto &times_child = ListVector::GetEntry(times_col);

    idx_t total_times = 0;
    for (idx_t row = 0; row < row_count; row++) {
        if (!v0.RowIsValid(row) || !v1.RowIsValid(row)) {
            out_validity.SetInvalid(row);
            times_entries[row] = list_entry_t{total_times, 0};
            continue;
        }
        Temporal *t = BlobToTempMVT(in_temp[row]);
        STBox *bx = BlobToStboxMVT(in_bounds[row]);
        int32_t extent = 4096;
        int32_t buffer = 256;
        bool clip = true;
        if (cc > 2) extent = FlatVector::GetData<int32_t>(args.data[2])[row];
        if (cc > 3) buffer = FlatVector::GetData<int32_t>(args.data[3])[row];
        if (cc > 4) clip   = FlatVector::GetData<bool>(args.data[4])[row];

        GSERIALIZED *geom = nullptr;
        int64 *times = nullptr;
        int count = 0;
        bool found = tpoint_as_mvtgeom(t, bx, extent, buffer, clip, &geom, &times, &count);
        free(t); free(bx);
        if (!found || !geom) {
            out_validity.SetInvalid(row);
            times_entries[row] = list_entry_t{total_times, 0};
            if (geom) free(geom);
            if (times) free(times);
            continue;
        }
        /* Encode geom into the geometry struct child */
        ArenaAllocator arena(BufferAllocator::Get(state.GetContext()));
        string_t enc = GSerializedToGeometry(geom, arena, geom_col);
        FlatVector::GetData<string_t>(geom_col)[row] =
            StringVector::AddStringOrBlob(geom_col, enc);
        free(geom);
        /* Encode times[] into the bigint[] struct child */
        if (count > 0 && times) {
            ListVector::Reserve(times_col, total_times + count);
            ListVector::SetListSize(times_col, total_times + count);
            times_entries[row] = list_entry_t{total_times, static_cast<uint64_t>(count)};
            auto times_data = FlatVector::GetData<int64_t>(times_child);
            for (int k = 0; k < count; k++) {
                times_data[total_times + k] = (int64_t) times[k];
            }
            total_times += count;
        } else {
            times_entries[row] = list_entry_t{total_times, 0};
        }
        if (times) free(times);
    }
    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

} // anonymous namespace for round-trip WKB

void TgeompointType::RegisterAnalyticsViz(ExtensionLoader &loader) {
    const auto T  = tgeompoint();
    const auto B  = StboxType::stbox();
    const auto G  = MobilityDuckGeometryType();
    const auto I  = LogicalType::INTEGER;
    const auto BL = LogicalType::BOOLEAN;

    /* asMVTGeom returns STRUCT(geom GEOMETRY, times BIGINT[]) */
    const auto MVT_OUT = LogicalType::STRUCT({
        {"geom", G},
        {"times", LogicalType::LIST(LogicalType::BIGINT)},
    });
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMVTGeom", {T, B},                MVT_OUT, TgeoAsMVTGeomExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMVTGeom", {T, B, I},             MVT_OUT, TgeoAsMVTGeomExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMVTGeom", {T, B, I, I},          MVT_OUT, TgeoAsMVTGeomExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMVTGeom", {T, B, I, I, BL},      MVT_OUT, TgeoAsMVTGeomExec));

    // geoMeasure(tgeompoint | tgeogpoint, tfloat[, boolean]) is generated from the catalog
    // (tpoint_tfloat_to_geomeas, meos_geo_conversion).
}

} // namespace duckdb
