#include "meos_wrapper_simple.hpp"

#include "common.hpp"
#include "temporal/span.hpp"
#include "temporal/set.hpp"
#include "temporal/spanset.hpp"
#include "temporal/tbox.hpp"
#include "geo/stbox.hpp"
#include "temporal/temporal.hpp"
#include "temporal/span_aggregates.hpp"
#include "time_util.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/function/aggregate_function.hpp"
#include "duckdb/function/function_set.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

namespace {

// =====================================================================
// Span-typed extent: state = Span. Inputs span/set/spanset/scalars all
// fold into the same shape, only the transition function differs.
// =====================================================================

struct SpanExtentState {
    Span span;
    bool isset;
};

template <auto TRANSFN>
struct SpanExtentFromSpanLike {
    template <class STATE>
    static void Initialize(STATE &state) { state.isset = false; }
    static bool IgnoreNull() { return true; }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        // INPUT_TYPE is string_t — span / set / spanset blob.
        const auto *in = reinterpret_cast<const std::remove_pointer_t<
            decltype(TRANSFN(static_cast<Span *>(nullptr), nullptr))> *>(input.GetData());
        if (!state.isset) {
            // Use a temporary state of NULL: TRANSFN palloc-copies into a fresh
            // Span. Copy it back inline and free.
            Span *fresh = TRANSFN(nullptr, in);
            if (fresh) {
                memcpy(&state.span, fresh, sizeof(Span));
                free(fresh);
                state.isset = true;
            }
        } else {
            (void) TRANSFN(&state.span, in);
        }
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &u, idx_t) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, u);
    }
    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        if (!source.isset) return;
        if (!target.isset) {
            memcpy(&target.span, &source.span, sizeof(Span));
            target.isset = true;
        } else {
            (void) span_extent_transfn(&target.span, &source.span);
        }
    }
    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &fd) {
        if (!state.isset) { fd.ReturnNull(); return; }
        target = fd.ReturnString(string_t(reinterpret_cast<const char *>(&state.span), sizeof(Span)));
    }
};

// Original span extent retained — span_extent_transfn expands in place even
// for the NULL-state case, so we keep the dedicated implementation for it
// to avoid double-allocating.
struct SpanExtentFunction {
    template <class STATE>
    static void Initialize(STATE &state) { state.isset = false; }
    static bool IgnoreNull() { return true; }
    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        const Span *input_span = reinterpret_cast<const Span *>(input.GetData());
        if (!state.isset) {
            memcpy(&state.span, input_span, sizeof(Span));
            state.isset = true;
        } else {
            (void) span_extent_transfn(&state.span, input_span);
        }
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &u, idx_t) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, u);
    }
    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        if (!source.isset) return;
        if (!target.isset) {
            memcpy(&target.span, &source.span, sizeof(Span));
            target.isset = true;
        } else {
            (void) span_extent_transfn(&target.span, &source.span);
        }
    }
    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &fd) {
        if (!state.isset) { fd.ReturnNull(); return; }
        target = fd.ReturnString(string_t(reinterpret_cast<const char *>(&state.span), sizeof(Span)));
    }
};

// =====================================================================
// Scalar inputs (int / bigint / float / date / timestamptz). For each
// the MEOS transfn signature is (Span *, value) -> Span *.
// =====================================================================

template <class IN, Span *(*TRANSFN)(Span *, IN)>
struct ScalarSpanExtentFn {
    template <class STATE>
    static void Initialize(STATE &state) { state.isset = false; }
    static bool IgnoreNull() { return true; }
    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        Span *fresh = TRANSFN(state.isset ? &state.span : nullptr, input);
        if (!state.isset && fresh) {
            memcpy(&state.span, fresh, sizeof(Span));
            free(fresh);
            state.isset = true;
        }
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &u, idx_t) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, u);
    }
    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        if (!source.isset) return;
        if (!target.isset) {
            memcpy(&target.span, &source.span, sizeof(Span));
            target.isset = true;
        } else {
            (void) span_extent_transfn(&target.span, &source.span);
        }
    }
    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &fd) {
        if (!state.isset) { fd.ReturnNull(); return; }
        target = fd.ReturnString(string_t(reinterpret_cast<const char *>(&state.span), sizeof(Span)));
    }
};

