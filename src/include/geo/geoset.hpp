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
    static void RegisterScalarFunctions(ExtensionLoader &loader);        
};

struct SpatialSetFunctions{
    //cast    
    static bool Text_to_geoset(Vector &source, Vector &result, idx_t count, CastParameters &parameters);

    //other    
    static void Spatialset_as_text(DataChunk &args, ExpressionState &state, Vector &result);
    static void Spatialset_as_ewkt(DataChunk &args, ExpressionState &state, Vector &result);    
    static void Set_mem_size(DataChunk &args, ExpressionState &state, Vector &result);
    static void Spatialset_srid(DataChunk &args, ExpressionState &state, Vector &result);
    static void Spatialset_set_srid(DataChunk &args, ExpressionState &state, Vector &result_vec);
    static void Spatialset_transform(DataChunk &args, ExpressionState &state, Vector &result_vec);
    static void Set_start_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_end_value(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_num_values(DataChunk &args, ExpressionState &state, Vector &result);
    static void Set_value_n(DataChunk &args, ExpressionState &state, Vector &result_vec);
};   

} // namespace duckdb
