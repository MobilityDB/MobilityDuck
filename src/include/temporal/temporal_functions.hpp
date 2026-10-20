#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/typedefs.hpp"

#include "span.hpp"
#include "set.hpp"

#include "tydef.hpp"

namespace duckdb {

class ExtensionLoader;

typedef struct {
    char *alias;
    MeosType temptype;
} alias_type_struct;

struct TemporalHelpers {
    static MeosType GetTemptypeFromAlias(const char *alias);
};

struct TemporalFunctions {
    /* ***************************************************
     * In/out functions: VARCHAR <-> Temporal
     ****************************************************/
    static bool Temporal_in(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Temporal_out(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Blob_to_tstzspanset(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* ***************************************************
     * Constructor functions
     ****************************************************/

    /***************************************************** 
     * Special cast 
     *****************************************************/

    static void Temporal_enforce_typmod(DataChunk &args, ExpressionState &state, Vector &result);
    
    /* ***************************************************
     * Conversion functions: [TYPE] -> Temporal
     ****************************************************/
    static bool Tbool_to_tint_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tint_to_tfloat_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tfloat_to_tint_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tnumber_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    /* ***************************************************
     * Accessor functions
     ****************************************************/
    static void Tnumber_avg_value(DataChunk &args, ExpressionState &state, Vector &result);
    // Temporal_timestamps retired: timestamps(<temporal>) is generated (temporal_timestamps) in generated_temporal_udfs.cpp.
    /* ***************************************************
     * Shift and scale 
     ****************************************************/
    static void Temporal_shift_scale_time(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_tsample(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Transformation functions
     ****************************************************/
    static void Temporal_to_tinstant(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_shift_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_scale_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_shift_scale_value(DataChunk &args, ExpressionState &state, Vector &result);
    /* ***************************************************
     * Restriction functions
     ****************************************************/
    /* ***************************************************
     * Modification Functions
     ****************************************************/
    /* ***************************************************
     * Modification Functions
    ****************************************************/
    // TODO: Stop functions 
    
    /* ***************************************************
     * Local Aggregate Functions
     ****************************************************/
    // TODO: Selectivity functions 

    /* ***************************************************
     * Boolean operators
     ****************************************************/
    static void Tbool_when_true(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Boolean operators on tbool
     ****************************************************/

    /* ***************************************************
     * Arithmetic operators on tnumber
     ****************************************************/

    // floor/ceil/round/degrees/radians on tfloat are generated (generated_temporal_udfs.cpp).

    /* ***************************************************
     * Workaround functions
     ****************************************************/
    template <typename T>
    static void Temporal_dump_common(DataChunk &args, Vector &result, MeosType basetype);
    static void Temporal_dump(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Math functions
     ****************************************************/
    // round(tfloat/tgeompoint/tgeogpoint/...) is generated (generated_temporal_udfs.cpp).

    /* ***************************************************
     * Serialization
     ****************************************************/
    static void Temporal_as_wkb(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_as_hexwkb(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
