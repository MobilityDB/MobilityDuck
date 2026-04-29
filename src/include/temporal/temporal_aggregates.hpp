#pragma once

#include "duckdb/function/function_set.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

struct TemporalAggregates {
    // Add tnumber overloads of extent() to the given set.
    static void AddExtentOverloads(AggregateFunctionSet &extent_set);
};

} // namespace duckdb
