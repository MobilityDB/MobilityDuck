#include "meos_wrapper_simple.hpp"

#include "common.hpp"
#include "temporal/temporal.hpp"
#include "temporal/temporal_functions.hpp"
#include "temporal/spanset.hpp"
#include "temporal/tbox.hpp"
#include "temporal/set.hpp"
#include "temporal/span.hpp"
#include "geo/stbox.hpp"
#include "geo/tgeompoint.hpp"
#include "geo/tgeometry.hpp"

#include "duckdb/common/types/blob.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/common/types/data_chunk.hpp"

#include "time_util.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

namespace duckdb {

#define DEFINE_TEMPORAL_TYPE(NAME) \
    LogicalType TemporalTypes::NAME() { \
        LogicalType type(LogicalTypeId::BLOB); \
        type.SetAlias(#NAME); \
        return type; \
    }

DEFINE_TEMPORAL_TYPE(tint)
DEFINE_TEMPORAL_TYPE(tbigint)
DEFINE_TEMPORAL_TYPE(tbool)
DEFINE_TEMPORAL_TYPE(tfloat)
DEFINE_TEMPORAL_TYPE(ttext)

#undef DEFINE_TEMPORAL_TYPE

void TemporalTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType( "tint", tint());
    loader.RegisterType( "tbigint", tbigint());
    loader.RegisterType( "tbool", tbool());
    loader.RegisterType( "tfloat", tfloat());
    loader.RegisterType( "ttext", ttext());
}

const std::vector<LogicalType> &TemporalTypes::AllTypes() {
    static std::vector<LogicalType> types = {
        tint(),
        tbigint(),
        tbool(),
        tfloat(),
        ttext()
    };
    return types;
}

LogicalType TemporalTypes::GetBaseTypeFromAlias(const char *alias) {
    for (size_t i = 0; i < sizeof(BASE_TYPES) / sizeof(BASE_TYPES[0]); i++) {
        if (strcmp(alias, BASE_TYPES[i].alias) == 0) {
            return BASE_TYPES[i].basetype;
        }
    }
    throw InternalException("Invalid temporal type alias: %s", alias);
}

void TemporalTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    for (auto &type : TemporalTypes::AllTypes()) {
        RegisterMeosCastFunction(loader, 
            LogicalType::VARCHAR,
            type,
            TemporalFunctions::Temporal_in
        );

        RegisterMeosCastFunction(loader, 
            type,
            LogicalType::VARCHAR,
            TemporalFunctions::Temporal_out
        );

    RegisterMeosCastFunction(loader, 
        LogicalType::BLOB,
        SpansetTypes::tstzspanset(),
        TemporalFunctions::Blob_to_tstzspanset
    );

    RegisterMeosCastFunction(loader, 
        TemporalTypes::tbool(),
        TemporalTypes::tint(),
        TemporalFunctions::Tbool_to_tint_cast
    );

    RegisterMeosCastFunction(loader, 
        TemporalTypes::tint(),
        TemporalTypes::tfloat(),
        TemporalFunctions::Tint_to_tfloat_cast
    );

    RegisterMeosCastFunction(loader, 
        TemporalTypes::tfloat(),
        TemporalTypes::tint(),
        TemporalFunctions::Tfloat_to_tint_cast
    );

    RegisterMeosCastFunction(loader, 
        TemporalTypes::tint(),
        TboxType::tbox(),
        TemporalFunctions::Tnumber_to_tbox_cast
    );

    RegisterMeosCastFunction(loader, 
        TemporalTypes::tfloat(),
        TboxType::tbox(),
        TemporalFunctions::Tnumber_to_tbox_cast
    );

}
}