// Wrappers to convert DuckDB-side timestamp/date to MEOS-side before calling
// the transition function, so the aggregate sees consistent inputs.
inline Span *DateExtentDuckDB(Span *state, date_t d) {
    return date_extent_transfn(state, ToMeosDate(d));
}
inline Span *TstzExtentDuckDB(Span *state, timestamp_tz_t t) {
    return timestamptz_extent_transfn(state, (TimestampTz) DuckDBToMeosTimestamp(t).value);
}
inline Span *Int32Extent(Span *state, int32_t v) { return int_extent_transfn(state, v); }
inline Span *Int64Extent(Span *state, int64_t v) { return bigint_extent_transfn(state, v); }
inline Span *DoubleExtent(Span *state, double v) { return float_extent_transfn(state, v); }

// =====================================================================
// TBox extent: state = TBox. Both numeric tbox-shaped inputs (tbox itself,
// tnumber via tnumber_extent_transfn) feed in here.
// =====================================================================

struct TBoxExtentState {
    TBox box;
    bool isset;
};

// Generic TBox-state aggregator, parametrised by the function that takes
// (state, inputBlob) and returns a fresh-or-state TBox*. Both
// `tnumber_extent_transfn(TBox*, const Temporal*)` and a "tbox extent"
// (built on `tbox_expand`) match this signature once we wrap them.
typedef TBox *(*TBoxTransFn)(TBox * /*state*/, const void * /*input ptr*/);

template <TBoxTransFn TRANSFN>
struct TBoxExtentFn {
    template <class STATE>
    static void Initialize(STATE &state) { state.isset = false; }
    static bool IgnoreNull() { return true; }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        TBox *result = TRANSFN(state.isset ? &state.box : nullptr, input.GetData());
        if (!state.isset && result) {
            memcpy(&state.box, result, sizeof(TBox));
            free(result);
            state.isset = true;
        }
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &u, idx_t) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, u);
    }
    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        if (!source.isset) return;
        if (!target.isset) {
            memcpy(&target.box, &source.box, sizeof(TBox));
            target.isset = true;
        } else {
            tbox_expand(&source.box, &target.box);
        }
    }
    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &fd) {
        if (!state.isset) { fd.ReturnNull(); return; }
        target = fd.ReturnString(string_t(reinterpret_cast<const char *>(&state.box), sizeof(TBox)));
    }
};

// Wrap MEOS extent-style functions to a uniform signature.
TBox *TboxExtentFromTBox(TBox *state, const void *input) {
    const TBox *box_in = reinterpret_cast<const TBox *>(input);
    if (!state) {
        TBox *fresh = (TBox *) malloc(sizeof(TBox));
        memcpy(fresh, box_in, sizeof(TBox));
        return fresh;
    }
    tbox_expand(box_in, state);
    return state;
}

TBox *TboxExtentFromTNumber(TBox *state, const void *input) {
    const Temporal *t = reinterpret_cast<const Temporal *>(input);
    return tnumber_extent_transfn(state, t);
}

// =====================================================================
// STBox extent: state = STBox.
// =====================================================================

struct STBoxExtentState {
    STBox box;
    bool isset;
};

typedef STBox *(*STBoxTransFn)(STBox * /*state*/, const void * /*input ptr*/);

template <STBoxTransFn TRANSFN>
struct STBoxExtentFn {
    template <class STATE>
    static void Initialize(STATE &state) { state.isset = false; }
    static bool IgnoreNull() { return true; }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        STBox *result = TRANSFN(state.isset ? &state.box : nullptr, input.GetData());
        if (!state.isset && result) {
            memcpy(&state.box, result, sizeof(STBox));
            free(result);
            state.isset = true;
        }
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &u, idx_t) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, u);
    }
    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        if (!source.isset) return;
        if (!target.isset) {
            memcpy(&target.box, &source.box, sizeof(STBox));
            target.isset = true;
        } else {
            stbox_expand(&source.box, &target.box);
        }
    }
    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &fd) {
        if (!state.isset) { fd.ReturnNull(); return; }
        target = fd.ReturnString(string_t(reinterpret_cast<const char *>(&state.box), sizeof(STBox)));
    }
};

STBox *StboxExtentFromSTBox(STBox *state, const void *input) {
    const STBox *box_in = reinterpret_cast<const STBox *>(input);
    if (!state) {
        STBox *fresh = (STBox *) malloc(sizeof(STBox));
        memcpy(fresh, box_in, sizeof(STBox));
        return fresh;
    }
    stbox_expand(box_in, state);
    return state;
}

STBox *StboxExtentFromTSpatial(STBox *state, const void *input) {
    const Temporal *t = reinterpret_cast<const Temporal *>(input);
    return tspatial_extent_transfn(state, t);
}

// =====================================================================
// Span-typed extent over Temporal: temporal_extent_transfn yields the
// time span (tstzspan) for tbool/ttext etc.
// =====================================================================

