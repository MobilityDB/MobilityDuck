#pragma once

#include "common.hpp"
#include "duckdb/common/types.hpp"

#include "meos_wrapper_simple.hpp"

#include "temporal/span.hpp"
#include "temporal/set.hpp"

namespace duckdb {

class ExtensionLoader;

struct TgeogpointType {
    static LogicalType TGEOGPOINT();

    static void RegisterType(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
    static void RegisterScalarFunctions(ExtensionLoader &loader);
    static void RegisterRoundtripIO(ExtensionLoader &loader);
};

} // namespace duckdb
