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
     * Spatial functions
     ****************************************************/
    static void Tpoint_length(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tpoint_trajectory(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tpoint_trajectory_gs(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_at_geom(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_at_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tspatial_transform(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Spatial relationships
     ****************************************************/
    static void Adisjoint_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Edwithin_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Eintersects_tgeo_geo(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Temporal-spatial relationships
     ****************************************************/
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
};

} // namespace duckdb