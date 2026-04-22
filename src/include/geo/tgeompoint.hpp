#pragma once

#include "common.hpp"
#include "duckdb/common/types.hpp"

#include "meos_wrapper_simple.hpp"

#include "temporal/span.hpp"
#include "temporal/set.hpp"

namespace duckdb {

class ExtensionLoader;

struct TgeompointType {
    static LogicalType TGEOMPOINT();

    static void RegisterType(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
    static void RegisterScalarFunctions(ExtensionLoader &loader);
};

} // namespace duckdb