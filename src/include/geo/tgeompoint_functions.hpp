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
    static void Spatialarr_as_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void Spatialarr_as_ewkt(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tspatial_to_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static bool Tspatial_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* ***************************************************
     * Conversion functions
     ****************************************************/
    static bool Temporal_to_tstzspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* ***************************************************
     * Accessor functions
     ****************************************************/
    // timestamps: Temporal_timestamps

    /* ***************************************************
     * Stops function
     ****************************************************/
    
    /* ***************************************************
     * Spatial functions
     ****************************************************/

    static void Tpoint_make_simple(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Distance functions
     ****************************************************/
    static void collect_gs(DataChunk &args, ExpressionState &state, Vector &result);
    static void distance_geo_geo(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Affine / translate / rotate / scale transforms
     ****************************************************/
    static void Tgeo_rotate_angle(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_rotate_geom(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_scale_geom_origin(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