void TemporalTypes::RegisterScalarFunctions(ExtensionLoader &loader) {
    for (auto &type : TemporalTypes::AllTypes()) {
        // The constructors from a base value and a time value, tint(integer, timestamptz) ..
        // ttext(text, tstzspanset), are generated (meos_temporal_constructor, RETIRED_GROUPS).
        duckdb::RegisterSerializedScalarFunction(loader, 
            ScalarFunction(
                StringUtil::Lower(type.GetAlias()),
                {type, LogicalType::INTEGER},
                type,
                TemporalFunctions::Temporal_enforce_typmod
            )
        );

        // timeSpan/valueSpan (temporal_to_tstzspan / tnumber_to_span, group
        // meos_temporal_conversion) are generated from the catalog sqlSignatures
        // in src/generated/generated_temporal_udfs.cpp (RETIRED_GROUPS).

        // timestamps(<temporal_type>) is generated from the catalog (temporal_timestamps) in generated_temporal_udfs.cpp.

        if (type.GetAlias() != "tbool") {
            duckdb::RegisterSerializedScalarFunction(loader, 
                ScalarFunction(
                    "tempDump",
                    {type},
                    LogicalType::LIST(
                        LogicalType::STRUCT(
                            {{"value", TemporalTypes::GetBaseTypeFromAlias(type.GetAlias().c_str())},
                            {"time", SpansetTypes::tstzspanset()}}
                        )
                    ),
                    TemporalFunctions::Temporal_dump
                )
            );
        }
        

        




    }

    // The temporal accessors (getValue/startValue/endValue/minValue/maxValue, getValues,
    // valueSet, getTime, instants, segmentMinDuration...; group meos_temporal_accessor) are
    // generated from the catalog sqlSignatures in src/generated/generated_temporal_udfs.cpp
    // (RETIRED_GROUPS).

    // atValues / minusValues (Temporal<T>, set-of-T) are now generated from the catalog
    // (temporal_at_values / temporal_minus_values, sqlSignatures-driven) — see
    // src/generated/generated_temporal_udfs.cpp. Behaviour-equivalent to the retired hand
    // regs (both call the MEOS set restriction), and the generated surface adds the
    // previously-missing tbigint overload.

    // whenTrue(tbool) is generated from the catalog (tbool_when_true, meos_temporal_bool).

    // atTbox / minusTbox (TNumber<T>, tbox) are now generated from the catalog
    // (tnumber_at_tbox / tnumber_minus_tbox, sqlSignatures-driven) — see
    // src/generated/generated_temporal_udfs.cpp. Behaviour-equivalent to the retired hand
    // regs (both call the MEOS box restriction), and the generated surface adds the
    // previously-missing tbigint overload.

    // round(tfloat) and round(tfloat, integer) are float-base scalar transforms
    // generated from the catalog (generated_temporal_udfs.cpp): the shorter
    // DEFAULT-arg overload comes from sqlSignatures argDefaults.

    // The temporal-number casts (tint/tfloat) and tbox() conversion function forms
    // (tbool_to_tint / tint_to_tfloat / tfloat_to_tint / tnumber_to_tbox, group
    // meos_temporal_conversion) are generated from the catalog sqlSignatures in
    // src/generated/generated_temporal_udfs.cpp (RETIRED_GROUPS) — incl the tbigint
    // overloads the hand layer was missing.

    // avgValue(tint | tbigint | tfloat) is generated from the catalog (Gen_tnumber_avg_value).

    // The boolean, arithmetic, text and distance operators over the temporal types are
    // generated from the catalog (src/generated).

    // Unary tfloat transforms floor/ceil/round/degrees/radians are float-base scalar
    // transforms generated from the catalog (generated_temporal_udfs.cpp), full arity
    // plus the shorter DEFAULT-arg overload via sqlSignatures argDefaults.
    // The meos_temporal_math group (abs/derivative/exp/ln/log10/deltaValue/trend) is
    // supplied by the generated surface (RETIRED_GROUPS in the codegen).


    // nearestApproachDistance / nad — scalar return

    // Temporal topological predicates (contains/contained/overlaps/same/adjacent +
    // @>/<@/&&/~=/-|-) for temporal × temporal (same base family) and temporal ×
    // tstzspan (both directions) are generated from the catalog sqlSignatures in
    // src/generated/generated_temporal_udfs.cpp (@ingroup meos_temporal_bbox_topo).
    // The non-canonical temporal_* snake aliases and the spurious mixed-family
    // cross-product overloads are dropped (bare same-family names supersede them).

    // Temporal time-position predicates (before/after/overbefore/overafter for
    // temporal×temporal, temporal×tstzspan both directions, and tgeompoint;
    // @ingroup meos_temporal_bbox_pos) are generated from the catalog sqlSignatures
    // in src/generated/generated_temporal_udfs.cpp. DuckDB's parser rejects `#` in
    // operator names, so only the named forms are reachable — the generator emits
    // exactly those, same-base-family per the catalog (no spurious mixed pairs).
    // The stale-snake temporal_* aliases are dropped (bare names supersede them).

    // Ever / always equality and inequality (named functions; DuckDB
    // parser does not accept ?= / #= operator names).
    // Traditional/ever/always temporal comparisons (meos_temporal_comp_ever) are
    // supplied by the generated surface (RETIRED_GROUPS). The spatial (geometry-arg)
    // ever/always overloads remain hand-registered in the geo module.

    // Similarity: frechetDistance/dynTimeWarpDistance/hausdorffDistance (scalar) and
    // frechetDistancePath/dynTimeWarpPath (SETOF -> DuckDB table function) are all GENERATED
    // from the catalog now. The non-canonical discreteFrechet/dynTimeWarp hand aliases were
    // retired (MobilityDB canonical = frechetDistance/dynTimeWarpDistance).

    // tnumber × {numspan, tbox} topological predicates (contains/contained/overlaps/
    // same/adjacent + @>/<@/&&/~=/-|-, both directions) are generated from the catalog
    // sqlSignatures (@ingroup meos_temporal_bbox_topo); the temporal_* snake aliases
    // are dropped. The numeric-axis positional predicates are likewise generated.

    // Temporal #= comparison (temporal_teq/tne/tlt/tle/tgt/tge) is generated
    // (group meos_temporal_comp_temp).
    // tsample over every temporal type is generated from the catalog (temporal_tsample,
    // meos_temporal_analytics_reduction).

    // tspatial × {stbox, tspatial} position predicates (spatial axes
    // left/right/below/above/front/back + over*, and the time axis before/after/
    // overbefore/overafter; @ingroup meos_geo_box_pos / meos_geo_bbox_pos) — bare
    // names AND the <<, >>, &<, &> operator forms — are generated from the catalog
    // sqlSignatures in src/generated/generated_temporal_udfs.cpp. The stale-snake
    // temporal_* aliases are dropped (bare names supersede them).

    // ttext text functions
    // Portable-SQL alias (MobilityDB name): ttext_cat
}

