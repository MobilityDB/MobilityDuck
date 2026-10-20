// Cross-type predicate registrations for tgeography. The MEOS C functions
// dispatched here all take Temporal * (subtype-agnostic), so this file is
// purely registration-and-glue; the heavy lifting stays in libmeos.

#include "meos_wrapper_simple.hpp"
#include "duckdb_version_compat.hpp"

#include "common.hpp"
#include "geo/tgeography.hpp"
#include "geo/tgeography_ops.hpp"
#include "geo/tgeometry.hpp"
#include "geo/tgeompoint_functions.hpp"
#include "geo/stbox.hpp"
#include "temporal/span.hpp"
#include "temporal/temporal.hpp"
#include "geo_util.hpp"
#include "time_util.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "spatial/spatial_types.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_geo.h>
    #include <meos_internal.h>
    #include <meos_internal_geo.h>
}

namespace duckdb {

namespace {

// ====================================================================
// Argument-decoding helpers
// ====================================================================

inline Temporal *DecodeTemporalCopy(string_t blob) {
    size_t sz = blob.GetSize();
    Temporal *t = (Temporal *) malloc(sz);
    memcpy(t, blob.GetData(), sz);
    return t;
}

// ====================================================================
// Spatial functions — SRID accessor, setSRID, transform, expand*,
// round, traversedArea, centroid, convexHull, and the
// tgeography <-> tgeogpoint coercions.
// ====================================================================

inline string_t StboxToBlob(Vector &result, STBox *box) {
    string_t out = StringVector::AddStringOrBlob(
        result, reinterpret_cast<const char *>(box), sizeof(STBox));
    free(box);
    return out;
}

void TspatialToStboxExec(DataChunk &args, ExpressionState &, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t blob) {
            Temporal *t = DecodeTemporalCopy(blob);
            STBox *box = tspatial_to_stbox(t);
            free(t);
            return StboxToBlob(result, box);
        });
}

} // namespace

