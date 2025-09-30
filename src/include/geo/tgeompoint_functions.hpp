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

    /* ***************************************************
     * Conversion functions
     ****************************************************/
    static void Temporal_to_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static bool Temporal_to_tstzspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* ***************************************************
     * Restriction functions
     ****************************************************/
    static void Tgeompoint_at_value(DataChunk &args, ExpressionState &state, Vector &result);
    // atTime(tgeompoint, tstzspan): Temporal_at_tstzspan
    // atTime(tgeompoint, tstzspanset): Temporal_at_tstzspanset
    static void Tgeompoint_value_at_timestamptz(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Spatial functions
     ****************************************************/
    static void Tpoint_length(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tpoint_trajectory(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_at_geom(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Spatial relationships
     ****************************************************/
    static void Adisjoint_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);
    static void Edwithin_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Temporal-spatial relationships
     ****************************************************/
    static void Tdwithin_tgeo_tgeo(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Operators (workaround as functions)
     ****************************************************/
    static void Temporal_overlaps_tgeompoint_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_overlaps_tgeompoint_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_contains_tgeompoint_stbox(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb