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

    /* ***************************************************
     * Constructor functions
     ****************************************************/
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
    static void Tinstant_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_valueset(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_start_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_end_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_min_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_max_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_value_n(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_min_instant(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_max_instant(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tinstant_timestamptz(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_time(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_duration(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_sequences(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_start_timestamptz(DataChunk &args, ExpressionState &state, Vector &result);

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
    static void Temporal_at_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static void Temporal_at_tstzspanset(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Boolean operators
     ****************************************************/
    static void Tbool_when_true(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb