#pragma once

#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

struct TemporalParquetFunctions {
    static void Register(ExtensionLoader &loader);
};

} // namespace duckdb
