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

namespace {

/* spaceSplit / spaceTimeSplit accept both literal and LATERAL column
 * inputs through DuckDB's in_out_function plumbing.  The bind callback
 * only declares the return types; the per-row parameters are read at
 * exec time from the input DataChunk.  The local state carries the
 * iteration progress so that the output is produced lazily, bounded by
 * STANDARD_VECTOR_SIZE.  The pattern mirrors
 * duckdb/extension/icu/icu-table-range.cpp. */

struct SpaceSplitBindData : public TableFunctionData {
    bool with_time = false;
};

struct SpaceSplitLocalState : public LocalTableFunctionState {
    /* Row of the input chunk under split, and whether its bins have
     * already been computed into the vectors below.
     * space_ewkb[i] is the raw EWKB serialisation of the i-th spatial bin
     * (GSERIALIZED -> EWKB when the row is loaded, decoded into the result
     * vector's geometry format when emitted, using DuckDB-spatial's
     * wkb_reader).  tpoint holds pre-built tgeompoint-aliased BLOB values.
     * time_bin is populated only by the spaceTimeSplit overload. */
    idx_t current_input_row = 0;
    bool initialized_row = false;
    idx_t out_idx = 0;
    std::vector<std::vector<uint8_t>> space_ewkb;
    std::vector<Value> time_bin;
    std::vector<Value> tpoint;

