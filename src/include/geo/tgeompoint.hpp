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

    static void RegisterType(DatabaseInstance &db);
    static void RegisterCastFunctions(DatabaseInstance &db);
    static void RegisterScalarFunctions(DatabaseInstance &db);
};

} // namespace duckdb