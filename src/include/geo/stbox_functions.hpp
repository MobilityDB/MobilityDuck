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
     * In/out functions: VARCHAR <-> STBOX
     ****************************************************/
    static bool Stbox_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static void Stbox_in(DataChunk &args, ExpressionState &state, Vector &result);
    static bool Stbox_out(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* ***************************************************
     * In/out functions: WKB/HexWKB <-> STBOX
     ****************************************************/
    static void Stbox_from_wkb(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_from_hexwkb(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_as_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_as_wkb(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_as_hexwkb(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Constructor functions
     ****************************************************/
    // static void Stbox_constructor_x(DataChunk &args, ExpressionState &state, Vector &result);
    // static void Stbox_constructor_z(DataChunk &args, ExpressionState &state, Vector &result);
    // static void Stbox_constructor_t(DataChunk &args, ExpressionState &state, Vector &result);

    static void Geo_timestamptz_to_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Geo_tstzspan_to_stbox(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Conversion functions + cast functions: [TYPE] -> STBOX
     ****************************************************/
    static void Geo_to_stbox_common(Vector &source, Vector &result, idx_t count);
    static void Geo_to_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static bool Geo_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static void Timestamptz_to_stbox(DataChunk &args, ExpressionState &state, Vector &result); 
    static bool Timestamptz_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static void Tstzset_to_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static bool Tstzset_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static void Tstzspan_to_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static bool Tstzspan_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static void Tstzspanset_to_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static bool Tstzspanset_to_stbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* ***************************************************
     * Conversion functions + cast functions: STBOX -> [TYPE]
     ****************************************************/
    static void Stbox_to_geo(DataChunk &args, ExpressionState &state, Vector &result);
    // static void Stbox_to_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Accessor functions
     ****************************************************/
    static void Stbox_hasx(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_hasz(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_hast(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_isgeodetic(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_xmin(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_xmax(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_ymin(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_ymax(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_zmin(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_zmax(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_tmin(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_tmax(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_tmin_inc(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_tmax_inc(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_area(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_volume(DataChunk &args, ExpressionState &state, Vector &result);
    // TODO static void Stbox_perimeter(DataChunk &args, ExpressionState &state, Vector &result);
    /* ***************************************************
     * Transformation functions
     ****************************************************/
    static void Stbox_shift_time(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_scale_time(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_shift_scale_time(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_get_space(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_expand_space(DataChunk &args, ExpressionState &state, Vector &result);
    static void Stbox_expand_time(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * TODO: Selectively functions for operators 
     ****************************************************/

    /* ***************************************************
     * Topological operators
     ****************************************************/
    static void Contains_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contained_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overlaps_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Same_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adjacent_stbox_stbox(DataChunk &args, ExpressionState &state, Vector &result);
};

}