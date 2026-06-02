/*****************************************************************************
 *
 * This MobilityDuck code is provided under The PostgreSQL License.
 * Copyright (c) 2025, Université libre de Bruxelles and MobilityDB
 * contributors
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written
 * agreement is hereby granted, provided that the above copyright notice and
 * this paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL UNIVERSITE LIBRE DE BRUXELLES BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS
 * DOCUMENTATION, EVEN IF UNIVERSITE LIBRE DE BRUXELLES HAS BEEN ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * UNIVERSITE LIBRE DE BRUXELLES SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN
 * "AS IS" BASIS, AND UNIVERSITE LIBRE DE BRUXELLES HAS NO OBLIGATIONS TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 *****************************************************************************/

/**
 * @brief Set-set spatial-join table functions over arrays of temporal
 * geometries.
 *
 * Three table functions, each registered for both tgeompoint[] and
 * tgeometry[] input arrays:
 *   - eDwithinPairs(arr1, arr2, dist)  -> SETOF (i INTEGER, j INTEGER)
 *   - tDwithinPairs(arr1, arr2, dist)  -> SETOF (i INTEGER, j INTEGER,
 *                                                periods tstzspanset)
 *   - aDisjointPairs(arr1, arr2)       -> SETOF (i INTEGER, j INTEGER)
 *
 * They wrap the MEOS set-set spatial-join kernel symbols
 * edwithin_tgeoarr_tgeoarr / tdwithin_tgeoarr_tgeoarr /
 * adisjoint_tgeoarr_tgeoarr, which return a malloc'd flattened pair-index
 * array [i0, j0, i1, j1, ...] of length 2*count.
 */

#include "meos_wrapper_simple.hpp"

#include "geo/tgeo_setset_join.hpp"
#include "geo/tgeometry.hpp"
#include "geo/tgeompoint.hpp"
#include "temporal/spanset.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

#include "duckdb/main/extension/extension_loader.hpp"

#include <cfloat>

