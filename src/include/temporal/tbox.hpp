#pragma once

#include "common.hpp"
#include "duckdb/common/types.hpp"

#include "meos_wrapper_simple.hpp"

#include "span.hpp"
#include "set.hpp"

namespace duckdb {

class ExtensionLoader;

struct TboxType {
    static LogicalType tbox();

    static void RegisterType(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
    static void RegisterScalarFunctions(ExtensionLoader &loader);
};

} // namespace duckdb
