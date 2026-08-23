#include "meos_wrapper_simple.hpp"

#include "temporal/span.hpp"
#include "temporal/span_table_functions.hpp"
#include "temporal/spanset.hpp"
#include "time_util.hpp"
#include "mobilityduck/meos_thread.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

namespace {

// Tag identifying the input span / spanset variant. Drives which MEOS
// function the global-state init dispatches to.
enum class BinsKind {
    intspan,
    bigintspan,
    floatspan,
    datespan,
    tstzspan,
    INTSPANSET,
    BIGINTSPANSET,
    FLOATSPANSET,
    DATESPANSET,
    TSTZSPANSET,
};

struct BinsBindData : public FunctionData {
    BinsKind kind;
    LogicalType output_span_type;
    // Captured constant arguments — bins() args are constants (the input
    // span/spanset blob, the vsize/duration, and the vorigin/torigin).
    string blob;
    Value vsize;
    Value vorigin;

    unique_ptr<FunctionData> Copy() const override {
        auto r = make_uniq<BinsBindData>();
        r->kind = kind;
        r->output_span_type = output_span_type;
        r->blob = blob;
        r->vsize = vsize;
        r->vorigin = vorigin;
        // DuckDB 1.4 disallows implicit derived->base unique_ptr conversion;
        // explicit base-type construction from the moved-from derived pointer.
        return unique_ptr_cast<BinsBindData, FunctionData>(std::move(r));
    }
    bool Equals(const FunctionData &other_p) const override {
        auto &other = other_p.Cast<BinsBindData>();
        return kind == other.kind && blob == other.blob && vsize == other.vsize && vorigin == other.vorigin;
    }
};

// Materializes the bin array on first row of execution. Stores the
// flat array of span bytes plus the count.
struct BinsGlobalState : public GlobalTableFunctionState {
    Span *bins = nullptr;
    int count = 0;
    int offset = 0;

