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
    static vector<Value> TempArrToArray(Temporal **temparr, int32_t count, LogicalType element_type);
};

struct TemporalFunctions {
    /* ***************************************************
     * In/out functions: VARCHAR <-> Temporal
     ****************************************************/
    static bool Temporal_in(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Temporal_out(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Composite_out(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Blob_to_tstzspanset(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* ***************************************************
     * Constructor functions
     ****************************************************/
    static void Tsequence_constructor(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tsequenceset_constructor(DataChunk &args, ExpressionState &state, Vector &result);

    static void Tsequenceset_constructor_gaps(DataChunk &args, ExpressionState &state, Vector &result);
    /***************************************************** 
     * Special cast 
     *****************************************************/

    static void Temporal_enforce_typmod(DataChunk &args, ExpressionState &state, Vector &result);
    static bool Temporal_enforce_typmod_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    
    /* ***************************************************
     * Conversion functions: [TYPE] -> Temporal
     ****************************************************/
    static void Temporal_to_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_to_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tbool_to_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tint_to_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tfloat_to_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_to_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static bool Tbool_to_tint_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tint_to_tfloat_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tfloat_to_tint_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tnumber_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    /* ***************************************************
     * Accessor functions
     ****************************************************/
    static void Tinstant_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_min_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_max_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_avg_value(DataChunk &args, ExpressionState &state, Vector &result);
    // Temporal_timestamps retired: timestamps(<temporal>) is generated (temporal_timestamps) in generated_temporal_udfs.cpp.
    /* ***************************************************
     * Shift and scale 
     ****************************************************/
    static void Temporal_shift_time(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_scale_time(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_shift_scale_time(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_tsample(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Transformation functions
     ****************************************************/
    static void Temporal_to_tinstant(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_to_tsequence(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_to_tsequenceset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_set_interp(DataChunk &args, ExpressionState &state, Vector &result);
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

    /* ***************************************************
     * Unary tnumber functions
     ****************************************************/
    static void Tnumber_abs(DataChunk &args, ExpressionState &state, Vector &result);
    // Tnumber_tboxes retired: tboxes(<tnumber>) is generated (tnumber_tboxes) in generated_temporal_udfs.cpp.
    static void Tnumber_delta_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_trend(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tfloat_exp(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tfloat_ln(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tfloat_log10(DataChunk &args, ExpressionState &state, Vector &result);
    // floor/ceil/round/degrees/radians on tfloat are generated (generated_temporal_udfs.cpp).

    /* ***************************************************
     * Temporal comparison predicates returning Temporal
     * (temporal_teq / tne / tlt / tle / tgt / tge)
     ****************************************************/
#define DECL_TCMP(OP)                                                                              \
    static void OP##_bool_tbool(DataChunk &args, ExpressionState &state, Vector &result);          \
    static void OP##_tbool_bool(DataChunk &args, ExpressionState &state, Vector &result);          \
    static void OP##_int_tint(DataChunk &args, ExpressionState &state, Vector &result);            \
    static void OP##_tint_int(DataChunk &args, ExpressionState &state, Vector &result);            \
    static void OP##_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);        \
    static void OP##_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);        \
    static void OP##_text_ttext(DataChunk &args, ExpressionState &state, Vector &result);          \
    static void OP##_ttext_text(DataChunk &args, ExpressionState &state, Vector &result);          \
    static void OP##_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);

    DECL_TCMP(Teq)
    DECL_TCMP(Tne)
#undef DECL_TCMP

#define DECL_TCMP_ORD(OP)                                                                          \
    static void OP##_int_tint(DataChunk &args, ExpressionState &state, Vector &result);            \
    static void OP##_tint_int(DataChunk &args, ExpressionState &state, Vector &result);            \
    static void OP##_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);        \
    static void OP##_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);        \
    static void OP##_text_ttext(DataChunk &args, ExpressionState &state, Vector &result);          \
    static void OP##_ttext_text(DataChunk &args, ExpressionState &state, Vector &result);          \
    static void OP##_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);

    DECL_TCMP_ORD(Tlt)
    DECL_TCMP_ORD(Tle)
    DECL_TCMP_ORD(Tgt)
    DECL_TCMP_ORD(Tge)
#undef DECL_TCMP_ORD

    /* ***************************************************
     * Distance operator on tnumber
     ****************************************************/
    static void Tdistance_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tdistance_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tdistance_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tdistance_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Nad_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Nad_tint_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Nad_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Nad_tfloat_tfloat(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Temporal topological predicates
     ****************************************************/
    static void Contains_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contained_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overlaps_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adjacent_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    static void Same_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contains_temporal_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contained_temporal_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overlaps_temporal_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adjacent_temporal_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Same_temporal_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contains_tstzspan_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contained_tstzspan_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overlaps_tstzspan_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adjacent_tstzspan_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    static void Same_tstzspan_temporal(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Temporal time-position predicates (<<#, #>>, &<#, #&>)
     ****************************************************/
    static void Before_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    static void After_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overbefore_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overafter_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    static void Before_temporal_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void After_temporal_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overbefore_temporal_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overafter_temporal_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Before_tstzspan_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    static void After_tstzspan_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overbefore_tstzspan_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overafter_tstzspan_temporal(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Ever / always equality and inequality
     * (DuckDB parser cannot accept ?= / #= operator names,
     * so the upstream MobilityDB ?= / ?<> / #= / #<> map to
     * named functions ever_eq / ever_ne / always_eq / always_ne.)
     ****************************************************/
    // ever_eq
    static void Ever_eq_bool_tbool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_eq_tbool_bool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_eq_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_eq_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_eq_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_eq_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_eq_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    // always_eq
    static void Always_eq_bool_tbool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_eq_tbool_bool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_eq_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_eq_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_eq_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_eq_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_eq_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    // ever_ne
    static void Ever_ne_bool_tbool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ne_tbool_bool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ne_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ne_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ne_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ne_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ne_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    // always_ne
    static void Always_ne_bool_tbool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ne_tbool_bool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ne_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ne_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ne_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ne_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ne_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);

    // Ever / always ordering (no tbool — boolean has no ordering)
    // ever_lt
    static void Ever_lt_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_lt_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_lt_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_lt_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_lt_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    // always_lt
    static void Always_lt_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_lt_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_lt_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_lt_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_lt_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    // ever_le
    static void Ever_le_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_le_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_le_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_le_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_le_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    // always_le
    static void Always_le_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_le_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_le_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_le_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_le_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    // ever_gt
    static void Ever_gt_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_gt_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_gt_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_gt_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_gt_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    // always_gt
    static void Always_gt_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_gt_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_gt_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_gt_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_gt_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    // ever_ge
    static void Ever_ge_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ge_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ge_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ge_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ge_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    // always_ge
    static void Always_ge_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ge_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ge_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ge_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ge_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Similarity measures
     ****************************************************/
    static void Temporal_frechet_distance(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_dyntimewarp_distance(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_hausdorff_distance(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * tnumber × {numspan, tbox} topological predicates
     * (4 ops × 4 type-pair shapes = 16 wrappers)
     ****************************************************/
    static void Contains_tnumber_numspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contained_tnumber_numspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overlaps_tnumber_numspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adjacent_tnumber_numspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Same_tnumber_numspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contains_numspan_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contained_numspan_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overlaps_numspan_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adjacent_numspan_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Same_numspan_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contains_tnumber_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contained_tnumber_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overlaps_tnumber_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adjacent_tnumber_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Same_tnumber_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contains_tbox_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contained_tbox_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overlaps_tbox_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adjacent_tbox_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Same_tbox_tnumber(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * tnumber × {numspan, tbox} position predicates
     * (4 ops × 4 type-pair shapes × 2 directions)
     ****************************************************/
    static void Left_tnumber_numspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Right_tnumber_numspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overleft_tnumber_numspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overright_tnumber_numspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Left_numspan_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Right_numspan_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overleft_numspan_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overright_numspan_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Left_tnumber_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Right_tnumber_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overleft_tnumber_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overright_tnumber_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Left_tbox_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Right_tbox_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overleft_tbox_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overright_tbox_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    // tnumber × tnumber position
    static void Left_tnumber_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Right_tnumber_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overleft_tnumber_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overright_tnumber_tnumber(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * tspatial × {stbox, tspatial} position predicates
     * (12 directions × 3 type-pair shapes)
     ****************************************************/
    static void Left_tspatial_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Right_tspatial_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Below_tspatial_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Above_tspatial_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Front_tspatial_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Back_tspatial_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overleft_tspatial_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overright_tspatial_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overbelow_tspatial_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overabove_tspatial_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overfront_tspatial_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overback_tspatial_stbox(DataChunk &args, ExpressionState &state, Vector &result);

    static void Left_stbox_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Right_stbox_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Below_stbox_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Above_stbox_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Front_stbox_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Back_stbox_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overleft_stbox_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overright_stbox_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overbelow_stbox_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overabove_stbox_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overfront_stbox_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overback_stbox_tspatial(DataChunk &args, ExpressionState &state, Vector &result);

    static void Left_tspatial_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Right_tspatial_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Below_tspatial_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Above_tspatial_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Front_tspatial_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Back_tspatial_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overleft_tspatial_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overright_tspatial_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overbelow_tspatial_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overabove_tspatial_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overfront_tspatial_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overback_tspatial_tspatial(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Time-axis position predicates on tspatial
     ****************************************************/
    static void Before_tspatial_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void After_tspatial_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overbefore_tspatial_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overafter_tspatial_stbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Before_stbox_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void After_stbox_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overbefore_stbox_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overafter_stbox_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Before_tspatial_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void After_tspatial_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overbefore_tspatial_tspatial(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overafter_tspatial_tspatial(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Text functions on ttext
     ****************************************************/
    static void Ttext_lower(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ttext_upper(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ttext_initcap(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Comparison operators
     ****************************************************/
    static void Temporal_eq(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_ne(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_le(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_lt(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_ge(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_gt(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_cmp(DataChunk &args, ExpressionState &state, Vector &result);
    
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
