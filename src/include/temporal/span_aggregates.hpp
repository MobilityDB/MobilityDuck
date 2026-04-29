#pragma once

#include "duckdb/function/function_set.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

struct SpanAggregates {
    // Add span / spanset / set overloads of extent() to the given set.
    static void AddExtentOverloads(AggregateFunctionSet &extent_set);

    // Register spanUnion(span | spanset) → spanset.
    static void RegisterSpanUnion(ExtensionLoader &loader);
};

} // namespace duckdb
