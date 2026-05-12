#pragma once

#include "duckdb/function/function_set.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

struct SpanAggregates {
    // Register all span-typed extent() overloads and SpanUnionAgg.
    static void RegisterAggregateFunctions(ExtensionLoader &loader);
};

} // namespace duckdb
