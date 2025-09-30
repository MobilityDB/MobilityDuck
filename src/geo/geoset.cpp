#include "geo/geoset.hpp"
#include "tydef.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/main/extension_util.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension_util.hpp"


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

LogicalType SpatialSetType::WKB_BLOB(){
    auto type = LogicalType(LogicalTypeId::BLOB);     
    type.SetAlias("WKB_BLOB");
    return type;
}

LogicalType SpatialSetType::geogset() {
    auto type = LogicalType(LogicalTypeId::BLOB);     
    type.SetAlias("geogset");
    return type;
}

void SpatialSetType::RegisterTypes(DatabaseInstance &db){
    ExtensionUtil::RegisterType(db, "geomset", geomset());
    ExtensionUtil::RegisterType(db, "geogset", geogset());
}

void SpatialSetType::RegisterCastFunctions(DatabaseInstance &instance) {        
    ExtensionUtil::RegisterCastFunction(
        instance,
        LogicalType::VARCHAR, 
        SpatialSetType::geomset(),                                    
        SpatialSetFunctions::Text_to_geoset   
    );     
    ExtensionUtil::RegisterCastFunction(
        instance,
        LogicalType::VARCHAR, 
        SpatialSetType::geogset(),                                    
        SpatialSetFunctions::Text_to_geoset   
    ); 
 
}

