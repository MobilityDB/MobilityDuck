#pragma once

#include "meos_wrapper_simple.hpp"
#include "duckdb/common/typedefs.hpp"

#include "span.hpp"
#include "set.hpp"

namespace duckdb {

class ExtensionLoader;

#define OUT_DEFAULT_DECIMAL_DIGITS 15
#define Float8GetDatum(X) ((Datum) *(uint64_t *) &(X))
#define Int32GetDatum(X) ((Datum) (X))

struct TboxFunctions {
    /* ***************************************************
     * In/out functions: VARCHAR <-> TBOX
     ****************************************************/
    static bool Tbox_in(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
    static bool Tbox_out(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* ***************************************************
     * Constructor functions
     ****************************************************/
    template <typename TA>
    static void NumberTimestamptzToTboxExecutor(Vector &value, Vector &t, meosType basetype, Vector &result, idx_t count);
    static void Number_timestamptz_to_tbox(DataChunk &args, ExpressionState &state, Vector &result);

    static void Numspan_timestamptz_to_tbox(DataChunk &args, ExpressionState &state, Vector &result);

    template <typename TA>
    static void NumberTstzspanToTboxExecutor(Vector &value, Vector &span_str, meosType basetype, Vector &result, idx_t count);
    static void Number_tstzspan_to_tbox(DataChunk &args, ExpressionState &state, Vector &result);;

    static void Numspan_tstzspan_to_tbox(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Conversion functions + cast functions: [TYPE] -> TBOX
     ****************************************************/
    template <typename TA>
    static void NumberToTboxExecutor(Vector &value, meosType basetype, Vector &result, idx_t count);
    static void Number_to_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static bool Number_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    // TODO: what is duckdb equivalent of numeric?
    // static void Numeric_to_tbox(DataChunk &args, ExpressionState &state, Vector &result);

    static void TimestamptzToTboxExecutor(Vector &value, Vector &result, idx_t count);
    static void Timestamptz_to_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static bool Timestamptz_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    static void SetToTboxExecutor(Vector &value, Vector &result, idx_t count);
    static void Set_to_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static bool Set_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    static void SpanToTboxExecutor(Vector &value, Vector &result, idx_t count);
    static void Span_to_tbox(DataChunk &args, ExpressionState &state, Vector &result);
    static bool Span_to_tbox_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    // TODO: implement once spanset is available
    // static void Spanset_to_tbox(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Conversion functions + cast functions: TBOX -> [TYPE]
     ****************************************************/
    static void TboxToIntspanExecutor(Vector &value, Vector &result, idx_t count);
    static void Tbox_to_intspan(DataChunk &args, ExpressionState &state, Vector &result);
    static bool Tbox_to_intspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    static void TboxToFloatspanExecutor(Vector &value, Vector &result, idx_t count);
    static void Tbox_to_floatspan(DataChunk &args, ExpressionState &state, Vector &result);
    static bool Tbox_to_floatspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    static void TboxToTstzspanExecutor(Vector &value, Vector &result, idx_t count);
    static void Tbox_to_tstzspan(DataChunk &args, ExpressionState &state, Vector &result);
    static bool Tbox_to_tstzspan_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    /* ***************************************************
     * Accessor functions
     ****************************************************/
    static void Tbox_hasx(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tbox_hast(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tbox_xmin(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tbox_xmin_inc(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tbox_xmax(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tbox_xmax_inc(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tbox_tmin(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tbox_tmin_inc(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tbox_tmax(DataChunk &args, ExpressionState &state, Vector &result);
    static void Tbox_tmax_inc(DataChunk &args, ExpressionState &state, Vector &result);

    /* ***************************************************
     * Transformation functions
     ****************************************************/

    template <typename TB>
    static void TboxShiftValueExecutor(Vector &tbox, Vector &shift, LogicalType type, Vector &result, idx_t count);
    static void Tbox_shift_value(DataChunk &args, ExpressionState &state, Vector &result);
    
    static void Tbox_shift_time(DataChunk &args, ExpressionState &state, Vector &result);

    template <typename TB>
    static void TboxScaleValueExecutor(Vector &tbox, Vector &scale, LogicalType type, Vector &result, idx_t count);
    static void Tbox_scale_value(DataChunk &args, ExpressionState &state, Vector &result);

    static void Tbox_scale_time(DataChunk &args, ExpressionState &state, Vector &result);

    template <typename TB>
    static void TboxShiftScaleValueExecutor(Vector &tbox, Vector &shift, Vector &scale, LogicalType type, Vector &result, idx_t count);
    static void Tbox_shift_scale_value(DataChunk &args, ExpressionState &state, Vector &result);

    static void Tbox_shift_scale_time(DataChunk &args, ExpressionState &state, Vector &result);

    template <typename TB>
    static void TboxExpandValueExecutor(Vector &tbox, Vector &value, meosType basetype, Vector &result, idx_t count);
    static void Tbox_expand_value(DataChunk &args, ExpressionState &state, Vector &result);

    static void Tbox_expand_time(DataChunk &args, ExpressionState &state, Vector &result);
};

} // namespace duckdb