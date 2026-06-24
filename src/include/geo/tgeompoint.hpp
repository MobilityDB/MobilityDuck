#pragma once

#include "common.hpp"
#include "duckdb/common/types.hpp"

#include "meos_wrapper_simple.hpp"

#include "temporal/span.hpp"
#include "temporal/set.hpp"

namespace duckdb {

class ExtensionLoader;

struct TgeompointType {
    static LogicalType tgeompoint();

    static void RegisterType(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
    static void RegisterScalarFunctions(ExtensionLoader &loader);
    static void RegisterRoundtripIO(ExtensionLoader &loader);
    static void RegisterTpointSplit(ExtensionLoader &loader);
    static void RegisterAnalyticsViz(ExtensionLoader &loader);
};

} // namespace duckdb