void SpatialSetType::RegisterScalarFunctions(DatabaseInstance &db) {	    
    ExtensionUtil::RegisterFunction(db, ScalarFunction(
		"asText", 
        {SpatialSetType::geomset()}, LogicalType::VARCHAR, SpatialSetFunctions::Spatialset_as_text));
    
    ExtensionUtil::RegisterFunction(db, ScalarFunction(
		"asText", 
        {SpatialSetType::geogset()}, LogicalType::VARCHAR, SpatialSetFunctions::Spatialset_as_text));

    ExtensionUtil::RegisterFunction(db, ScalarFunction(
		"asText", 
        {SpatialSetType::geomset(), LogicalType::INTEGER}, LogicalType::VARCHAR, SpatialSetFunctions::Spatialset_as_text));
    
    ExtensionUtil::RegisterFunction(db, ScalarFunction(
		"asText", 
        {SpatialSetType::geogset(), LogicalType::INTEGER}, LogicalType::VARCHAR, SpatialSetFunctions::Spatialset_as_text));

        ExtensionUtil::RegisterFunction(db, ScalarFunction(
		"asEWKT", 
        {SpatialSetType::geomset()}, LogicalType::VARCHAR, SpatialSetFunctions::Spatialset_as_ewkt));
    
    ExtensionUtil::RegisterFunction(db, ScalarFunction(
		"asEWKT", 
        {SpatialSetType::geogset()}, LogicalType::VARCHAR, SpatialSetFunctions::Spatialset_as_ewkt));

    ExtensionUtil::RegisterFunction(db, ScalarFunction(
		"asEWKT", 
        {SpatialSetType::geomset(), LogicalType::INTEGER}, LogicalType::VARCHAR, SpatialSetFunctions::Spatialset_as_ewkt));
    
    ExtensionUtil::RegisterFunction(db, ScalarFunction(
		"asEWKT", 
        {SpatialSetType::geogset(), LogicalType::INTEGER}, LogicalType::VARCHAR, SpatialSetFunctions::Spatialset_as_ewkt));    

    ExtensionUtil::RegisterFunction(db, ScalarFunction(
        "memSize", 
        {SpatialSetType::geomset()}, LogicalType::INTEGER, SpatialSetFunctions::Set_mem_size));
    
    ExtensionUtil::RegisterFunction(db, ScalarFunction(
        "memSize", 
        {SpatialSetType::geogset()}, LogicalType::INTEGER, SpatialSetFunctions::Set_mem_size));

    ExtensionUtil::RegisterFunction(db, ScalarFunction(
        "SRID", 
        {SpatialSetType::geomset()}, LogicalType::INTEGER, SpatialSetFunctions::Spatialset_srid));
    
    ExtensionUtil::RegisterFunction(db, ScalarFunction(
        "SRID", 
        {SpatialSetType::geogset()}, LogicalType::INTEGER, SpatialSetFunctions::Spatialset_srid));

    ExtensionUtil::RegisterFunction(db, ScalarFunction(
		"setSRID", 
        {SpatialSetType::geomset(), LogicalType::INTEGER}, SpatialSetType::geomset(), SpatialSetFunctions::Spatialset_set_srid));

    ExtensionUtil::RegisterFunction(db, ScalarFunction(
		"setSRID", 
        {SpatialSetType::geogset(), LogicalType::INTEGER}, SpatialSetType::geogset(), SpatialSetFunctions::Spatialset_set_srid));

    ExtensionUtil::RegisterFunction(db, ScalarFunction(
		"transform", 
        {SpatialSetType::geomset(), LogicalType::INTEGER}, SpatialSetType::geomset(), SpatialSetFunctions::Spatialset_transform));
    
    ExtensionUtil::RegisterFunction(db, ScalarFunction(
		"transform", 
        {SpatialSetType::geogset(), LogicalType::INTEGER}, SpatialSetType::geogset(), SpatialSetFunctions::Spatialset_transform));

    ExtensionUtil::RegisterFunction(db, ScalarFunction(
		"startValue", {SpatialSetType::geomset()},  
		SpatialSetType::WKB_BLOB(),    // return geometry as WKB --> it will be casted to geometry type in spatial
		SpatialSetFunctions::Set_start_value
	));    

    ExtensionUtil::RegisterFunction(db, ScalarFunction(
        "endValue", 
        {SpatialSetType::geomset()}, SpatialSetType::WKB_BLOB(), SpatialSetFunctions::Set_end_value));

    ExtensionUtil::RegisterFunction(db, ScalarFunction(
        "numValues",
        {SpatialSetType::geomset()}, LogicalType::INTEGER, SpatialSetFunctions::Set_num_values));
    
    ExtensionUtil::RegisterFunction(db, ScalarFunction(
        "numValues",
        {SpatialSetType::geogset()}, LogicalType::INTEGER, SpatialSetFunctions::Set_num_values));

    ExtensionUtil::RegisterFunction(db, ScalarFunction(
        "valueN", {SpatialSetType::geomset(), LogicalType::INTEGER},  
        SpatialSetType::WKB_BLOB(),    
        SpatialSetFunctions::Set_value_n
    )); 
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

// --- asText ---
void SpatialSetFunctions::Spatialset_as_text(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input_vec = args.data[0];
    input_vec.Flatten(args.size());

    bool has_digits = args.ColumnCount() > 1;
    Vector *digits_vec_ptr = has_digits ? &args.data[1] : nullptr;
    if (has_digits) digits_vec_ptr->Flatten(args.size());
    for (idx_t i = 0; i < args.size(); i++) {
        if (FlatVector::IsNull(input_vec, i) || (has_digits && FlatVector::IsNull(*digits_vec_ptr, i))) {
            FlatVector::SetNull(result, i, true);
            continue;
        }
        auto blob = FlatVector::GetData<string_t>(input_vec)[i];
        int digits = has_digits ? FlatVector::GetData<int32_t>(*digits_vec_ptr)[i] : 15; // DEFAULT Max Digits = 15 
        const uint8_t *data = (const uint8_t *)blob.GetData();
        size_t size = blob.GetSize();

        Set *s = (Set *)malloc(size);
        memcpy(s, data, size);

        char *cstr = spatialset_as_text(s, digits);
        auto str = StringVector::AddString(result, cstr);
        FlatVector::GetData<string_t>(result)[i] = str;
        free(s);
        free(cstr);
    }
}
// --- asEWKT ---
void SpatialSetFunctions::Spatialset_as_ewkt(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input_vec = args.data[0];
    input_vec.Flatten(args.size());

    bool has_digits = args.ColumnCount() > 1;
    Vector *digits_vec_ptr = has_digits ? &args.data[1] : nullptr;
    if (has_digits) digits_vec_ptr->Flatten(args.size());
    for (idx_t i = 0; i < args.size(); i++) {
        if (FlatVector::IsNull(input_vec, i) || (has_digits && FlatVector::IsNull(*digits_vec_ptr, i))) {
            FlatVector::SetNull(result, i, true);
            continue;
        }
        auto blob = FlatVector::GetData<string_t>(input_vec)[i];
        int digits = has_digits ? FlatVector::GetData<int32_t>(*digits_vec_ptr)[i] : 15; // DEFAULT Max Digits = 15 
        const uint8_t *data = (const uint8_t *)blob.GetData();
        size_t size = blob.GetSize();

        Set *s = (Set *)malloc(size);
        memcpy(s, data, size);

        char *cstr = spatialset_as_ewkt(s, digits);
        auto str = StringVector::AddString(result, cstr);
        FlatVector::GetData<string_t>(result)[i] = str;
        free(s);
        free(cstr);
    }
}

// --- memSize ---
void SpatialSetFunctions::Set_mem_size(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];

    UnaryExecutor::Execute<string_t, int32_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> int32_t {                        
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            Set *s = (Set*)malloc(size);
            memcpy(s, data, size);
            int mem_size = set_mem_size(s);  
            free(s);
            return mem_size;             
        });
}

