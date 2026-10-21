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
    static bool Stbox_out(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

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
