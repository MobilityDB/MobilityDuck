#pragma once

#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

struct SpanTableFunctions {
    static void RegisterBins(ExtensionLoader &loader);
};

} // namespace duckdb