// --- srid ---
void SpatialSetFunctions::Spatialset_srid(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];

    UnaryExecutor::Execute<string_t, int32_t>(
        input, result, args.size(),
        [&](string_t input_blob) -> int32_t {                        
            const uint8_t *data = (const uint8_t *)input_blob.GetData();
            size_t size = input_blob.GetSize();
            Set *s = (Set*)malloc(size);
            memcpy(s, data, size);
            int srid = spatialset_srid(s);
            free(s);
            return srid;
        });
}

// Set SRID 
void SpatialSetFunctions::Spatialset_set_srid(DataChunk &args, ExpressionState &state, Vector &result_vec) {
	auto &input = args.data[0];
	auto &srid_vec = args.data[1];

	input.Flatten(args.size());
	srid_vec.Flatten(args.size());

    auto input_data = FlatVector::GetData<string_t>(input);
    auto srid_data = FlatVector::GetData<int32_t>(srid_vec);
    auto result_data = FlatVector::GetData<string_t>(result_vec);


	for (idx_t i = 0; i < args.size(); i++) {
        if (FlatVector::IsNull(input, i) || FlatVector::IsNull(srid_vec, i)) {
            FlatVector::SetNull(result_vec, i, true);
            continue;
        }
        const string_t &blob = input_data[i];
        const uint8_t *data = (const uint8_t *)blob.GetData();
        size_t size = blob.GetSize();
        
        Set *set = (Set *)malloc(size);
        memcpy(set, data, size);
        
        Set *modified = spatialset_set_srid(set, srid_data[i]);

        size_t total_size =  set_mem_size(modified); 
        result_data[i] = StringVector::AddStringOrBlob(result_vec, (const char*)modified, total_size);                

        free(set);
        free(modified);        
    }

}

// --- transform ---
void SpatialSetFunctions::Spatialset_transform(DataChunk &args, ExpressionState &state, Vector &result_vec) {
	auto &input_vec = args.data[0];
	auto &srid_vec = args.data[1];

	input_vec.Flatten(args.size());
	srid_vec.Flatten(args.size());

    auto input_data = FlatVector::GetData<string_t>(input_vec);
    auto srid_data = FlatVector::GetData<int32_t>(srid_vec);
    auto result_data = FlatVector::GetData<string_t>(result_vec);


	for (idx_t i = 0; i < args.size(); i++) {
		if (FlatVector::IsNull(input_vec, i) || FlatVector::IsNull(srid_vec, i)) {
			FlatVector::SetNull(result_vec, i, true);
			continue;
		}

        const string_t &blob = input_data[i];
        const uint8_t *data = (const uint8_t *)blob.GetData();
        size_t size = blob.GetSize();
        
        Set *s = (Set *)malloc(size);
        memcpy(s, data, size);        	         	     
		Set *result = spatialset_transform(s, srid_data[i]);
        
		free(s);

		if (!result) {
			FlatVector::SetNull(result_vec, i, true);
			continue;
		}
        size_t total_size =  set_mem_size(result); 
        result_data[i] = StringVector::AddStringOrBlob(result_vec, (const char*)result, total_size);                        
        free(result);		
	}
}