    ~BinsGlobalState() {
        if (bins) {
            free(bins);
        }
    }
};

static unique_ptr<GlobalTableFunctionState> BinsInitGlobal(ClientContext &, TableFunctionInitInput &input) {
    EnsureMeosThreadInitialized();
    auto &bind_data = input.bind_data->Cast<BinsBindData>();
    auto state = make_uniq<BinsGlobalState>();

    const void *raw_blob = bind_data.blob.data();
    size_t raw_size = bind_data.blob.size();

    switch (bind_data.kind) {
        case BinsKind::intspan: {
            Span *s = reinterpret_cast<Span *>(malloc(raw_size));
            memcpy(s, raw_blob, raw_size);
            state->bins = intspan_bins(s, bind_data.vsize.GetValue<int32_t>(),
                                       bind_data.vorigin.GetValue<int32_t>(), &state->count);
            free(s);
            break;
        }
        case BinsKind::bigintspan: {
            Span *s = reinterpret_cast<Span *>(malloc(raw_size));
            memcpy(s, raw_blob, raw_size);
            state->bins = bigintspan_bins(s, bind_data.vsize.GetValue<int64_t>(),
                                          bind_data.vorigin.GetValue<int64_t>(), &state->count);
            free(s);
            break;
        }
        case BinsKind::floatspan: {
            Span *s = reinterpret_cast<Span *>(malloc(raw_size));
            memcpy(s, raw_blob, raw_size);
            state->bins = floatspan_bins(s, bind_data.vsize.GetValue<double>(),
                                         bind_data.vorigin.GetValue<double>(), &state->count);
            free(s);
            break;
        }
        case BinsKind::datespan: {
            Span *s = reinterpret_cast<Span *>(malloc(raw_size));
            memcpy(s, raw_blob, raw_size);
            interval_t duck_duration = bind_data.vsize.GetValue<interval_t>();
            MeosInterval duration = IntervaltToInterval(duck_duration);
            int32_t torigin = ToMeosDate(bind_data.vorigin.GetValue<date_t>());
            state->bins = datespan_bins(s, &duration, torigin, &state->count);
            free(s);
            break;
        }
        case BinsKind::tstzspan: {
            Span *s = reinterpret_cast<Span *>(malloc(raw_size));
            memcpy(s, raw_blob, raw_size);
            interval_t duck_duration = bind_data.vsize.GetValue<interval_t>();
            MeosInterval duration = IntervaltToInterval(duck_duration);
            timestamp_tz_t in_ts;
            in_ts.value = bind_data.vorigin.GetValueUnsafe<timestamp_t>().value;
            timestamp_tz_t meos_ts = DuckDBToMeosTimestamp(in_ts);
            state->bins = tstzspan_bins(s, &duration, meos_ts.value, &state->count);
            free(s);
            break;
        }
        case BinsKind::INTSPANSET: {
            SpanSet *ss = reinterpret_cast<SpanSet *>(malloc(raw_size));
            memcpy(ss, raw_blob, raw_size);
            state->bins = intspanset_bins(ss, bind_data.vsize.GetValue<int32_t>(),
                                          bind_data.vorigin.GetValue<int32_t>(), &state->count);
            free(ss);
            break;
        }
        case BinsKind::BIGINTSPANSET: {
            SpanSet *ss = reinterpret_cast<SpanSet *>(malloc(raw_size));
            memcpy(ss, raw_blob, raw_size);
            state->bins = bigintspanset_bins(ss, bind_data.vsize.GetValue<int64_t>(),
                                             bind_data.vorigin.GetValue<int64_t>(), &state->count);
            free(ss);
            break;
        }
        case BinsKind::FLOATSPANSET: {
            SpanSet *ss = reinterpret_cast<SpanSet *>(malloc(raw_size));
            memcpy(ss, raw_blob, raw_size);
            state->bins = floatspanset_bins(ss, bind_data.vsize.GetValue<double>(),
                                            bind_data.vorigin.GetValue<double>(), &state->count);
            free(ss);
            break;
        }
        case BinsKind::DATESPANSET: {
            SpanSet *ss = reinterpret_cast<SpanSet *>(malloc(raw_size));
            memcpy(ss, raw_blob, raw_size);
            interval_t duck_duration = bind_data.vsize.GetValue<interval_t>();
            MeosInterval duration = IntervaltToInterval(duck_duration);
            int32_t torigin = ToMeosDate(bind_data.vorigin.GetValue<date_t>());
            state->bins = datespanset_bins(ss, &duration, torigin, &state->count);
            free(ss);
            break;
        }
        case BinsKind::TSTZSPANSET: {
            SpanSet *ss = reinterpret_cast<SpanSet *>(malloc(raw_size));
            memcpy(ss, raw_blob, raw_size);
            interval_t duck_duration = bind_data.vsize.GetValue<interval_t>();
            MeosInterval duration = IntervaltToInterval(duck_duration);
            timestamp_tz_t in_ts;
            in_ts.value = bind_data.vorigin.GetValueUnsafe<timestamp_t>().value;
            timestamp_tz_t meos_ts = DuckDBToMeosTimestamp(in_ts);
            state->bins = tstzspanset_bins(ss, &duration, meos_ts.value, &state->count);
            free(ss);
            break;
        }
    }
    return std::move(state);
}

static void BinsExecute(ClientContext &, TableFunctionInput &data_p, DataChunk &output) {
    auto &state = data_p.global_state->Cast<BinsGlobalState>();
    if (state.offset >= state.count) {
        output.SetCardinality(0);
        return;
    }
    idx_t row = 0;
    auto &out_vec = output.data[0];
    auto *out_data = FlatVector::GetData<string_t>(out_vec);
    while (state.offset < state.count && row < STANDARD_VECTOR_SIZE) {
        out_data[row] = StringVector::AddStringOrBlob(
            out_vec, reinterpret_cast<const char *>(&state.bins[state.offset]), sizeof(Span));
        state.offset++;
        row++;
    }
    output.SetCardinality(row);
}

template <BinsKind KIND>
static unique_ptr<FunctionData> BinsBind(ClientContext &, TableFunctionBindInput &input,
                                          vector<LogicalType> &return_types, vector<string> &names) {
    if (input.inputs.size() != 3) {
        throw BinderException("bins(...) requires exactly 3 arguments");
    }
    for (idx_t i = 0; i < 3; i++) {
        if (input.inputs[i].IsNull()) {
            throw BinderException("bins(...) does not accept NULL arguments");
        }
    }

    auto data = make_uniq<BinsBindData>();
    data->kind = KIND;
    auto blob = input.inputs[0].GetValueUnsafe<string_t>();
    data->blob.assign(blob.GetData(), blob.GetSize());
    data->vsize = input.inputs[1];
    data->vorigin = input.inputs[2];

    LogicalType span_out;
    switch (KIND) {
        case BinsKind::intspan:
        case BinsKind::INTSPANSET:    span_out = SpanTypes::intspan(); break;
        case BinsKind::bigintspan:
        case BinsKind::BIGINTSPANSET: span_out = SpanTypes::bigintspan(); break;
        case BinsKind::floatspan:
        case BinsKind::FLOATSPANSET:  span_out = SpanTypes::floatspan(); break;
        case BinsKind::datespan:
        case BinsKind::DATESPANSET:   span_out = SpanTypes::datespan(); break;
        case BinsKind::tstzspan:
        case BinsKind::TSTZSPANSET:   span_out = SpanTypes::tstzspan(); break;
    }
    data->output_span_type = span_out;
    return_types.emplace_back(span_out);
    names.emplace_back("bin");
    return std::move(data);
}

template <BinsKind KIND>
static TableFunction MakeBinsFunction(const LogicalType &input_type, const LogicalType &vsize_type,
                                       const LogicalType &vorigin_type) {
    return TableFunction("bins",
                         {input_type, vsize_type, vorigin_type},
                         BinsExecute,
                         BinsBind<KIND>,
                         BinsInitGlobal);
}

} // namespace