// ============================================================
// timeSplit / valueSplit / valueTimeSplit  — table-function impls
// ============================================================

namespace {

// ---------- timeSplit / valueSplit / valueTimeSplit ----------
//
// Each takes its temporal value from a literal or from a LATERAL column
// through DuckDB's in_out_function, as spaceSplit and spaceTimeSplit do
// (src/geo/tgeompoint.cpp). The bind declares the result columns from the
// function_info of the registration, which states the temporal type, the bins
// the split produces and whether its value bins are integers; each input row
// is split when the output reaches it, and its bins are emitted at most
// STANDARD_VECTOR_SIZE at a time.

struct TemporalSplitInfo : public TableFunctionInfo {
    TemporalSplitInfo(LogicalType ttype_p, bool with_value_p, bool is_int_p, bool with_time_p)
        : ttype(std::move(ttype_p)), with_value(with_value_p), is_int(is_int_p),
          with_time(with_time_p) {
    }
    LogicalType ttype;  // the temporal type of the input and of each piece
    bool with_value;    // valueSplit / valueTimeSplit: a value bin
    bool is_int;        // integer value bin size and origin, else double
    bool with_time;     // timeSplit / valueTimeSplit: a time bin
};

struct TemporalSplitBindData : public TableFunctionData {
    TemporalSplitBindData(LogicalType ttype_p, bool with_value_p, bool is_int_p, bool with_time_p)
        : ttype(std::move(ttype_p)), with_value(with_value_p), is_int(is_int_p),
          with_time(with_time_p) {
    }
    LogicalType ttype;
    bool with_value;
    bool is_int;
    bool with_time;
};

struct TemporalSplitLocalState : public LocalTableFunctionState {
    idx_t current_input_row = 0;
    bool initialized_row = false;
    idx_t out_idx = 0;
    vector<Value> value_bins;  // valueSplit / valueTimeSplit
    vector<Value> time_bins;   // timeSplit / valueTimeSplit
    vector<Value> temporals;

