#include "meos_wrapper_simple.hpp"

#include "common.hpp"
#include "geo/stbox.hpp"
#include "geo/stbox_functions.hpp"
#include "geo_util.hpp"
#include "time_util.hpp"

#include "duckdb/common/types/blob.hpp"
#include "duckdb/function/function.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include <scoped_allocator>
#include "spatial/spatial_types.hpp"

namespace duckdb {

LogicalType StboxType::STBOX() {
    LogicalType type(LogicalTypeId::BLOB);
    type.SetAlias("STBOX");
    return type;
}

void StboxType::RegisterType(ExtensionLoader &loader) {
    loader.RegisterType( "STBOX", STBOX());
}

void StboxType::RegisterCastFunctions(ExtensionLoader &loader) {
    loader.RegisterCastFunction(
        LogicalType::VARCHAR,
        STBOX(),
        StboxFunctions::Stbox_in_cast
    );

    loader.RegisterCastFunction(
        STBOX(),
        LogicalType::VARCHAR,
        StboxFunctions::Stbox_out
    );

    loader.RegisterCastFunction(
        GeoTypes::GEOMETRY(),
        STBOX(),
        StboxFunctions::Geo_to_stbox_cast
    );

    loader.RegisterCastFunction(
        LogicalType::TIMESTAMP_TZ,
        STBOX(),
        StboxFunctions::Timestamptz_to_stbox_cast
    );

    loader.RegisterCastFunction(
        SetTypes::tstzset(),
        STBOX(),
        StboxFunctions::Tstzset_to_stbox_cast
    );

    loader.RegisterCastFunction(
        SpanTypes::TSTZSPAN(),
        STBOX(),
        StboxFunctions::Tstzspan_to_stbox_cast
    );

    loader.RegisterCastFunction(
        SpansetTypes::tstzspanset(),
        STBOX(),
        StboxFunctions::Tstzspanset_to_stbox_cast
    );
}

void StboxType::RegisterScalarFunctions(ExtensionLoader &loader) {
    loader.RegisterFunction(
        ScalarFunction("stbox", {LogicalType::VARCHAR}, STBOX(), StboxFunctions::Stbox_in, nullptr, nullptr, nullptr,
                     nullptr, LogicalType(LogicalTypeId::INVALID), FunctionStability::VOLATILE));

    loader.RegisterFunction(
        ScalarFunction(
            "stboxFromBinary",
            {LogicalType::BLOB},
            STBOX(),
            StboxFunctions::Stbox_from_wkb
        )
    );

    // ExtensionUtil::RegisterFunction(
    //     instance,
    //     ScalarFunction(
    //         "stboxFromHexWKB",
    //         {LogicalType::VARCHAR},
    //         STBOX(),
    //         StboxFunctions::Stbox_from_hexwkb
    //     )
    // );

    loader.RegisterFunction(
        ScalarFunction(
            "asText",
            {STBOX()},
            LogicalType::VARCHAR,
            StboxFunctions::Stbox_as_text
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "asBinary",
            {STBOX()},
            LogicalType::BLOB,
            StboxFunctions::Stbox_as_wkb
        )
    );

    // ExtensionUtil::RegisterFunction(
    //     instance,
    //     ScalarFunction(
    //         "asHexWKB",
    //         {STBOX()},
    //         LogicalType::VARCHAR,
    //         StboxFunctions::Stbox_as_hexwkb
    //     )
    // );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox",
            {GeoTypes::GEOMETRY(), LogicalType::TIMESTAMP_TZ},
            StboxType::STBOX(),
            StboxFunctions::Geo_timestamptz_to_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox",
            {GeoTypes::GEOMETRY(), SpanTypes::TSTZSPAN()},
            StboxType::STBOX(),
            StboxFunctions::Geo_tstzspan_to_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox",
            {LogicalType::TIMESTAMP_TZ},
            StboxType::STBOX(),
            StboxFunctions::Timestamptz_to_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox",
            {SetTypes::tstzset()},
            StboxType::STBOX(),
            StboxFunctions::Tstzset_to_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox",
            {SpanTypes::TSTZSPAN()},
            StboxType::STBOX(),
            StboxFunctions::Tstzspan_to_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox",
            {SpansetTypes::tstzspanset()},
            StboxType::STBOX(),
            StboxFunctions::Tstzspanset_to_stbox
        )
    );
    

    loader.RegisterFunction(
        ScalarFunction(
            "stbox",
            {GeoTypes::GEOMETRY()},
            StboxType::STBOX(),
            StboxFunctions::Geo_to_stbox
        )
    );

    // -----------------------------------------------------------------
    // Typed STBox constructors — stboxX/Z/T/XT/ZT and the geodstbox*
    // variants. Mirrors MobilityDB's `051_stbox.in.sql` surface; all
    // funnel through MEOS' single `stbox_make` constructor.
    // -----------------------------------------------------------------
    {
        const auto STB = StboxType::STBOX();
        const auto DBL = LogicalType::DOUBLE;
        const auto INT = LogicalType::INTEGER;
        const auto TS  = LogicalType::TIMESTAMP_TZ;
        const auto SP  = SpanTypes::TSTZSPAN();

        // stboxX(xmin, ymin, xmax, ymax, [srid])
        loader.RegisterFunction(ScalarFunction("stboxX",
            {DBL, DBL, DBL, DBL}, STB, StboxFunctions::Stbox_constructor_x));
        loader.RegisterFunction(ScalarFunction("stboxX",
            {DBL, DBL, DBL, DBL, INT}, STB, StboxFunctions::Stbox_constructor_x));

        // stboxZ(xmin, ymin, zmin, xmax, ymax, zmax, [srid])
        loader.RegisterFunction(ScalarFunction("stboxZ",
            {DBL, DBL, DBL, DBL, DBL, DBL}, STB, StboxFunctions::Stbox_constructor_z));
        loader.RegisterFunction(ScalarFunction("stboxZ",
            {DBL, DBL, DBL, DBL, DBL, DBL, INT}, STB, StboxFunctions::Stbox_constructor_z));

        // stboxT(timestamptz | tstzspan)
        loader.RegisterFunction(ScalarFunction("stboxT", {TS}, STB,
            StboxFunctions::Stbox_constructor_t_timestamp));
        loader.RegisterFunction(ScalarFunction("stboxT", {SP}, STB,
            StboxFunctions::Stbox_constructor_t_span));

        // stboxXT(xmin, ymin, xmax, ymax, ts | span, [srid])
        loader.RegisterFunction(ScalarFunction("stboxXT",
            {DBL, DBL, DBL, DBL, TS}, STB,
            StboxFunctions::Stbox_constructor_xt_timestamp));
        loader.RegisterFunction(ScalarFunction("stboxXT",
            {DBL, DBL, DBL, DBL, TS, INT}, STB,
            StboxFunctions::Stbox_constructor_xt_timestamp));
        loader.RegisterFunction(ScalarFunction("stboxXT",
            {DBL, DBL, DBL, DBL, SP}, STB,
            StboxFunctions::Stbox_constructor_xt_span));
        loader.RegisterFunction(ScalarFunction("stboxXT",
            {DBL, DBL, DBL, DBL, SP, INT}, STB,
            StboxFunctions::Stbox_constructor_xt_span));

        // stboxZT(xmin, ymin, zmin, xmax, ymax, zmax, ts | span, [srid])
        loader.RegisterFunction(ScalarFunction("stboxZT",
            {DBL, DBL, DBL, DBL, DBL, DBL, TS}, STB,
            StboxFunctions::Stbox_constructor_zt_timestamp));
        loader.RegisterFunction(ScalarFunction("stboxZT",
            {DBL, DBL, DBL, DBL, DBL, DBL, TS, INT}, STB,
            StboxFunctions::Stbox_constructor_zt_timestamp));
        loader.RegisterFunction(ScalarFunction("stboxZT",
            {DBL, DBL, DBL, DBL, DBL, DBL, SP}, STB,
            StboxFunctions::Stbox_constructor_zt_span));
        loader.RegisterFunction(ScalarFunction("stboxZT",
            {DBL, DBL, DBL, DBL, DBL, DBL, SP, INT}, STB,
            StboxFunctions::Stbox_constructor_zt_span));

        // Geodetic variants — same shapes but geodetic flag set.
        loader.RegisterFunction(ScalarFunction("geodstboxZ",
            {DBL, DBL, DBL, DBL, DBL, DBL}, STB,
            StboxFunctions::Geodstbox_constructor_z));
        loader.RegisterFunction(ScalarFunction("geodstboxZ",
            {DBL, DBL, DBL, DBL, DBL, DBL, INT}, STB,
            StboxFunctions::Geodstbox_constructor_z));
        loader.RegisterFunction(ScalarFunction("geodstboxT", {TS}, STB,
            StboxFunctions::Geodstbox_constructor_t_timestamp));
        loader.RegisterFunction(ScalarFunction("geodstboxT", {SP}, STB,
            StboxFunctions::Geodstbox_constructor_t_span));
        loader.RegisterFunction(ScalarFunction("geodstboxZT",
            {DBL, DBL, DBL, DBL, DBL, DBL, TS}, STB,
            StboxFunctions::Geodstbox_constructor_zt_timestamp));
        loader.RegisterFunction(ScalarFunction("geodstboxZT",
            {DBL, DBL, DBL, DBL, DBL, DBL, TS, INT}, STB,
            StboxFunctions::Geodstbox_constructor_zt_timestamp));
        loader.RegisterFunction(ScalarFunction("geodstboxZT",
            {DBL, DBL, DBL, DBL, DBL, DBL, SP}, STB,
            StboxFunctions::Geodstbox_constructor_zt_span));
        loader.RegisterFunction(ScalarFunction("geodstboxZT",
            {DBL, DBL, DBL, DBL, DBL, DBL, SP, INT}, STB,
            StboxFunctions::Geodstbox_constructor_zt_span));
    }

    loader.RegisterFunction(
        ScalarFunction(
            "geometry",
            {STBOX()},
            GeoTypes::GEOMETRY(),
            StboxFunctions::Stbox_to_geo
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "hasX",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_hasx
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "hasZ",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_hasz
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "hasT",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_hast
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "isGeodetic",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_isgeodetic
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "Xmin",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_xmin
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "Ymin",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_ymin
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "Zmin",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_zmin
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "Tmin",
            {STBOX()},
            LogicalType::TIMESTAMP_TZ,
            StboxFunctions::Stbox_tmin
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "TminInc",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_tmin_inc
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "Xmax",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_xmax
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "Ymax",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_ymax
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "Zmax",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_zmax
        )
    );
 
    loader.RegisterFunction(
        ScalarFunction(
            "Tmax",
            {STBOX()},
            LogicalType::TIMESTAMP_TZ,
            StboxFunctions::Stbox_tmax
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "TmaxInc",
            {STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_tmax_inc
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "area",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_area
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "area",
            {STBOX(), LogicalType::BOOLEAN},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_area
        )
    );

    // spaceTiles(stbox, xsize, ysize, zsize, [sorigin geometry,
    //            [borderInc bool]]) -> LIST<stbox>
    // spaceTimeTiles(stbox, xsize, ysize, zsize, duration interval,
    //                [sorigin geometry, [torigin timestamptz,
    //                 [borderInc bool]]]) -> LIST<stbox>
    // Both wrap MEOS' `stbox_space_tiles` / `stbox_space_time_tiles`.
    auto emit_stbox_list_local = [](Vector &result, idx_t row_idx, STBox *boxes,
                                    int count, idx_t &total_offset,
                                    list_entry_t *list_entries, Vector &child_vec,
                                    ValidityMask &result_validity) {
        if (!boxes || count <= 0) {
            if (boxes) free(boxes);
            result_validity.SetInvalid(row_idx);
            return;
        }
        ListVector::SetListSize(result, total_offset + count);
        list_entries[row_idx] = list_entry_t{total_offset, (uint64_t) count};
        auto *child_data = FlatVector::GetData<string_t>(child_vec);
        for (int j = 0; j < count; ++j) {
            child_data[total_offset + j] = StringVector::AddStringOrBlob(
                child_vec, reinterpret_cast<const char *>(&boxes[j]), sizeof(STBox));
        }
        free(boxes);
        total_offset += count;
    };

    auto space_tiles_exec = [emit_stbox_list_local](
        DataChunk &args, ExpressionState &, Vector &result) {
        const idx_t cnt = args.size();
        for (idx_t c = 0; c < args.ColumnCount(); ++c) args.data[c].Flatten(cnt);
        auto &result_validity = FlatVector::Validity(result);
        auto list_entries = FlatVector::GetData<list_entry_t>(result);
        auto &child_vec = ListVector::GetEntry(result);
        child_vec.SetVectorType(VectorType::FLAT_VECTOR);
        ListVector::Reserve(result, cnt);
        idx_t total_offset = 0;

        auto box_data = FlatVector::GetData<string_t>(args.data[0]);
        auto x_data = FlatVector::GetData<double>(args.data[1]);
        auto y_data = FlatVector::GetData<double>(args.data[2]);
        auto z_data = FlatVector::GetData<double>(args.data[3]);
        const bool with_origin = args.ColumnCount() >= 5;
        const bool with_border = args.ColumnCount() >= 6;
        for (idx_t i = 0; i < cnt; ++i) {
            if (FlatVector::IsNull(args.data[0], i)) {
                result_validity.SetInvalid(i);
                continue;
            }
            STBox box;
            memcpy(&box, box_data[i].GetData(), sizeof(STBox));
            GSERIALIZED *origin = nullptr;
            if (with_origin && !FlatVector::IsNull(args.data[4], i)) {
                string_t g = FlatVector::GetData<string_t>(args.data[4])[i];
                origin = GeometryToGSerialized(g, 0);
            } else {
                origin = geompoint_make3dz(0, 0.0, 0.0, 0.0);
            }
            bool border_inc = true;
            if (with_border && !FlatVector::IsNull(args.data[5], i)) {
                border_inc = FlatVector::GetData<bool>(args.data[5])[i];
            }
            int count = 0;
            STBox *tiles = stbox_space_tiles(&box, x_data[i], y_data[i],
                                              z_data[i], origin, border_inc, &count);
            if (origin) free(origin);
            emit_stbox_list_local(result, i, tiles, count, total_offset,
                                  list_entries, child_vec, result_validity);
        }
    };

    auto space_time_tiles_exec = [emit_stbox_list_local](
        DataChunk &args, ExpressionState &, Vector &result) {
        const idx_t cnt = args.size();
        for (idx_t c = 0; c < args.ColumnCount(); ++c) args.data[c].Flatten(cnt);
        auto &result_validity = FlatVector::Validity(result);
        auto list_entries = FlatVector::GetData<list_entry_t>(result);
        auto &child_vec = ListVector::GetEntry(result);
        child_vec.SetVectorType(VectorType::FLAT_VECTOR);
        ListVector::Reserve(result, cnt);
        idx_t total_offset = 0;

        auto box_data = FlatVector::GetData<string_t>(args.data[0]);
        auto x_data = FlatVector::GetData<double>(args.data[1]);
        auto y_data = FlatVector::GetData<double>(args.data[2]);
        auto z_data = FlatVector::GetData<double>(args.data[3]);
        auto dur_data = FlatVector::GetData<interval_t>(args.data[4]);
        const bool with_origin = args.ColumnCount() >= 6;
        const bool with_torigin = args.ColumnCount() >= 7;
        const bool with_border = args.ColumnCount() >= 8;
        for (idx_t i = 0; i < cnt; ++i) {
            if (FlatVector::IsNull(args.data[0], i)) {
                result_validity.SetInvalid(i);
                continue;
            }
            STBox box;
            memcpy(&box, box_data[i].GetData(), sizeof(STBox));
            MeosInterval iv = IntervaltToInterval(dur_data[i]);
            GSERIALIZED *origin = nullptr;
            if (with_origin && !FlatVector::IsNull(args.data[5], i)) {
                string_t g = FlatVector::GetData<string_t>(args.data[5])[i];
                origin = GeometryToGSerialized(g, 0);
            } else {
                origin = geompoint_make3dz(0, 0.0, 0.0, 0.0);
            }
            // MobilityDB defaults torigin to '2000-01-03 00:00:00+00';
            // MEOS interprets that timestamp as the "PG epoch + 2 days"
            // anchor. Use the same constant the cluster G code uses.
            constexpr int64_t DEFAULT_TIME_ORIGIN_MEOS = 2LL * 86400LL * 1000000LL;
            TimestampTz torigin = (TimestampTz) DEFAULT_TIME_ORIGIN_MEOS;
            if (with_torigin && !FlatVector::IsNull(args.data[6], i)) {
                timestamp_tz_t in = FlatVector::GetData<timestamp_tz_t>(args.data[6])[i];
                torigin = (TimestampTz) DuckDBToMeosTimestamp(in).value;
            }
            bool border_inc = true;
            if (with_border && !FlatVector::IsNull(args.data[7], i)) {
                border_inc = FlatVector::GetData<bool>(args.data[7])[i];
            }
            int count = 0;
            STBox *tiles = stbox_space_time_tiles(&box, x_data[i], y_data[i],
                                                   z_data[i], &iv, origin,
                                                   torigin, border_inc, &count);
            if (origin) free(origin);
            emit_stbox_list_local(result, i, tiles, count, total_offset,
                                  list_entries, child_vec, result_validity);
        }
    };

    LogicalType list_stbox = LogicalType::LIST(STBOX());
    const auto DBL = LogicalType::DOUBLE;
    const auto IVAL = LogicalType::INTERVAL;
    const auto TS = LogicalType::TIMESTAMP_TZ;
    const auto BOOL = LogicalType::BOOLEAN;
    const auto GEOM = GeoTypes::GEOMETRY();

    loader.RegisterFunction(ScalarFunction("spaceTiles",
        {STBOX(), DBL, DBL, DBL}, list_stbox, space_tiles_exec));
    loader.RegisterFunction(ScalarFunction("spaceTiles",
        {STBOX(), DBL, DBL, DBL, GEOM}, list_stbox, space_tiles_exec));
    loader.RegisterFunction(ScalarFunction("spaceTiles",
        {STBOX(), DBL, DBL, DBL, GEOM, BOOL}, list_stbox, space_tiles_exec));

    loader.RegisterFunction(ScalarFunction("spaceTimeTiles",
        {STBOX(), DBL, DBL, DBL, IVAL}, list_stbox, space_time_tiles_exec));
    loader.RegisterFunction(ScalarFunction("spaceTimeTiles",
        {STBOX(), DBL, DBL, DBL, IVAL, GEOM}, list_stbox, space_time_tiles_exec));
    loader.RegisterFunction(ScalarFunction("spaceTimeTiles",
        {STBOX(), DBL, DBL, DBL, IVAL, GEOM, TS}, list_stbox, space_time_tiles_exec));
    loader.RegisterFunction(ScalarFunction("spaceTimeTiles",
        {STBOX(), DBL, DBL, DBL, IVAL, GEOM, TS, BOOL}, list_stbox, space_time_tiles_exec));

    loader.RegisterFunction(
        ScalarFunction(
            "perimeter",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_perimeter
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "perimeter",
            {STBOX(), LogicalType::BOOLEAN},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_perimeter
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "volume",
            {STBOX()},
            LogicalType::DOUBLE,
            StboxFunctions::Stbox_volume
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "shiftTime",
            {STBOX(), LogicalType::INTERVAL},
            STBOX(),
            StboxFunctions::Stbox_shift_time
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "scaleTime",
            {STBOX(), LogicalType::INTERVAL},
            STBOX(),
            StboxFunctions::Stbox_scale_time
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "shiftScaleTime",
            {STBOX(), LogicalType::INTERVAL, LogicalType::INTERVAL},
            STBOX(),
            StboxFunctions::Stbox_shift_scale_time
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "getSpace",
            {STBOX()},
            STBOX(),
            StboxFunctions::Stbox_get_space
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "expandTime",
            {STBOX(), LogicalType::INTERVAL},
            STBOX(),
            StboxFunctions::Stbox_expand_time
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "expandSpace",
            {STBOX(), LogicalType::DOUBLE},
            STBOX(),
            StboxFunctions::Stbox_expand_space
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_contains",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contains_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_contained",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contained_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_overlaps",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overlaps_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_same",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Same_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_adjacent",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Adjacent_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "@>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contains_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "<@",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Contained_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "&&",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overlaps_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "~=",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Same_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "-|-",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Adjacent_stbox_stbox
        )
    );

        loader.RegisterFunction(
        ScalarFunction(
            "stbox_left",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Left_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_overleft",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overleft_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_right",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Right_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_overright",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overright_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_below",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Below_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_overbelow",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overbelow_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_above",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Above_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_overabove",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overabove_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_before",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Before_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_overbefore",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overbefore_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_after",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::After_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_overafter",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overafter_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_front",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Front_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_overfront",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overfront_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_back",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Back_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_overback",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overback_stbox_stbox
        )
    );

        loader.RegisterFunction(
        ScalarFunction(
            "<<",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Left_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "&<",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overleft_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            ">>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Right_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "&>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overright_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "<<|",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Below_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "&<|",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overbelow_stbox_stbox
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "|>>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Above_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "|&>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overabove_stbox_stbox
        )
    );
// # is not a operator in Duckdb, fix later
    loader.RegisterFunction(
        ScalarFunction(
            "<<#",  
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Before_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "&<#",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overbefore_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "#>>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::After_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "#&>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overafter_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "<</",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Front_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "&</",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overfront_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "/>>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Back_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "/&>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Overback_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_union",
            {STBOX(), STBOX()},
            STBOX(),
            StboxFunctions::Union_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_intersection",
            {STBOX(), STBOX()},
            STBOX(),
            StboxFunctions::Intersection_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "+",
            {STBOX(), STBOX()},
            STBOX(),
            StboxFunctions::Union_stbox_stbox
        )
    );
    
    loader.RegisterFunction(
        ScalarFunction(
            "*",
            {STBOX(), STBOX()},
            STBOX(),
            StboxFunctions::Intersection_stbox_stbox
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "stbox_cmp",
            {STBOX(), STBOX()},
            LogicalType::INTEGER,
            StboxFunctions::Stbox_cmp
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_eq",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_eq
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_ne",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_ne
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_lt",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_lt
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_le",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_le
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_ge",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_ge
        )
    );  
    loader.RegisterFunction(
        ScalarFunction(
            "stbox_gt",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_gt
        )
    );

    loader.RegisterFunction(
        ScalarFunction(
            "=",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_eq
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "<>",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_ne
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "<",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_lt
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            "<=",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_le
        )
    );
    loader.RegisterFunction(
        ScalarFunction(
            ">=",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_ge
        )
    );  
    loader.RegisterFunction(
        ScalarFunction(
            ">",
            {STBOX(), STBOX()},
            LogicalType::BOOLEAN,
            StboxFunctions::Stbox_gt
        )
    );

    // transformPipeline(stbox, pipeline text, srid int, is_forward bool)
    loader.RegisterFunction(
        ScalarFunction(
            "transformPipeline",
            {STBOX(), LogicalType::VARCHAR, LogicalType::INTEGER, LogicalType::BOOLEAN},
            STBOX(),
            StboxFunctions::Stbox_transform_pipeline
        )
    );
}

} // namespace duckdb