void SpanTableFunctions::RegisterBins(ExtensionLoader &loader) {
    // span variants
    loader.RegisterFunction(MakeBinsFunction<BinsKind::intspan>(
        SpanTypes::intspan(), LogicalType::INTEGER, LogicalType::INTEGER));
    loader.RegisterFunction(MakeBinsFunction<BinsKind::bigintspan>(
        SpanTypes::bigintspan(), LogicalType::BIGINT, LogicalType::BIGINT));
    loader.RegisterFunction(MakeBinsFunction<BinsKind::floatspan>(
        SpanTypes::floatspan(), LogicalType::DOUBLE, LogicalType::DOUBLE));
    loader.RegisterFunction(MakeBinsFunction<BinsKind::datespan>(
        SpanTypes::datespan(), LogicalType::INTERVAL, LogicalType::DATE));
    loader.RegisterFunction(MakeBinsFunction<BinsKind::tstzspan>(
        SpanTypes::tstzspan(), LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ));

    // spanset variants
    loader.RegisterFunction(MakeBinsFunction<BinsKind::INTSPANSET>(
        SpansetTypes::intspanset(), LogicalType::INTEGER, LogicalType::INTEGER));
    loader.RegisterFunction(MakeBinsFunction<BinsKind::BIGINTSPANSET>(
        SpansetTypes::bigintspanset(), LogicalType::BIGINT, LogicalType::BIGINT));
    loader.RegisterFunction(MakeBinsFunction<BinsKind::FLOATSPANSET>(
        SpansetTypes::floatspanset(), LogicalType::DOUBLE, LogicalType::DOUBLE));
    loader.RegisterFunction(MakeBinsFunction<BinsKind::DATESPANSET>(
        SpansetTypes::datespanset(), LogicalType::INTERVAL, LogicalType::DATE));
    loader.RegisterFunction(MakeBinsFunction<BinsKind::TSTZSPANSET>(
        SpansetTypes::tstzspanset(), LogicalType::INTERVAL, LogicalType::TIMESTAMP_TZ));
}

} // namespace duckdb
