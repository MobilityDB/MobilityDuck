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
    meosType temptype;
} alias_type_struct;

struct TemporalHelpers {
    static meosType GetTemptypeFromAlias(const char *alias);
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
    template <typename T>
    static void Tinstant_constructor_common(Vector &value, Vector &ts, Vector &result, idx_t count);
    static void Tinstant_constructor_text(Vector &value, Vector &ts, Vector &result, idx_t count);
    static void Tinstant_constructor(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tsequence_constructor(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tsequenceset_constructor(DataChunk &args, ExpressionState &state, Vector &result);

    static void Tsequence_from_base_tstzset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tsequence_from_base_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tsequenceset_from_base_tstzspanset(DataChunk &args, ExpressionState &state, Vector &result);
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
    static void Temporal_subtype(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_interp(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_mem_size(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tinstant_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_valueset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_valuespans(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_start_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_end_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_min_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_max_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_avg_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_value_n(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_num_instants(DataChunk &args, ExpressionState &state, Vector &result); 
    static void Temporal_min_instant(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_max_instant(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tinstant_timestamptz(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_time(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_duration(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_sequences(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_start_timestamptz(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_end_timestamptz(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_timestamps(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_instants(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_num_sequences(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_lower_inc(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_upper_inc(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_start_instant(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_end_instant(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_instant_n(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_num_timestamps(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_timestamptz_n(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_start_sequence(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_end_sequence(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_sequence_n(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_segments(DataChunk &args, ExpressionState &state, Vector &result);
    /* ***************************************************
     * Shift and scale 
     ****************************************************/
    static void Temporal_shift_time(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_scale_time(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_shift_scale_time(DataChunk &args, ExpressionState &state, Vector &result);
    // TODO: static void Temporal_tprecision
    // TODO: static void Temporal_tsample

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
    static void Temporal_append_tinstant(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_append_tsequence(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_merge(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_merge_array(DataChunk &args, ExpressionState &state, Vector &result);
    /* ***************************************************
     * Restriction functions
     ****************************************************/
    static void Temporal_at_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_at_values(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_minus_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_at_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_minus_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_at_spanset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_minus_spanset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_at_min(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_minus_min(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_at_max(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_minus_max(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_at_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_minus_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_at_timestamptz(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_minus_timestamptz(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_value_at_timestamptz(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_at_tstzset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_minus_tstzset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_at_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_minus_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_at_tstzspanset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_minus_tstzspanset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_before_timestamptz(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_after_timestamptz(DataChunk &args, ExpressionState &state, Vector &result);
    /* ***************************************************
     * Modification Functions
     ****************************************************/
    static void Temporal_insert(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_update(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_delete_timestamptz(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_delete_tstzset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_delete_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_delete_tstzspanset(DataChunk &args, ExpressionState &state, Vector &result);


    /* ***************************************************
     * Modification Functions
    ****************************************************/
    static void Temporal_segm_min_duration(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_segm_max_duration(DataChunk &args, ExpressionState &state, Vector &result);

    // TODO: Stop functions 
    
    /* ***************************************************
     * Local Aggregate Functions
     ****************************************************/
    static void Tnumber_integral(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_twavg(DataChunk &args, ExpressionState &state, Vector &result);

    // TODO: Selectivity functions 

    /* ***************************************************
     * Boolean operators
     ****************************************************/
    static void Tbool_when_true(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Boolean operators on tbool
     ****************************************************/
    static void Tand_tbool_bool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tand_bool_tbool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tand_tbool_tbool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tor_tbool_bool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tor_bool_tbool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tor_tbool_tbool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnot_tbool(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Arithmetic operators on tnumber
     ****************************************************/
    static void Add_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Add_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Add_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Add_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Add_tnumber_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Sub_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Sub_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Sub_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Sub_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Sub_tnumber_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Mult_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Mult_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Mult_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Mult_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Mult_tnumber_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Div_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Div_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Div_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Div_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Div_tnumber_tnumber(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Unary tnumber functions
     ****************************************************/
    static void Tnumber_abs(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_delta_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_trend(DataChunk &args, ExpressionState &state, Vector &result);
    // PROJ pipeline projection — works on any spatio-temporal type.
    // Signature: transformPipeline(temp, pipeline text, srid int, is_forward bool).
    static void Tspatial_transform_pipeline(DataChunk &args, ExpressionState &state, Vector &result);
    // Temporal_derivative declared in the math-functions block below.

    /* ***************************************************
     * MFJSON / Hex(E)WKB I/O
     ****************************************************/
    static void Temporal_as_mfjson(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_as_hexwkb(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_as_ewkb(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_tprecision(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_tsample(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tint_from_mfjson(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tfloat_from_mfjson(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tbool_from_mfjson(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ttext_from_mfjson(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeompoint_from_mfjson(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeogpoint_from_mfjson(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeometry_from_mfjson(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tgeography_from_mfjson(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tfloat_degrees(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tfloat_radians(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Distance operator on tnumber
     ****************************************************/
    static void Tdistance_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tdistance_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tdistance_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tdistance_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tdistance_tnumber_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
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
    static void Contains_temporal_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contained_temporal_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overlaps_temporal_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adjacent_temporal_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contains_tstzspan_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contained_tstzspan_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overlaps_tstzspan_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adjacent_tstzspan_temporal(DataChunk &args, ExpressionState &state, Vector &result);

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
    static void Ever_eq_text_ttext(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_eq_ttext_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_eq_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    // always_eq
    static void Always_eq_bool_tbool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_eq_tbool_bool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_eq_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_eq_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_eq_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_eq_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_eq_text_ttext(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_eq_ttext_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_eq_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    // ever_ne
    static void Ever_ne_bool_tbool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ne_tbool_bool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ne_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ne_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ne_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ne_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ne_text_ttext(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ne_ttext_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ne_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    // always_ne
    static void Always_ne_bool_tbool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ne_tbool_bool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ne_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ne_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ne_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ne_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ne_text_ttext(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ne_ttext_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ne_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);

    // Ever / always ordering (no tbool — boolean has no ordering)
    // ever_lt
    static void Ever_lt_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_lt_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_lt_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_lt_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_lt_text_ttext(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_lt_ttext_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_lt_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    // always_lt
    static void Always_lt_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_lt_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_lt_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_lt_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_lt_text_ttext(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_lt_ttext_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_lt_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    // ever_le
    static void Ever_le_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_le_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_le_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_le_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_le_text_ttext(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_le_ttext_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_le_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    // always_le
    static void Always_le_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_le_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_le_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_le_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_le_text_ttext(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_le_ttext_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_le_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    // ever_gt
    static void Ever_gt_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_gt_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_gt_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_gt_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_gt_text_ttext(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_gt_ttext_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_gt_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    // always_gt
    static void Always_gt_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_gt_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_gt_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_gt_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_gt_text_ttext(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_gt_ttext_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_gt_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    // ever_ge
    static void Ever_ge_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ge_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ge_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ge_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ge_text_ttext(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ge_ttext_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ever_ge_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);
    // always_ge
    static void Always_ge_int_tint(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ge_tint_int(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ge_float_tfloat(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ge_tfloat_float(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ge_text_ttext(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ge_ttext_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void Always_ge_temporal_temporal(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Similarity measures
     ****************************************************/
    static void Temporal_frechet_distance(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_dyntimewarp_distance(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_hausdorff_distance(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Temporal simplification — Douglas-Peucker and friends
     ****************************************************/
    static void Temporal_simplify_dp(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_simplify_min_dist(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_simplify_max_dist(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_simplify_min_tdelta(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * tnumber × {numspan, tbox} topological predicates
     * (4 ops × 4 type-pair shapes = 16 wrappers)
     ****************************************************/
    static void Contains_tnumber_numspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contained_tnumber_numspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overlaps_tnumber_numspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adjacent_tnumber_numspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contains_numspan_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contained_numspan_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overlaps_numspan_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adjacent_numspan_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contains_tnumber_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contained_tnumber_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overlaps_tnumber_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adjacent_tnumber_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contains_tbox_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contained_tbox_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overlaps_tbox_tnumber(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adjacent_tbox_tnumber(DataChunk &args, ExpressionState &state, Vector &result);

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
     * Text functions on ttext
     ****************************************************/
    static void Ttext_lower(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ttext_upper(DataChunk &args, ExpressionState &state, Vector &result);
    static void Ttext_initcap(DataChunk &args, ExpressionState &state, Vector &result);
    static void Textcat_text_ttext(DataChunk &args, ExpressionState &state, Vector &result);
    static void Textcat_ttext_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void Textcat_ttext_ttext(DataChunk &args, ExpressionState &state, Vector &result);

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
    static void Temporal_dump_common(DataChunk &args, Vector &result, meosType basetype);
    static void Temporal_dump(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Math functions
     ****************************************************/
    static void Temporal_round(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_derivative(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb
