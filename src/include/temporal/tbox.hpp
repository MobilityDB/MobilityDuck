#pragma once

#include "common.hpp"
#include "duckdb/common/types.hpp"

#include "meos_wrapper_simple.hpp"

#include "span.hpp"
#include "set.hpp"

namespace duckdb {

class ExtensionLoader;

struct TboxType {
    static LogicalType TBOX();

    static void RegisterType(DatabaseInstance &db);
    static void RegisterCastFunctions(DatabaseInstance &db);
    static void RegisterScalarFunctions(DatabaseInstance &db);
};

} // namespace duckdb