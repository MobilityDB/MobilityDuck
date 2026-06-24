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

typedef struct {
    const char *alias;
    LogicalType basetype;
} basetype_struct;

static const basetype_struct BASE_TYPES[] = {
    {"tint", LogicalType::BIGINT},
    {"tbigint", LogicalType::BIGINT},
    {"tbool", LogicalType::BOOLEAN},
    {"tfloat", LogicalType::DOUBLE},
    {"ttext", LogicalType::VARCHAR}
};

struct TemporalTypes {
    static LogicalType tint();
    static LogicalType tbigint();
    static LogicalType tbool();
    static LogicalType tfloat();
    static LogicalType ttext();

    static const std::vector<LogicalType> &AllTypes();
    static LogicalType GetBaseTypeFromAlias(const char *alias);

    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
    static void RegisterScalarFunctions(ExtensionLoader &loader);
    static void RegisterTemporalUnnestFunction(ExtensionLoader &loader);
    static void RegisterWkbFunctions(ExtensionLoader &loader);
    static void RegisterTemporalTileSplit(ExtensionLoader &loader);
    static void RegisterTileGetters(ExtensionLoader &loader);
    static void RegisterTnumberValueSplit(ExtensionLoader &loader);
    static void RegisterSimilarityPath(ExtensionLoader &loader);
};

} // namespace duckdb
