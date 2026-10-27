#include "geo/geoset.hpp"
#include "duckdb_version_compat.hpp"
#include "tydef.hpp"
#include "geo_util.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "mobilityduck/meos_registration.hpp"
#include "temporal/set_functions.hpp"

extern "C" {    
    #include "meos.h"    
    #include "meos_internal.h"   
    #include "meos_geo.h"   
    #include "meos_internal_geo.h"     
}

namespace duckdb {

LogicalType SpatialSetType::geomset() {
    auto type = LogicalType(LogicalTypeId::BLOB);     
    type.SetAlias("geomset");
    return type;
}

LogicalType SpatialSetType::geogset() {
    auto type = LogicalType(LogicalTypeId::BLOB);     
    type.SetAlias("geogset");
    return type;
}

void SpatialSetType::RegisterTypes(ExtensionLoader &loader){
    loader.RegisterType( "geomset", geomset());
    loader.RegisterType( "geogset", geogset());
}

void SpatialSetType::RegisterCastFunctions(ExtensionLoader &loader) {        
    RegisterMeosCastFunction(loader, 
        LogicalType::VARCHAR, 
        SpatialSetType::geomset(),                                    
        SpatialSetFunctions::Text_to_geoset   
    );     
    RegisterMeosCastFunction(loader, 
        LogicalType::VARCHAR, 
        SpatialSetType::geogset(),                                    
        SpatialSetFunctions::Text_to_geoset   
    ); 
    // A geometry set reads as text as every set does, through set_out
    RegisterMeosCastFunction(loader,
        SpatialSetType::geomset(),
        LogicalType::VARCHAR,
        SetFunctions::Set_to_text
    );
    RegisterMeosCastFunction(loader,
        SpatialSetType::geogset(),
        LogicalType::VARCHAR,
        SetFunctions::Set_to_text
    );
}

// --- Cast Function ---
bool SpatialSetFunctions::Text_to_geoset(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    source.Flatten(count);

    auto target_type = result.GetType();    
    auto input_data = FlatVector::GetData<string_t>(source);
    auto result_data = FlatVector::GetData<string_t>(result);

    for (idx_t i = 0; i < count; ++i) {
        if (FlatVector::IsNull(source, i)) {
            FlatVector::SetNull(result, i, true);
            continue;
        }

        const std::string input_blob = input_data[i].GetString();
        auto set_type = (target_type == SpatialSetType::geomset()) ? T_GEOMSET:T_GEOGSET;
        Set *s = set_in(input_blob.c_str(), set_type);        
        size_t total_size =  set_mem_size(s); 
        result_data[i] = StringVector::AddStringOrBlob(result, (const char*)s, total_size);        
        free(s);
    }

    result.SetVectorType(VectorType::FLAT_VECTOR);
    return true;
}

} // namespace duckdb