    void Reset() {
        value_bins.clear();
        time_bins.clear();
        temporals.clear();
        out_idx = 0;
    }
};

unique_ptr<FunctionData> TemporalSplitBind(ClientContext &, TableFunctionBindInput &input,
                                           vector<LogicalType> &return_types, vector<string> &names) {
    auto &info = input.info->Cast<TemporalSplitInfo>();
    if (!info.with_value) {
        return_types = {LogicalType::TIMESTAMP_TZ, info.ttype};
        names = {"time", StringUtil::Lower(info.ttype.GetAlias())};
    } else if (info.with_time) {
        return_types = {info.is_int ? LogicalType::INTEGER : LogicalType::DOUBLE,
                        LogicalType::TIMESTAMP_TZ, info.ttype};
        names = {"value", "time", StringUtil::Lower(info.ttype.GetAlias())};
    } else {
        return_types = {info.is_int ? LogicalType::BIGINT : LogicalType::DOUBLE, info.ttype};
        names = {"number", "tnumber"};
    }
    return make_uniq<TemporalSplitBindData>(info.ttype, info.with_value, info.is_int, info.with_time);
}

unique_ptr<LocalTableFunctionState> TemporalSplitLocalInit(ExecutionContext &context, TableFunctionInitInput &,
                                                           GlobalTableFunctionState *) {
    EnsureMeosThreadInitialized(context.client);
    return make_uniq<TemporalSplitLocalState>();
}

/* Split one input row into the local state.  Input column layout:
 *   timeSplit:      [0]=temporal, [1]=duration, [2]=origin (optional), [3]=borderInc (optional)
 *   valueSplit:     [0]=tnumber, [1]=size, [2]=origin (optional), [3]=borderInc (optional)
 *   valueTimeSplit: [0]=tnumber, [1]=vsize, [2]=duration,
 *                   [3]=vorigin and [4]=torigin (optional, together), [5]=borderInc (optional)
 * A NULL temporal, size, duration or borderInc splits into no row; an absent borderInc is true. */
void LoadTemporalSplitRow(TemporalSplitLocalState &state, const TemporalSplitBindData &bd,
                          DataChunk &input, idx_t row) {
    state.Reset();
    for (idx_t c = 0; c < input.ColumnCount(); c++) {
        input.data[c].Flatten(input.size());
    }
    if (FlatVector::IsNull(input.data[0], row) || FlatVector::IsNull(input.data[1], row)) {
        return;
    }
    if (bd.with_value && bd.with_time && FlatVector::IsNull(input.data[2], row)) {
        return;
    }
    const idx_t duration_col = bd.with_value ? 2 : 1;
    const idx_t vorigin_col = bd.with_time ? 3 : 2;
    const idx_t torigin_col = bd.with_value ? 4 : 2;
    const bool has_vorigin = bd.with_value && input.ColumnCount() > vorigin_col &&
                             !FlatVector::IsNull(input.data[vorigin_col], row);
    const idx_t border_col = (bd.with_value && bd.with_time) ? 5 : 3;
    bool border_inc = true;
    if (input.ColumnCount() > border_col) {
        if (FlatVector::IsNull(input.data[border_col], row)) {
            return;
        }
        border_inc = FlatVector::GetData<bool>(input.data[border_col])[row];
    }
    MeosInterval mi {};
    TimestampTz torigin = DEFAULT_TIME_ORIGIN_MEOS;
    if (bd.with_time) {
        mi = IntervaltToInterval(FlatVector::GetData<interval_t>(input.data[duration_col])[row]);
        if (input.ColumnCount() > torigin_col && !FlatVector::IsNull(input.data[torigin_col], row)) {
            timestamp_tz_t t = FlatVector::GetData<timestamp_tz_t>(input.data[torigin_col])[row];
            torigin = static_cast<TimestampTz>(DuckDBToMeosTimestamp(t).value);
        }
    }

    string_t blob = FlatVector::GetData<string_t>(input.data[0])[row];
    Temporal *temp = static_cast<Temporal *>(malloc(blob.GetSize()));
    memcpy(temp, blob.GetData(), blob.GetSize());

    int count = 0;
    Temporal **parts = nullptr;
    TimestampTz *tbins = nullptr;
    int *ibins = nullptr;
    double *dbins = nullptr;
    if (!bd.with_value) {
        parts = temporal_time_split(temp, &mi, torigin, border_inc, &tbins, &count);
    } else if (bd.is_int) {
        int vsize = FlatVector::GetData<int32_t>(input.data[1])[row];
        int vorigin = has_vorigin ? FlatVector::GetData<int32_t>(input.data[vorigin_col])[row] : 0;
        parts = bd.with_time
            ? tint_value_time_split(temp, vsize, &mi, vorigin, torigin, border_inc, &ibins, &tbins, &count)
            : tint_value_split(temp, vsize, vorigin, border_inc, &ibins, &count);
    } else {
        double vsize = FlatVector::GetData<double>(input.data[1])[row];
        double vorigin = has_vorigin ? FlatVector::GetData<double>(input.data[vorigin_col])[row] : 0.0;
        parts = bd.with_time
            ? tfloat_value_time_split(temp, vsize, &mi, vorigin, torigin, border_inc, &dbins, &tbins, &count)
            : tfloat_value_split(temp, vsize, vorigin, border_inc, &dbins, &count);
    }
    free(temp);

    if (parts && count > 0) {
        state.temporals.reserve(count);
        if (bd.with_value) {
            state.value_bins.reserve(count);
        }
        if (bd.with_time) {
            state.time_bins.reserve(count);
        }
        for (int i = 0; i < count; i++) {
            if (bd.with_value && bd.is_int) {
                state.value_bins.push_back(bd.with_time ? Value::INTEGER(ibins[i]) : Value::BIGINT(ibins[i]));
            } else if (bd.with_value) {
                state.value_bins.push_back(Value::DOUBLE(dbins[i]));
            }
            if (bd.with_time) {
                timestamp_tz_t ts = MeosToDuckDBTimestamp(timestamp_tz_t(static_cast<int64_t>(tbins[i])));
                state.time_bins.push_back(Value::TIMESTAMPTZ(ts));
            }
            Value tblob = Value::BLOB(const_data_ptr_cast(parts[i]), temporal_mem_size(parts[i]));
            tblob.Reinterpret(bd.ttype);
            state.temporals.push_back(std::move(tblob));
            free(parts[i]);
        }
    }
    free(parts);
    free(ibins);
    free(dbins);
    free(tbins);
}

OperatorResultType TemporalSplitInOut(ExecutionContext &, TableFunctionInput &data_p, DataChunk &input,
                                      DataChunk &output) {
    auto &bd = data_p.bind_data->Cast<TemporalSplitBindData>();
    auto &state = data_p.local_state->Cast<TemporalSplitLocalState>();
    idx_t out_row = 0;

    /* As in SpaceSplitInOutCommon: on a drained input chunk the call returns
     * the rows it emitted as HAVE_MORE_OUTPUT, or NEED_MORE_INPUT when it
     * emitted none, so the source-scan path stops and the LATERAL path
     * advances to the next chunk. */
    while (out_row < STANDARD_VECTOR_SIZE) {
        if (!state.initialized_row) {
            if (state.current_input_row >= input.size()) {
                if (out_row > 0) {
                    output.SetCardinality(out_row);
                    return OperatorResultType::HAVE_MORE_OUTPUT;
                }
                state.current_input_row = 0;
                return OperatorResultType::NEED_MORE_INPUT;
            }
            LoadTemporalSplitRow(state, bd, input, state.current_input_row);
            state.initialized_row = true;
        }
        if (state.out_idx >= state.temporals.size()) {
            state.current_input_row++;
            state.initialized_row = false;
            continue;
        }
        idx_t col = 0;
        if (bd.with_value) {
            output.data[col++].SetValue(out_row, state.value_bins[state.out_idx]);
        }
        if (bd.with_time) {
            output.data[col++].SetValue(out_row, state.time_bins[state.out_idx]);
        }
        output.data[col].SetValue(out_row, state.temporals[state.out_idx]);
        state.out_idx++;
        out_row++;
    }
    output.SetCardinality(out_row);
    return OperatorResultType::HAVE_MORE_OUTPUT;
}

void RegisterTemporalSplit(ExtensionLoader &loader, const string &name, vector<LogicalType> args,
                           LogicalType ttype, bool with_value, bool is_int, bool with_time) {
    TableFunction fn(name, std::move(args), /*function=*/nullptr, TemporalSplitBind,
                     /*init_global=*/nullptr, TemporalSplitLocalInit);
    fn.in_out_function = TemporalSplitInOut;
    fn.function_info = make_shared_ptr<TemporalSplitInfo>(std::move(ttype), with_value, is_int, with_time);
    RegisterMeosFunction(loader, fn);
}

} // anonymous namespace

