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
    {"TINT", LogicalType::BIGINT},
    {"TBOOL", LogicalType::BOOLEAN},
    {"TFLOAT", LogicalType::DOUBLE},
    {"TTEXT", LogicalType::VARCHAR}
};

struct TemporalTypes {
    static LogicalType TINT();
    static LogicalType TBOOL();
    static LogicalType TFLOAT();
    static LogicalType TTEXT();

    static const std::vector<LogicalType> &AllTypes();
    static LogicalType GetBaseTypeFromAlias(const char *alias);

    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
    static void RegisterScalarFunctions(ExtensionLoader &loader);
    static void RegisterTemporalUnnestFunction(ExtensionLoader &loader);
    static void RegisterTileFunctions(ExtensionLoader &loader);
};

} // namespace duckdb