    void Reset() {
        space_ewkb.clear();
        time_bin.clear();
        tpoint.clear();
        out_idx = 0;
    }
};

GSERIALIZED *DefaultOriginSplit() {
    return geompoint_make3dz(0, 0.0, 0.0, 0.0);
}

unique_ptr<FunctionData> SpaceSplitBindCommon(ClientContext &context,
                                              TableFunctionBindInput &input,
                                              vector<LogicalType> &return_types,
                                              vector<string> &names,
                                              bool with_time) {
    auto bd = make_uniq<SpaceSplitBindData>();
    bd->with_time = with_time;
    if (with_time) {
        return_types = {MobilityDuckGeometryType(), LogicalType::TIMESTAMP_TZ, TgeompointType::tgeompoint()};
        names = {"spaceBin", "timeBin", "tpoint"};
    } else {
        return_types = {MobilityDuckGeometryType(), TgeompointType::tgeompoint()};
        names = {"spaceBin", "tpoint"};
    }
    return std::move(bd);
}

unique_ptr<FunctionData> SpaceSplitBind(ClientContext &context, TableFunctionBindInput &input,
                                        vector<LogicalType> &return_types, vector<string> &names) {
    return SpaceSplitBindCommon(context, input, return_types, names, /*with_time=*/false);
}

unique_ptr<FunctionData> SpaceTimeSplitBind(ClientContext &context, TableFunctionBindInput &input,
                                            vector<LogicalType> &return_types, vector<string> &names) {
    return SpaceSplitBindCommon(context, input, return_types, names, /*with_time=*/true);
}

unique_ptr<LocalTableFunctionState> SpaceSplitLocalInit(ExecutionContext &context,
                                                        TableFunctionInitInput &input,
                                                        GlobalTableFunctionState *) {
    return make_uniq<SpaceSplitLocalState>();
}

void EmitSpaceBinAt(ClientContext &context, Vector &col, idx_t row,
                    const std::vector<uint8_t> &ewkb) {
    if (ewkb.empty()) {
        FlatVector::SetNull(col, row, true);
        return;
    }
    GSERIALIZED *gs = geo_from_ewkb(ewkb.data(), ewkb.size(), 0);
    if (!gs) {
        FlatVector::SetNull(col, row, true);
        return;
    }
    ArenaAllocator arena(BufferAllocator::Get(context));
    string_t enc = GSerializedToGeometry(gs, arena, col);
    auto out_data = FlatVector::GetData<string_t>(col);
    out_data[row] = StringVector::AddStringOrBlob(col, enc);
    free(gs);
}

/* Read one input row's parameters and pre-compute all of its split bins
 * into the local state.  Called once per input row by the in_out_function
 * below.
 *
 * Input column layout:
 *   spaceSplit:     [0]=tgeompoint, [1]=xsize, [2]=ysize, [3]=zsize,
 *                   [4]=sorigin (geometry, optional),
 *                   [5]=bitmatrix (bool, optional)
 *   spaceTimeSplit: [0]=tgeompoint, [1]=xsize, [2]=ysize, [3]=zsize,
 *                   [4]=duration (interval),
 *                   [5]=sorigin (geometry, optional),
 *                   [6]=torigin (timestamptz, optional),
 *                   [7]=bitmatrix (bool, optional)
 */
void LoadSpaceSplitRow(ClientContext &context, SpaceSplitLocalState &state,
                       DataChunk &input, idx_t row_idx, bool with_time) {
    state.Reset();
    for (idx_t c = 0; c < input.ColumnCount(); c++) {
        input.data[c].Flatten(input.size());
    }
    if (FlatVector::IsNull(input.data[0], row_idx)) {
        return;
    }
    string_t blob = FlatVector::GetData<string_t>(input.data[0])[row_idx];
    double xsize = FlatVector::GetData<double>(input.data[1])[row_idx];
    double ysize = FlatVector::GetData<double>(input.data[2])[row_idx];
    double zsize = FlatVector::GetData<double>(input.data[3])[row_idx];

    idx_t col = 4;
    MeosInterval mi{};
    if (with_time) {
        if (input.ColumnCount() <= col || FlatVector::IsNull(input.data[col], row_idx)) {
            return;
        }
        mi = IntervaltToInterval(FlatVector::GetData<interval_t>(input.data[col])[row_idx]);
        col++;
    }
    GSERIALIZED *origin = nullptr;
    if (input.ColumnCount() > col && !FlatVector::IsNull(input.data[col], row_idx)) {
        string_t origin_blob = FlatVector::GetData<string_t>(input.data[col])[row_idx];
        origin = GeometryToGSerialized(origin_blob, 0);
    }
    col++;
    TimestampTz torigin = DEFAULT_TIME_ORIGIN_MEOS;
    if (with_time && input.ColumnCount() > col && !FlatVector::IsNull(input.data[col], row_idx)) {
        timestamp_tz_t t = FlatVector::GetData<timestamp_tz_t>(input.data[col])[row_idx];
        torigin = (TimestampTz) DuckDBToMeosTimestamp(t).value;
        col++;
    }
    bool bitmatrix = true;
    if (input.ColumnCount() > col && !FlatVector::IsNull(input.data[col], row_idx)) {
        bitmatrix = FlatVector::GetData<bool>(input.data[col])[row_idx];
    }
    if (!origin) origin = DefaultOriginSplit();

    Temporal *temp = (Temporal *) malloc(blob.GetSize());
    memcpy(temp, blob.GetData(), blob.GetSize());

    int count = 0;
    Temporal **trajs = nullptr;
    GSERIALIZED **bins = nullptr;
    TimestampTz *tbins = nullptr;
    if (with_time) {
        trajs = tgeo_space_time_split(temp, xsize, ysize, zsize, &mi, origin, torigin,
                                      bitmatrix, true, &bins, &tbins, &count);
    } else {
        trajs = tgeo_space_split(temp, xsize, ysize, zsize, origin, bitmatrix, true, &bins,
                                 &count);
    }
    free(temp);
    free(origin);

    if (!trajs || count <= 0) {
        if (trajs) free(trajs);
        if (bins) free(bins);
        if (tbins) free(tbins);
        return;
    }

    state.space_ewkb.reserve(count);
    if (with_time) state.time_bin.reserve(count);
    state.tpoint.reserve(count);

    for (int i = 0; i < count; i++) {
        /* Capture the spaceBin as EWKB; the DuckDB-spatial encoding happens
         * when the row is emitted, where an arena allocator scoped to the
         * result vector is available. */
        size_t wkb_sz = 0;
        uint8_t *wkb = geo_as_ewkb(bins[i], nullptr, &wkb_sz);
        if (wkb) {
            state.space_ewkb.emplace_back(wkb, wkb + wkb_sz);
            free(wkb);
        } else {
            state.space_ewkb.emplace_back();
        }
        free(bins[i]);

        if (with_time) {
            timestamp_tz_t t = MeosToDuckDBTimestamp(timestamp_tz_t((int64_t) tbins[i]));
            state.time_bin.emplace_back(Value::TIMESTAMPTZ(t));
        }

        size_t sz = temporal_mem_size(trajs[i]);
        Value tblob = Value::BLOB(reinterpret_cast<const_data_ptr_t>(trajs[i]), sz);
        tblob.Reinterpret(TgeompointType::tgeompoint());
        state.tpoint.push_back(std::move(tblob));
        free(trajs[i]);
    }
    free(trajs);
    free(bins);
    if (tbins) free(tbins);
}

OperatorResultType SpaceSplitInOutCommon(ExecutionContext &context, TableFunctionInput &data_p,
                                          DataChunk &input, DataChunk &output, bool with_time) {
    auto &state = data_p.local_state->Cast<SpaceSplitLocalState>();
    idx_t out_row = 0;

    /* PhysicalTableScan resets `output` (chunk.size() = 0) before each call
     * and treats output.size() == 0 as FINISHED regardless of the returned
     * OperatorResultType, while PhysicalTableInOutFunction (the LATERAL
     * path) honours the return value.  As in
     * duckdb/extension/icu/icu-table-range.cpp, leaving the chunk untouched
     * (size 0) and returning NEED_MORE_INPUT once the input chunk is drained
     * stops the source-scan path and advances the LATERAL path to the next
     * input chunk. */
    while (out_row < STANDARD_VECTOR_SIZE) {
        if (!state.initialized_row) {
            if (state.current_input_row >= input.size()) {
                /* Chunk drained.  Rows emitted in this call are returned as
                 * HAVE_MORE_OUTPUT with state.current_input_row left at
                 * input.size(), so the next invocation falls straight back
                 * into this branch with out_row == 0. */
                if (out_row > 0) {
                    output.SetCardinality(out_row);
                    return OperatorResultType::HAVE_MORE_OUTPUT;
                }
                state.current_input_row = 0;
                return OperatorResultType::NEED_MORE_INPUT;
            }
            LoadSpaceSplitRow(context.client, state, input, state.current_input_row, with_time);
            state.initialized_row = true;
        }
        if (state.out_idx >= state.tpoint.size()) {
            state.current_input_row++;
            state.initialized_row = false;
            continue;
        }
        EmitSpaceBinAt(context.client, output.data[0], out_row, state.space_ewkb[state.out_idx]);
        if (with_time) {
            output.data[1].SetValue(out_row, state.time_bin[state.out_idx]);
            output.data[2].SetValue(out_row, state.tpoint[state.out_idx]);
        } else {
            output.data[1].SetValue(out_row, state.tpoint[state.out_idx]);
        }
        state.out_idx++;
        out_row++;
    }
    output.SetCardinality(out_row);
    return OperatorResultType::HAVE_MORE_OUTPUT;
}

OperatorResultType SpaceSplitInOut(ExecutionContext &context, TableFunctionInput &data_p,
                                    DataChunk &input, DataChunk &output) {
    return SpaceSplitInOutCommon(context, data_p, input, output, /*with_time=*/false);
}

OperatorResultType SpaceTimeSplitInOut(ExecutionContext &context, TableFunctionInput &data_p,
                                        DataChunk &input, DataChunk &output) {
    return SpaceSplitInOutCommon(context, data_p, input, output, /*with_time=*/true);
}

} // namespace