namespace duckdb {

namespace {

// ---------------------------------------------------------------------------
// Shared marshaling helpers
// ---------------------------------------------------------------------------

// Marshal a DuckDB LIST Value of temporal blobs into a freshly malloc'd
// array of Temporal* (each element copied into its own malloc'd buffer so
// MEOS owns nothing of DuckDB's memory). Returns nullptr with *count == 0
// for an empty (or null) list.
const Temporal **MarshalTemporalArray(const Value &list_val, int *count) {
    if (list_val.IsNull()) {
        *count = 0;
        return nullptr;
    }
    auto &children = ListValue::GetChildren(list_val);
    int n = static_cast<int>(children.size());
    if (n == 0) {
        *count = 0;
        return nullptr;
    }
    const Temporal **arr = (const Temporal **) malloc(sizeof(Temporal *) * n);
    for (int i = 0; i < n; i++) {
        string s = StringValue::Get(children[i]);
        uint8_t *p = (uint8_t *) malloc(s.size());
        memcpy(p, s.data(), s.size());
        arr[i] = (const Temporal *) p;
    }
    *count = n;
    return arr;
}

void FreeTemporalArray(const Temporal **arr, int count) {
    if (!arr)
        return;
    for (int i = 0; i < count; i++)
        free((void *) arr[i]);
    free((void *) arr);
}

// ---------------------------------------------------------------------------
// Global state shared by all three functions
// ---------------------------------------------------------------------------

struct SetSetJoinGlobalState : public GlobalTableFunctionState {
    idx_t idx = 0;
    vector<int> is;
    vector<int> js;
    vector<Value> periods;   // populated only for tDwithinPairs
};

// ---------------------------------------------------------------------------
// eDwithinPairs / aDisjointPairs  (two output columns: i, j)
// ---------------------------------------------------------------------------

struct PairsBindData : public TableFunctionData {
    Value arr1;
    Value arr2;
    double dist = 0.0;
    bool has_dist = false;
};

template <bool HasDist>
unique_ptr<FunctionData> PairsBind(ClientContext &, TableFunctionBindInput &input,
                                   vector<LogicalType> &return_types, vector<string> &names) {
    auto bd = make_uniq<PairsBindData>();
    bd->arr1 = input.inputs[0];
    bd->arr2 = input.inputs[1];
    if (HasDist) {
        bd->dist = input.inputs[2].GetValue<double>();
        bd->has_dist = true;
    }
    return_types = {LogicalType::INTEGER, LogicalType::INTEGER};
    names = {"i", "j"};
    return std::move(bd);
}

// Init for eDwithinPairs
unique_ptr<GlobalTableFunctionState> EDwithinInit(ClientContext &, TableFunctionInitInput &input) {
    auto &bd = input.bind_data->Cast<PairsBindData>();
    auto state = make_uniq<SetSetJoinGlobalState>();

    int count1 = 0, count2 = 0;
    const Temporal **arr1 = MarshalTemporalArray(bd.arr1, &count1);
    const Temporal **arr2 = MarshalTemporalArray(bd.arr2, &count2);

    int count = 0;
    int *res = edwithin_tgeoarr_tgeoarr(arr1, count1, arr2, count2, bd.dist, &count);
    if (res && count > 0) {
        state->is.reserve(count);
        state->js.reserve(count);
        for (int k = 0; k < count; k++) {
            // DuckDB lists are 1-based: expose the kernel's 0-based pair indexes as 1-based.
            state->is.push_back(res[2 * k] + 1);
            state->js.push_back(res[2 * k + 1] + 1);
        }
    }
    if (res)
        free(res);
    FreeTemporalArray(arr1, count1);
    FreeTemporalArray(arr2, count2);
    return std::move(state);
}

// Init for aDisjointPairs
unique_ptr<GlobalTableFunctionState> ADisjointInit(ClientContext &, TableFunctionInitInput &input) {
    auto &bd = input.bind_data->Cast<PairsBindData>();
    auto state = make_uniq<SetSetJoinGlobalState>();

    int count1 = 0, count2 = 0;
    const Temporal **arr1 = MarshalTemporalArray(bd.arr1, &count1);
    const Temporal **arr2 = MarshalTemporalArray(bd.arr2, &count2);

    int count = 0;
    int *res = adisjoint_tgeoarr_tgeoarr(arr1, count1, arr2, count2, &count);
    if (res && count > 0) {
        state->is.reserve(count);
        state->js.reserve(count);
        for (int k = 0; k < count; k++) {
            // DuckDB lists are 1-based: expose the kernel's 0-based pair indexes as 1-based.
            state->is.push_back(res[2 * k] + 1);
            state->js.push_back(res[2 * k + 1] + 1);
        }
    }
    if (res)
        free(res);
    FreeTemporalArray(arr1, count1);
    FreeTemporalArray(arr2, count2);
    return std::move(state);
}

void PairsExec(ClientContext &, TableFunctionInput &input, DataChunk &output) {
    auto &state = input.global_state->Cast<SetSetJoinGlobalState>();
    idx_t remaining = state.is.size() - state.idx;
    idx_t emit = MinValue<idx_t>(STANDARD_VECTOR_SIZE, remaining);
    for (idx_t i = 0; i < emit; i++) {
        output.data[0].SetValue(i, Value::INTEGER(state.is[state.idx]));
        output.data[1].SetValue(i, Value::INTEGER(state.js[state.idx]));
        state.idx++;
    }
    output.SetCardinality(emit);
}

// ---------------------------------------------------------------------------
// tDwithinPairs  (three output columns: i, j, periods)
// ---------------------------------------------------------------------------

unique_ptr<FunctionData> TDwithinBind(ClientContext &, TableFunctionBindInput &input,
                                      vector<LogicalType> &return_types, vector<string> &names) {
    auto bd = make_uniq<PairsBindData>();
    bd->arr1 = input.inputs[0];
    bd->arr2 = input.inputs[1];
    bd->dist = input.inputs[2].GetValue<double>();
    bd->has_dist = true;
    return_types = {LogicalType::INTEGER, LogicalType::INTEGER, SpansetTypes::tstzspanset()};
    names = {"i", "j", "periods"};
    return std::move(bd);
}

unique_ptr<GlobalTableFunctionState> TDwithinInit(ClientContext &, TableFunctionInitInput &input) {
    auto &bd = input.bind_data->Cast<PairsBindData>();
    auto state = make_uniq<SetSetJoinGlobalState>();

    int count1 = 0, count2 = 0;
    const Temporal **arr1 = MarshalTemporalArray(bd.arr1, &count1);
    const Temporal **arr2 = MarshalTemporalArray(bd.arr2, &count2);

    int count = 0;
    SpanSet **periods = nullptr;
    int *res = tdwithin_tgeoarr_tgeoarr(arr1, count1, arr2, count2, bd.dist, &count, &periods);
    if (res && count > 0) {
        state->is.reserve(count);
        state->js.reserve(count);
        state->periods.reserve(count);
        for (int k = 0; k < count; k++) {
            // DuckDB lists are 1-based: expose the kernel's 0-based pair indexes as 1-based.
            state->is.push_back(res[2 * k] + 1);
            state->js.push_back(res[2 * k + 1] + 1);
            SpanSet *ss = periods[k];
            Value v = Value::BLOB(reinterpret_cast<const_data_ptr_t>(ss),
                                  static_cast<idx_t>(spanset_mem_size(ss)));
            v.Reinterpret(SpansetTypes::tstzspanset());
            state->periods.push_back(std::move(v));
            free(ss);
        }
    }
    if (res)
        free(res);
    if (periods)
        free(periods);
    FreeTemporalArray(arr1, count1);
    FreeTemporalArray(arr2, count2);
    return std::move(state);
}

void TDwithinExec(ClientContext &, TableFunctionInput &input, DataChunk &output) {
    auto &state = input.global_state->Cast<SetSetJoinGlobalState>();
    idx_t remaining = state.is.size() - state.idx;
    idx_t emit = MinValue<idx_t>(STANDARD_VECTOR_SIZE, remaining);
    for (idx_t i = 0; i < emit; i++) {
        output.data[0].SetValue(i, Value::INTEGER(state.is[state.idx]));
        output.data[1].SetValue(i, Value::INTEGER(state.js[state.idx]));
        output.data[2].SetValue(i, state.periods[state.idx]);
        state.idx++;
    }
    output.SetCardinality(emit);
}

// ---------------------------------------------------------------------------
// minDistance(arr1, arr2) -> DOUBLE  (set-set spatial minimum distance)
// ---------------------------------------------------------------------------
// Scalar reduction member of the set-set family: the exact minimum spatial
// distance between any temporal geometry of arr1 and any of arr2, wrapping the
// MEOS kernel mindistance_tgeoarr_tgeoarr, which uses each element's STBox as a
// sound lower-bound prefilter so element pairs whose boxes are already farther
// apart than the running minimum are skipped.
void MindistanceArrExec(DataChunk &args, ExpressionState &, Vector &result) {
    const idx_t n = args.size();
    auto out = FlatVector::GetData<double>(result);
    auto &outv = FlatVector::Validity(result);
    for (idx_t row = 0; row < n; row++) {
        Value v1 = args.data[0].GetValue(row);
        Value v2 = args.data[1].GetValue(row);
        if (v1.IsNull() || v2.IsNull()) {
            outv.SetInvalid(row);
            continue;
        }
        int count1 = 0, count2 = 0;
        const Temporal **arr1 = MarshalTemporalArray(v1, &count1);
        const Temporal **arr2 = MarshalTemporalArray(v2, &count2);
        if (count1 == 0 || count2 == 0) {
            FreeTemporalArray(arr1, count1);
            FreeTemporalArray(arr2, count2);
            outv.SetInvalid(row);
            continue;
        }
        double d = mindistance_tgeoarr_tgeoarr(arr1, count1, arr2, count2);
        FreeTemporalArray(arr1, count1);
        FreeTemporalArray(arr2, count2);
        if (d == DBL_MAX) {
            outv.SetInvalid(row);
            continue;
        }
        out[row] = d;
    }
}

} // anonymous namespace

void RegisterSetSetSpatialJoin(ExtensionLoader &loader) {
    const auto D = LogicalType::DOUBLE;

    for (const auto &elem : {TgeompointType::TGEOMPOINT(), TGeometryTypes::TGEOMETRY()}) {
        const auto L = LogicalType::LIST(elem);

        loader.RegisterFunction(TableFunction(
            "eDwithinPairs", {L, L, D}, PairsExec, PairsBind<true>, EDwithinInit));

        loader.RegisterFunction(TableFunction(
            "tDwithinPairs", {L, L, D}, TDwithinExec, TDwithinBind, TDwithinInit));

        loader.RegisterFunction(TableFunction(
            "aDisjointPairs", {L, L}, PairsExec, PairsBind<false>, ADisjointInit));

        duckdb::RegisterSerializedScalarFunction(
            loader, ScalarFunction("minDistance", {L, L}, D, MindistanceArrExec));
    }
}

} // namespace duckdb