void TemporalTypes::RegisterTemporalTileSplit(ExtensionLoader &loader) {
    const auto I  = LogicalType::INTEGER;
    const auto D  = LogicalType::DOUBLE;
    const auto IV = LogicalType::INTERVAL;
    const auto TS = LogicalType::TIMESTAMP_TZ;
    const auto B  = LogicalType::BOOLEAN;

    // timeSplit(temporal, interval [, timestamptz [, borderInc]])
    for (const auto &ttype : AllTypes()) {
        RegisterTemporalSplit(loader, "timeSplit", {ttype, IV}, ttype, false, false, true);
        RegisterTemporalSplit(loader, "timeSplit", {ttype, IV, TS}, ttype, false, false, true);
        RegisterTemporalSplit(loader, "timeSplit", {ttype, IV, TS, B}, ttype, false, false, true);
    }
    // also for tgeompoint
    for (const auto &ttype : {TgeompointType::tgeompoint()}) {
        RegisterTemporalSplit(loader, "timeSplit", {ttype, IV}, ttype, false, false, true);
        RegisterTemporalSplit(loader, "timeSplit", {ttype, IV, TS}, ttype, false, false, true);
        RegisterTemporalSplit(loader, "timeSplit", {ttype, IV, TS, B}, ttype, false, false, true);
    }

    // valueSplit is registered separately via RegisterTnumberValueSplit

    // valueTimeSplit(tint, integer, interval [, integer, timestamptz])
    RegisterTemporalSplit(loader, "valueTimeSplit", {tint(), I, IV}, tint(), true, true, true);
    RegisterTemporalSplit(loader, "valueTimeSplit", {tint(), I, IV, I, TS}, tint(), true, true, true);
    // valueTimeSplit(tfloat, double, interval [, double, timestamptz])
    RegisterTemporalSplit(loader, "valueTimeSplit", {tfloat(), D, IV}, tfloat(), true, false, true);
    RegisterTemporalSplit(loader, "valueTimeSplit", {tfloat(), D, IV, D, TS}, tfloat(), true, false, true);
    // each full form also takes borderInc
    RegisterTemporalSplit(loader, "valueTimeSplit", {tint(), I, IV, I, TS, B}, tint(), true, true, true);
    RegisterTemporalSplit(loader, "valueTimeSplit", {tfloat(), D, IV, D, TS, B}, tfloat(), true, false, true);
}