Span *TstzSpanExtentFromTemporal(Span *state, const void *input) {
    const Temporal *t = reinterpret_cast<const Temporal *>(input);
    return temporal_extent_transfn(state, t);
}

struct GenericSpanExtentState : SpanExtentState {};

template <Span *(*TRANSFN)(Span *, const void *)>
struct GenericSpanFromBlobFn {
    template <class STATE>
    static void Initialize(STATE &state) { state.isset = false; }
    static bool IgnoreNull() { return true; }

    template <class INPUT_TYPE, class STATE, class OP>
    static void Operation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &) {
        Span *fresh = TRANSFN(state.isset ? &state.span : nullptr, input.GetData());
        if (!state.isset && fresh) {
            memcpy(&state.span, fresh, sizeof(Span));
            free(fresh);
            state.isset = true;
        }
    }
    template <class INPUT_TYPE, class STATE, class OP>
    static void ConstantOperation(STATE &state, const INPUT_TYPE &input, AggregateUnaryInput &u, idx_t) {
        Operation<INPUT_TYPE, STATE, OP>(state, input, u);
    }
    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        if (!source.isset) return;
        if (!target.isset) {
            memcpy(&target.span, &source.span, sizeof(Span));
            target.isset = true;
        } else {
            (void) span_extent_transfn(&target.span, &source.span);
        }
    }
    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &fd) {
        if (!state.isset) { fd.ReturnNull(); return; }
        target = fd.ReturnString(string_t(reinterpret_cast<const char *>(&state.span), sizeof(Span)));
    }
};

Span *SpanExtentFromSet(Span *state, const void *input) {
    const Set *s = reinterpret_cast<const Set *>(input);
    return set_extent_transfn(state, s);
}
Span *SpanExtentFromSpanSet(Span *state, const void *input) {
    const SpanSet *ss = reinterpret_cast<const SpanSet *>(input);
    return spanset_extent_transfn(state, ss);
}

// =====================================================================
// Aggregate-construction helpers
// =====================================================================

static AggregateFunction MakeExtentSpanAggregate(const LogicalType &span_type) {
    return AggregateFunction::UnaryAggregate<SpanExtentState, string_t, string_t, SpanExtentFunction>(
        span_type, span_type);
}

template <class T, Span *(*TRANSFN)(Span *, T)>
static AggregateFunction MakeExtentScalarAggregate(const LogicalType &input_type, const LogicalType &out_span) {
    return AggregateFunction::UnaryAggregate<SpanExtentState, T, string_t, ScalarSpanExtentFn<T, TRANSFN>>(
        input_type, out_span);
}

template <Span *(*TRANSFN)(Span *, const void *)>
static AggregateFunction MakeExtentBlobToSpanAggregate(const LogicalType &input_type,
                                                       const LogicalType &out_span) {
    return AggregateFunction::UnaryAggregate<SpanExtentState, string_t, string_t,
                                             GenericSpanFromBlobFn<TRANSFN>>(input_type, out_span);
}

template <TBoxTransFn TRANSFN>
static AggregateFunction MakeExtentTboxAggregate(const LogicalType &input_type) {
    return AggregateFunction::UnaryAggregate<TBoxExtentState, string_t, string_t, TBoxExtentFn<TRANSFN>>(
        input_type, TboxType::TBOX());
}

template <STBoxTransFn TRANSFN>
static AggregateFunction MakeExtentStboxAggregate(const LogicalType &input_type) {
    return AggregateFunction::UnaryAggregate<STBoxExtentState, string_t, string_t, STBoxExtentFn<TRANSFN>>(
        input_type, StboxType::STBOX());
}

} // namespace

