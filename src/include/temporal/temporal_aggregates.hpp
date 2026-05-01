#pragma once

#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

// SkipList-state temporal aggregates (tcount / tand / tor / tmin / tmax /
// tsum / tavg / tcentroid). Registered separately from the fixed-state
// extent() aggregates (in span_aggregates.cpp) because they need a
// destructor and reach into MEOS skiplist internals.
struct TemporalAggregates {
    static void RegisterAggregateFunctions(ExtensionLoader &loader);
};

} // namespace duckdb
