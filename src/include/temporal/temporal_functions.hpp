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

    /* ***************************************************
     * Conversion functions: [TYPE] -> Temporal
     ****************************************************/
    static void Temporal_to_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_to_span(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Accessor functions
     ****************************************************/
    static void Temporal_subtype(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_interp(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_mem_size(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tinstant_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_valueset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_start_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_end_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_min_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_max_value(DataChunk &args, ExpressionState &state, Vector &result);
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
    //TODO:
    //static void Temporal_segments

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
    static void Temporal_to_tsequence(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_to_tsequenceset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_shift_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_scale_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_shift_scale_value(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Restriction functions
     ****************************************************/
    static void Temporal_at_value_tbool(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_at_timestamptz(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_at_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_at_tstzspanset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tnumber_at_span(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_at_min(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Boolean operators
     ****************************************************/
    static void Tbool_when_true(DataChunk &args, ExpressionState &state, Vector &result);

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
};

} // namespace duckdb