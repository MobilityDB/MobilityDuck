#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/typedefs.hpp"

#include "span.hpp"
#include "set.hpp"
#include "spanset.hpp"

namespace duckdb {

class ExtensionLoader;

#define OUT_DEFAULT_DECIMAL_DIGITS 15
#define Float8GetDatum(X) ((Datum) *(uint64_t *) &(X))
#define Int32GetDatum(X) ((Datum) (X))

struct TboxFunctions {
    /* ***************************************************
     * In/out functions: VARCHAR <-> tbox
     ****************************************************/
    static bool Tbox_in(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tbox_out(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* ***************************************************
     * Conversion functions + cast functions: [TYPE] -> tbox
     ****************************************************/
    template <typename TA>
    static void NumberToTboxExecutor(Vector &value, MeosType basetype, Vector &result, idx_t count);
    static bool Number_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    // TODO: what is duckdb equivalent of numeric?
    // static void Numeric_to_tbox(DataChunk &args, ExpressionState &state, Vector &result);

    static void TimestamptzToTboxExecutor(Vector &value, Vector &result, idx_t count);
    static bool Timestamptz_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    static void SetToTboxExecutor(Vector &value, Vector &result, idx_t count);
    static bool Set_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    static void SpanToTboxExecutor(Vector &value, Vector &result, idx_t count);
    static bool Span_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    static void SpansetToTboxExecutor(Vector &value, Vector &result, idx_t count); 
    static bool Spanset_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    /* ***************************************************
     * Conversion functions + cast functions: tbox -> [TYPE]
     ****************************************************/
    static void TboxToIntspanExecutor(Vector &value, Vector &result, idx_t count);
    static bool Tbox_to_intspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    static void TboxToFloatspanExecutor(Vector &value, Vector &result, idx_t count);
    static bool Tbox_to_floatspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    static void TboxToTstzspanExecutor(Vector &value, Vector &result, idx_t count);
    static bool Tbox_to_tstzspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    // TODO: Selectivity functions
    
    /********************************************************
    * Topological operators 
    ********************************************************/
    static void Contains_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Contained_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overlaps_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result);    
    static void Same_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Adjacent_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result);

    /********************************************************
    * Position operators
    ********************************************************/
    static void Left_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overleft_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Right_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overright_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Before_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overbefore_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void After_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static void Overafter_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result);

    /********************************************************
    * set operators
    ********************************************************/
    static void Intersection_tbox_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    // TODO: extend aggregation
    // TODO: Comparison
    static void Tbox_eq(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tbox_ne(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tbox_lt(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tbox_le(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tbox_ge(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tbox_gt(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tbox_cmp(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * WKB / hex-WKB serialization + hashing
     ****************************************************/
};

} // namespace duckdb
