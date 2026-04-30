#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/typedefs.hpp"

#include "temporal/span.hpp"
#include "temporal/set.hpp"

#include "tydef.hpp"

namespace duckdb {

class ExtensionLoader;

struct TgeompointFunctions {
    /* ***************************************************
     * In/out functions
     ****************************************************/
    static bool Tpoint_in(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    // Out function: overload Temporal_out
    static void Tspatial_as_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tspatial_as_ewkt(DataChunk &args, ExpressionState &state, Vector &result);
    static void Spatialarr_as_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void Spatialarr_as_ewkt(DataChunk &args, ExpressionState &state, Vector &result);
    /* ***************************************************
    * Constructor functions
    ****************************************************/
    static void Tpointinst_constructor(DataChunk &args, ExpressionState &state, Vector &result);
    // tgeompointSeq: overload temporal's Tsequence_constructor
    static void Tspatial_to_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static bool Tspatial_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    // getTime: Temporal_time
    static void Tgeompoint_start_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeompoint_end_value(DataChunk &args, ExpressionState &state, Vector &result);
    // duration: Temporal_duration
    // startTimestamp: Temporal_start_timestamptz
    static void Tgeompoint_sequence_constructor(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Conversion functions
     ****************************************************/
    static void Temporal_to_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static bool Temporal_to_tstzspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* ***************************************************
     * Accessor functions
     ****************************************************/
    static void Tgeompoint_value(DataChunk &args, ExpressionState &state, Vector &result);
    // timestamps: Temporal_timestamps

    /* ***************************************************
     * Restriction functions
     ****************************************************/
    static void Tgeompoint_at_value(DataChunk &args, ExpressionState &state, Vector &result);
    // atTime(tgeompoint, timestamptz): Temporal_at_timestamptz
    // atTime(tgeompoint, tstzspan): Temporal_at_tstzspan
    // atTime(tgeompoint, tstzspanset): Temporal_at_tstzspanset
    static void Tgeompoint_value_at_timestamptz(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Stops function
     ****************************************************/
    static void Tgeompoint_stops(DataChunk &args, ExpressionState &state, Vector &result);
    
    /* ***************************************************
    * TODO: Ever/Always Comparison functions
    ****************************************************/
    static void Ever_eq_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_eq_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ne_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ne_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_eq_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_eq_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ne_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ne_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_eq_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_eq_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ne_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ne_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Teq_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tne_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Teq_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Teq_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tne_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    /* ***************************************************
     * Spatial functions
     ****************************************************/
    static void Tpoint_get_x(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tpoint_get_y(DataChunk &args, ExpressionState &state, Vector &result);  
    static void Tpoint_get_z(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tpoint_length(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tpoint_cumulative_length(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tpoint_twcentroid(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tpoint_direction(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tpoint_azimuth(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tpoint_angular_difference(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tpoint_is_simple(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tpoint_make_simple(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tpoint_trajectory(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tpoint_trajectory_gs(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_at_geom(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_minus_geom(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_at_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_minus_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tspatial_transform(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Spatial relationships
     ****************************************************/
    static void Econtains_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Acontains_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Edisjoint_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Edisjoint_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Edisjoint_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adisjoint_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adisjoint_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adisjoint_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Eintersects_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Eintersects_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Eintersects_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Aintersects_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Aintersects_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Aintersects_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Etouches_geo_tpoint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Etouches_tpoint_geo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Atouches_geo_tpoint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Atouches_tpoint_geo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Edwithin_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Edwithin_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Edwithin_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adwithin_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adwithin_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adwithin_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    /* ***************************************************
     * Temporal-spatial relationships
     ****************************************************/
    static void Tcontains_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tdisjoint_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tdisjoint_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tdisjoint_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tintersects_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tintersects_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tintersects_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ttouches_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ttouches_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tdwithin_geo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tdwithin_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tdwithin_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void ShortestLine_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    /* ***************************************************
     * Operators (workaround as functions)
     ****************************************************/
    static void Temporal_overlaps_tgeompoint_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_overlaps_tgeompoint_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_contains_tgeompoint_stbox(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Distance function
     ****************************************************/
    static void Tdistance_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    // static void gs_as_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void collect_gs(DataChunk &args, ExpressionState &state, Vector &result);
    static void distance_geo_geo(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Affine and derived spatial transforms
     ****************************************************/
    static void Tgeo_affine(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_affine_2d(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_translate_2d(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_translate_3d(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_rotate(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_rotate_xy(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_rotate_x(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_rotate_y(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_transscale(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_scale_geom(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_scale_geom_origin(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
