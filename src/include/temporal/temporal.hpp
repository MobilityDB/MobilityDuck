#pragma once

#include "common.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/common/serializer/serializer.hpp"
#include "duckdb/common/serializer/deserializer.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

#include "meos_wrapper_simple.hpp"

namespace duckdb {

class ExtensionLoader;

struct TemporalTypes {
    static LogicalType tint();
    static LogicalType tbigint();
    static LogicalType tbool();
    static LogicalType tfloat();
    static LogicalType ttext();

    static const std::vector<LogicalType> &AllTypes();

    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
    static void RegisterScalarFunctions(ExtensionLoader &loader);
    static void RegisterTemporalTileSplit(ExtensionLoader &loader);
    static void RegisterTnumberValueSplit(ExtensionLoader &loader);
};

} // namespace duckdb
