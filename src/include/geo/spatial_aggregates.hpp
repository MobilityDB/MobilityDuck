#pragma once

#include "duckdb/function/function_set.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

struct SpatialAggregates {
    // Add tgeompoint overloads of extent() to the given set.
    static void AddExtentOverloads(AggregateFunctionSet &extent_set);

    // Register tCentroid(tgeompoint) -> tgeompoint.
    static void RegisterTcentroid(ExtensionLoader &loader);

    // Register MinDistanceAgg(tgeompoint, tgeompoint) / (tgeogpoint, tgeogpoint) -> DOUBLE.
    static void RegisterMinDistance(ExtensionLoader &loader);
};

} // namespace duckdb
