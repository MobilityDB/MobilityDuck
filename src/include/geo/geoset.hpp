#pragma once

#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

namespace duckdb {

struct SpatialSetType{    
    static LogicalType geomset();     
    static LogicalType geogset();   

    static void RegisterTypes(ExtensionLoader &loader);
    static void RegisterCastFunctions(ExtensionLoader &loader);
};

struct SpatialSetFunctions{
    //cast
    static bool Text_to_geoset(Vector &source, Vector &result, idx_t count, CastParameters &parameters);
};   

} // namespace duckdb
