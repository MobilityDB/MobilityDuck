#pragma once

#include "duckdb/function/function_set.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

struct TemporalAggregates {
    // Add tnumber overloads of extent() to the given set.
    static void AddExtentOverloads(AggregateFunctionSet &extent_set);

    // Register tCount(temporal) → tint as its own AggregateFunctionSet.
    static void RegisterTCount(ExtensionLoader &loader);

    // Register tAnd / tOr / tMin / tMax / tSum aggregates.
    static void RegisterTemporalAggregates(ExtensionLoader &loader);

    // Register wmin / wmax / wsum window aggregates.
    static void RegisterWindowAggregates(ExtensionLoader &loader);

    // Register tAvg(tnumber) -> tfloat aggregate.
    static void RegisterTAvg(ExtensionLoader &loader);

    // Register MergeAgg / AppendInstantAgg / AppendSequenceAgg aggregates.
    static void RegisterAppendMergeAggregates(ExtensionLoader &loader);
};

} // namespace duckdb