void TGeographyOps::RegisterScalarFunctions(ExtensionLoader &loader) {
    const LogicalType TGEOM = TGeographyTypes::tgeography();
    const LogicalType GEOM  = MobilityDuckGeometryType();
    const LogicalType stbox = StboxType::stbox();
    const LogicalType tstzspan = SpanTypes::tstzspan();
    const LogicalType BOOL = LogicalType::BOOLEAN;
    const LogicalType DBL = LogicalType::DOUBLE;
    const LogicalType tfloat = TemporalTypes::tfloat();

    // Time-axis position predicates on tgeography × tstzspan (both directions;
    // before/after/overbefore/overafter, backing fns before_temporal_tstzspan etc.
    // @ingroup meos_temporal_bbox_pos) are generated from the catalog sqlSignatures
    // in src/generated/generated_temporal_udfs.cpp. The stale-snake temporal_*
    // aliases are dropped (bare names supersede them).


    // tgeography → stbox is a cast in the SQL surface; expose it as a
    // function for now to keep the implementation a single template.
    RegisterMeosFunction(loader, ScalarFunction(
        "stbox", {TGEOM}, stbox, TspatialToStboxExec));

    // Centroid / convexHull / traversedArea — produce a non-temporal
    // geometry summary of the trajectory.

    // -----------------------------------------------------------------
    // Tile / box emitters — spaceBoxes(tgeography, x, y, z) and
    // spaceTimeBoxes(tgeography, x, y, z, interval). Both return
    // LIST<stbox> with the bounding boxes that cover the input.
    // -----------------------------------------------------------------
    auto emit_stbox_list = [](Vector &result, idx_t row_idx, STBox *boxes, int count,
                              idx_t &total_offset, list_entry_t *list_entries,
                              Vector &child_vector, ValidityMask &result_validity) {
        if (!boxes || count <= 0) {
            if (boxes) free(boxes);
            result_validity.SetInvalid(row_idx);
            return;
        }
        ListVector::SetListSize(result, total_offset + count);
        list_entries[row_idx] = list_entry_t{total_offset, static_cast<uint64_t>(count)};
        auto *child_data = FlatVector::GetData<string_t>(child_vector);
        const size_t stbox_bytes = sizeof(STBox);
        for (int j = 0; j < count; ++j) {
            child_data[total_offset + j] = StringVector::AddStringOrBlob(
                child_vector, reinterpret_cast<const char *>(&boxes[j]), stbox_bytes);
        }
        free(boxes);
        total_offset += count;
    };

    auto space_boxes_exec = [emit_stbox_list]
        (DataChunk &args, ExpressionState &, Vector &result) {
        const idx_t row_count = args.size();
        for (idx_t c = 0; c < args.ColumnCount(); ++c) args.data[c].Flatten(row_count);

        auto &result_validity = FlatVector::Validity(result);
        auto list_entries = FlatVector::GetData<list_entry_t>(result);
        auto &child_vector = ListVector::GetEntry(result);
        child_vector.SetVectorType(VectorType::FLAT_VECTOR);
        ListVector::Reserve(result, row_count);
        idx_t total_offset = 0;

        auto t_data = FlatVector::GetData<string_t>(args.data[0]);
        auto x_data = FlatVector::GetData<double>(args.data[1]);
        auto y_data = FlatVector::GetData<double>(args.data[2]);
        auto z_data = FlatVector::GetData<double>(args.data[3]);
        for (idx_t i = 0; i < row_count; ++i) {
            if (FlatVector::IsNull(args.data[0], i) || FlatVector::IsNull(args.data[1], i) ||
                FlatVector::IsNull(args.data[2], i) || FlatVector::IsNull(args.data[3], i)) {
                result_validity.SetInvalid(i);
                continue;
            }
            Temporal *t = DecodeTemporalCopy(t_data[i]);
            GSERIALIZED *origin = geompoint_make3dz(0, 0.0, 0.0, 0.0);
            int count = 0;
            STBox *boxes = tgeo_space_boxes(
                t, x_data[i], y_data[i], z_data[i], origin,
                /*bitmatrix=*/true, /*border_inc=*/true, &count);
            free(t); free(origin);
            emit_stbox_list(result, i, boxes, count, total_offset, list_entries,
                            child_vector, result_validity);
        }
    };

    LogicalType list_stbox = LogicalType::LIST(stbox);
    RegisterMeosFunction(loader, ScalarFunction(
        "spaceBoxes", {TGEOM, DBL, DBL, DBL}, list_stbox, space_boxes_exec));

    auto space_time_boxes_exec = [emit_stbox_list]
        (DataChunk &args, ExpressionState &, Vector &result) {
        const idx_t row_count = args.size();
        for (idx_t c = 0; c < args.ColumnCount(); ++c) args.data[c].Flatten(row_count);

        auto &result_validity = FlatVector::Validity(result);
        auto list_entries = FlatVector::GetData<list_entry_t>(result);
        auto &child_vector = ListVector::GetEntry(result);
        child_vector.SetVectorType(VectorType::FLAT_VECTOR);
        ListVector::Reserve(result, row_count);
        idx_t total_offset = 0;

        auto t_data = FlatVector::GetData<string_t>(args.data[0]);
        auto x_data = FlatVector::GetData<double>(args.data[1]);
        auto y_data = FlatVector::GetData<double>(args.data[2]);
        auto z_data = FlatVector::GetData<double>(args.data[3]);
        auto dur_data = FlatVector::GetData<interval_t>(args.data[4]);
        for (idx_t i = 0; i < row_count; ++i) {
            if (FlatVector::IsNull(args.data[0], i) || FlatVector::IsNull(args.data[1], i) ||
                FlatVector::IsNull(args.data[2], i) || FlatVector::IsNull(args.data[3], i) ||
                FlatVector::IsNull(args.data[4], i)) {
                result_validity.SetInvalid(i);
                continue;
            }
            Temporal *t = DecodeTemporalCopy(t_data[i]);
            GSERIALIZED *origin = geompoint_make3dz(0, 0.0, 0.0, 0.0);
            MeosInterval iv = IntervaltToInterval(dur_data[i]);
            int count = 0;
            STBox *boxes = tgeo_space_time_boxes(
                t, x_data[i], y_data[i], z_data[i], &iv, origin,
                (TimestampTz) DEFAULT_TIME_ORIGIN_MEOS,
                /*bitmatrix=*/true, /*border_inc=*/true, &count);
            free(t); free(origin);
            emit_stbox_list(result, i, boxes, count, total_offset, list_entries,
                            child_vector, result_validity);
        }
    };

    RegisterMeosFunction(loader, ScalarFunction(
        "spaceTimeBoxes",
        {TGEOM, DBL, DBL, DBL, LogicalType::INTERVAL},
        list_stbox, space_time_boxes_exec));
}

} // namespace duckdb