/* ***************************************************
 * valueSplit(tint|tfloat, size [, origin]) → SETOF (number, tnumber)
 * ---------------------------------------------------
 * Wraps MEOS tint_value_split / tfloat_value_split through the in_out
 * function valueTimeSplit shares. Each row is a (bin-start value,
 * sub-temporal) pair where the sub-temporal is the slice of the input whose
 * value fell into that bin.
 ****************************************************/

void TemporalTypes::RegisterTnumberValueSplit(ExtensionLoader &loader) {
    const auto I = LogicalType::INTEGER;
    const auto D = LogicalType::DOUBLE;
    RegisterTemporalSplit(loader, "valueSplit", {tint(), I}, tint(), true, true, false);
    RegisterTemporalSplit(loader, "valueSplit", {tint(), I, I}, tint(), true, true, false);
    RegisterTemporalSplit(loader, "valueSplit", {tfloat(), D}, tfloat(), true, false, false);
    RegisterTemporalSplit(loader, "valueSplit", {tfloat(), D, D}, tfloat(), true, false, false);
    const auto B = LogicalType::BOOLEAN;
    RegisterTemporalSplit(loader, "valueSplit", {tint(), I, I, B}, tint(), true, true, false);
    RegisterTemporalSplit(loader, "valueSplit", {tfloat(), D, D, B}, tfloat(), true, false, false);
}

} // namespace duckdb