void TgeompointType::RegisterTpointSplit(ExtensionLoader &loader) {
    const auto T  = tgeompoint();
    const auto D  = LogicalType::DOUBLE;
    const auto I  = LogicalType::INTERVAL;
    const auto TS = LogicalType::TIMESTAMP_TZ;
    const auto G  = MobilityDuckGeometryType();
    const auto B  = LogicalType::BOOLEAN;

    /* spaceSplit overloads (tgeompoint, xsize, ysize, zsize[, sorigin geom[, bitmatrix bool]]) */
    {
        std::vector<vector<LogicalType>> arg_lists = {
            {T, D, D, D},
            {T, D, D, D, G},
            {T, D, D, D, G, B},
        };
        for (auto &args : arg_lists) {
            TableFunction fn("spaceSplit", args, /*function=*/nullptr, SpaceSplitBind,
                             /*init_global=*/nullptr, SpaceSplitLocalInit);
            fn.in_out_function = SpaceSplitInOut;
            RegisterMeosFunction(loader, fn);
        }
    }

    /* spaceTimeSplit overloads (tgeompoint, xsize, ysize, zsize, duration[, sorigin[, torigin[, bitmatrix]]]) */
    {
        std::vector<vector<LogicalType>> arg_lists = {
            {T, D, D, D, I},
            {T, D, D, D, I, G},
            {T, D, D, D, I, G, TS},
            {T, D, D, D, I, G, TS, B},
        };
        for (auto &args : arg_lists) {
            TableFunction fn("spaceTimeSplit", args, /*function=*/nullptr, SpaceTimeSplitBind,
                             /*init_global=*/nullptr, SpaceSplitLocalInit);
            fn.in_out_function = SpaceTimeSplitInOut;
            RegisterMeosFunction(loader, fn);
        }
    }
}

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
