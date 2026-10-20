#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/typedefs.hpp"

#include "temporal/span.hpp"
#include "temporal/set.hpp"
#include "temporal/spanset.hpp"

#include "tydef.hpp"

namespace duckdb {

class ExtensionLoader;

struct StboxFunctions {
    /* ***************************************************
     * In/out functions: VARCHAR <-> stbox
     ****************************************************/
    static bool Stbox_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static void Stbox_in(DataChunk &args, ExpressionState &state, Vector &result);
    static bool Stbox_out(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* ***************************************************
     * In/out functions: WKB/HexWKB <-> stbox
     ****************************************************/
    static void Stbox_from_hexwkb(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_as_hexwkb(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Conversion functions + cast functions: [TYPE] -> stbox
     ****************************************************/
    static void Geo_to_stbox_common(Vector &source, Vector &result, idx_t count);
    static bool Geo_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Timestamptz_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tstzset_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tstzspan_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tstzspanset_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* ***************************************************
     * TODO: Selectively functions for operators 
     ****************************************************/

    /* ***************************************************
     * Topological operators
     ****************************************************/

#define DECL_TSPATIAL_TOPO(OP)                                                                                  \
    static void OP##_tspatial_stbox(DataChunk &args, ExpressionState &state, Vector &result);                   \
    static void OP##_stbox_tspatial(DataChunk &args, ExpressionState &state, Vector &result);                   \
    static void OP##_tspatial_tspatial(DataChunk &args, ExpressionState &state, Vector &result);

    DECL_TSPATIAL_TOPO(Contains)
    DECL_TSPATIAL_TOPO(Contained)
    DECL_TSPATIAL_TOPO(Overlaps)
    DECL_TSPATIAL_TOPO(Same)
    DECL_TSPATIAL_TOPO(Adjacent)
#undef DECL_TSPATIAL_TOPO

    /* ***************************************************
     * Set operators
     ****************************************************/
    static void Intersection_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result);
     // TODO: Split functions + Extent aggregation 
    
    /* ***************************************************
     * Tile / box emitters and single-tile getters
     ****************************************************/
    static void Stbox_space_tiles(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_time_tiles(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_space_time_tiles(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_space_boxes(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeo_space_time_boxes(DataChunk &args, ExpressionState &state, Vector &result);
};

}
