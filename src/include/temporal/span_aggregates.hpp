#pragma once

#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

struct SpanAggregates {
    static void RegisterAggregateFunctions(ExtensionLoader &loader);
};

} // namespace duckdb