void SpanAggregates::RegisterAggregateFunctions(ExtensionLoader &loader) {
    AggregateFunctionSet extent_set("extent");

    // ---- extent(span) for all 5 span types ----
    for (const auto &span_type : {SpanTypes::INTSPAN(), SpanTypes::BIGINTSPAN(),
                                  SpanTypes::FLOATSPAN(), SpanTypes::DATESPAN(),
                                  SpanTypes::TSTZSPAN()}) {
        extent_set.AddFunction(MakeExtentSpanAggregate(span_type));
    }

    // ---- extent(scalar) -> typed span ----
    extent_set.AddFunction(MakeExtentScalarAggregate<int32_t, Int32Extent>(
        LogicalType::INTEGER, SpanTypes::INTSPAN()));
    extent_set.AddFunction(MakeExtentScalarAggregate<int64_t, Int64Extent>(
        LogicalType::BIGINT, SpanTypes::BIGINTSPAN()));
    extent_set.AddFunction(MakeExtentScalarAggregate<double, DoubleExtent>(
        LogicalType::DOUBLE, SpanTypes::FLOATSPAN()));
    extent_set.AddFunction(MakeExtentScalarAggregate<date_t, DateExtentDuckDB>(
        LogicalType::DATE, SpanTypes::DATESPAN()));
    extent_set.AddFunction(MakeExtentScalarAggregate<timestamp_tz_t, TstzExtentDuckDB>(
        LogicalType::TIMESTAMP_TZ, SpanTypes::TSTZSPAN()));

    // ---- extent(set) -> typed span ----
    struct SetSpanPair {
        LogicalType in;
        LogicalType out;
    };
    const std::vector<SetSpanPair> set_pairs = {
        {SetTypes::intset(),     SpanTypes::INTSPAN()},
        {SetTypes::bigintset(),  SpanTypes::BIGINTSPAN()},
        {SetTypes::floatset(),   SpanTypes::FLOATSPAN()},
        {SetTypes::dateset(),    SpanTypes::DATESPAN()},
        {SetTypes::tstzset(),    SpanTypes::TSTZSPAN()},
    };
    for (const auto &p : set_pairs) {
        extent_set.AddFunction(MakeExtentBlobToSpanAggregate<SpanExtentFromSet>(p.in, p.out));
    }

    // ---- extent(spanset) -> typed span ----
    const std::vector<SetSpanPair> spanset_pairs = {
        {SpansetTypes::intspanset(),    SpanTypes::INTSPAN()},
        {SpansetTypes::bigintspanset(), SpanTypes::BIGINTSPAN()},
        {SpansetTypes::floatspanset(),  SpanTypes::FLOATSPAN()},
        {SpansetTypes::datespanset(),   SpanTypes::DATESPAN()},
        {SpansetTypes::tstzspanset(),   SpanTypes::TSTZSPAN()},
    };
    for (const auto &p : spanset_pairs) {
        extent_set.AddFunction(MakeExtentBlobToSpanAggregate<SpanExtentFromSpanSet>(p.in, p.out));
    }

    // ---- extent(tbox) -> tbox; extent(tnumber) -> tbox ----
    extent_set.AddFunction(MakeExtentTboxAggregate<TboxExtentFromTBox>(TboxType::TBOX()));
    extent_set.AddFunction(MakeExtentTboxAggregate<TboxExtentFromTNumber>(TemporalTypes::TINT()));
    extent_set.AddFunction(MakeExtentTboxAggregate<TboxExtentFromTNumber>(TemporalTypes::TFLOAT()));

    // ---- extent(stbox | tgeompoint | tgeometry) -> stbox ----
    extent_set.AddFunction(MakeExtentStboxAggregate<StboxExtentFromSTBox>(StboxType::STBOX()));
    {
        // tgeompoint / tgeometry are registered via their respective Type
        // helpers; we avoid pulling those headers here by using a
        // BLOB-aliased clone since the underlying MEOS extent transition
        // function (`tspatial_extent_transfn`) is subtype-agnostic.
        LogicalType tgeompoint_type(LogicalTypeId::BLOB);
        tgeompoint_type.SetAlias("TGEOMPOINT");
        extent_set.AddFunction(
            MakeExtentStboxAggregate<StboxExtentFromTSpatial>(tgeompoint_type));

        LogicalType tgeometry_type(LogicalTypeId::BLOB);
        tgeometry_type.SetAlias("TGEOMETRY");
        extent_set.AddFunction(
            MakeExtentStboxAggregate<StboxExtentFromTSpatial>(tgeometry_type));

        LogicalType tgeography_type(LogicalTypeId::BLOB);
        tgeography_type.SetAlias("TGEOGRAPHY");
        extent_set.AddFunction(
            MakeExtentStboxAggregate<StboxExtentFromTSpatial>(tgeography_type));

        LogicalType tgeogpoint_type(LogicalTypeId::BLOB);
        tgeogpoint_type.SetAlias("TGEOGPOINT");
        extent_set.AddFunction(
            MakeExtentStboxAggregate<StboxExtentFromTSpatial>(tgeogpoint_type));
    }

    // ---- extent(tbool/ttext) -> tstzspan ----
    extent_set.AddFunction(MakeExtentBlobToSpanAggregate<TstzSpanExtentFromTemporal>(
        TemporalTypes::TBOOL(), SpanTypes::TSTZSPAN()));
    extent_set.AddFunction(MakeExtentBlobToSpanAggregate<TstzSpanExtentFromTemporal>(
        TemporalTypes::TTEXT(), SpanTypes::TSTZSPAN()));

    loader.RegisterFunction(std::move(extent_set));
}

} // namespace duckdb