// --- startValue ---
void SpatialSetFunctions::Set_start_value(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &input = args.data[0];
	input.Flatten(args.size());
	auto input_data = FlatVector::GetData<string_t>(input);
	auto result_data = FlatVector::GetData<string_t>(result);

	for (idx_t i = 0; i < args.size(); i++) {
		if (FlatVector::IsNull(input, i)) {
			FlatVector::SetNull(result, i, true);
			continue;
		}

		const string_t &blob = input_data[i];
		const uint8_t *data = (const uint8_t *)blob.GetData();
		size_t size = blob.GetSize();		
		Set *s = (Set *)malloc(size);
		memcpy(s, data, size);

		Datum d = set_start_value(s);
        GSERIALIZED *g = DatumGetGserializedP(d);
		size_t wkb_len = 0;
		uint8_t *wkb = geo_as_ewkb(g, "NDR", &wkb_len);
		
		string_t str = StringVector::AddString(result, std::string((char *)wkb, wkb_len));
		result_data[i] = str;		
		free(s);
		free(wkb);  
        
	}
}

// --- endValue ---
void SpatialSetFunctions::Set_end_value(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    input.Flatten(args.size());
    auto input_data = FlatVector::GetData<string_t>(input);
    auto result_data = FlatVector::GetData<string_t>(result);

    for (idx_t i = 0; i < args.size(); i++) {
        if (FlatVector::IsNull(input, i)) {
            FlatVector::SetNull(result, i, true);
            continue;
        }

        const string_t &blob = input_data[i];
        const uint8_t *data = (const uint8_t *)blob.GetData();
        size_t size = blob.GetSize();		
        Set *s = (Set *)malloc(size);
        memcpy(s, data, size);

        Datum d = set_end_value(s);
        GSERIALIZED *g = DatumGetGserializedP(d);
        size_t wkb_len = 0;
        uint8_t *wkb = geo_as_ewkb(g, "NDR", &wkb_len);
        
        string_t str = StringVector::AddString(result, std::string((char *)wkb, wkb_len));
        result_data[i] = str;		
        free(s);
        free(wkb);  
        
    }
}

// --- numValues ---
void SpatialSetFunctions::Set_num_values(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    input.Flatten(args.size());
    auto input_data = FlatVector::GetData<string_t>(input);
    auto result_data = FlatVector::GetData<int64_t>(result);

    for (idx_t i = 0; i < args.size(); i++) {
        if (FlatVector::IsNull(input, i)) {
            FlatVector::SetNull(result, i, true);
            continue;
        }

        const string_t &blob = input_data[i];
        const uint8_t *data = (const uint8_t *)blob.GetData();
        size_t size = blob.GetSize();		
        Set *s = (Set *)malloc(size);
        memcpy(s, data, size);

        int64_t num_values = set_num_values(s);
        result_data[i] = num_values;

        free(s);
    }
}

// --- valueN ---
void SpatialSetFunctions::Set_value_n(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    auto &n = args.data[1];

    input.Flatten(args.size());
    n.Flatten(args.size());

    auto input_data = FlatVector::GetData<string_t>(input);
    auto n_data = FlatVector::GetData<int64_t>(n);
    auto result_data = FlatVector::GetData<string_t>(result);

    for (idx_t i = 0; i < args.size(); i++) {
        if (FlatVector::IsNull(input, i) || FlatVector::IsNull(n, i)) {
            FlatVector::SetNull(result, i, true);
            continue;
        }

        const string_t &blob = input_data[i];
        const uint8_t *data = (const uint8_t *)blob.GetData();
        size_t size = blob.GetSize();		
        Set *s = (Set *)malloc(size);
        memcpy(s, data, size);

        Datum d;
        bool found = set_value_n(s, n_data[i], &d);
        GSERIALIZED *g = DatumGetGserializedP(d);
        size_t wkb_len = 0;
        uint8_t *wkb = geo_as_ewkb(g, "NDR", &wkb_len);
        
        string_t str = StringVector::AddString(result, std::string((char *)wkb, wkb_len));
        result_data[i] = str;		
        
        free(s);
        free(wkb);  
    }
}

} // namespace duckdb
