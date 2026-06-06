#include "geo/geoset.hpp"
#include "temporal/set_functions.hpp"
#include "tydef.hpp"
#include "geo_util.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "spatial/spatial_types.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

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
 
}

void SpatialSetType::RegisterScalarFunctions(ExtensionLoader &loader) {	    
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
		"asText", 
        {SpatialSetType::geomset()}, LogicalType::VARCHAR, SpatialSetFunctions::Spatialset_as_text));
    
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
		"asText", 
        {SpatialSetType::geogset()}, LogicalType::VARCHAR, SpatialSetFunctions::Spatialset_as_text));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
		"asText", 
        {SpatialSetType::geomset(), LogicalType::INTEGER}, LogicalType::VARCHAR, SpatialSetFunctions::Spatialset_as_text));
    
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
		"asText", 
        {SpatialSetType::geogset(), LogicalType::INTEGER}, LogicalType::VARCHAR, SpatialSetFunctions::Spatialset_as_text));

        duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
		"asEWKT", 
        {SpatialSetType::geomset()}, LogicalType::VARCHAR, SpatialSetFunctions::Spatialset_as_ewkt));
    
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
		"asEWKT", 
        {SpatialSetType::geogset()}, LogicalType::VARCHAR, SpatialSetFunctions::Spatialset_as_ewkt));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
		"asEWKT", 
        {SpatialSetType::geomset(), LogicalType::INTEGER}, LogicalType::VARCHAR, SpatialSetFunctions::Spatialset_as_ewkt));
    
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
		"asEWKT", 
        {SpatialSetType::geogset(), LogicalType::INTEGER}, LogicalType::VARCHAR, SpatialSetFunctions::Spatialset_as_ewkt));    

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
        "memSize", 
        {SpatialSetType::geomset()}, LogicalType::INTEGER, SpatialSetFunctions::Set_mem_size));
    
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
        "memSize", 
        {SpatialSetType::geogset()}, LogicalType::INTEGER, SpatialSetFunctions::Set_mem_size));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
        "SRID", 
        {SpatialSetType::geomset()}, LogicalType::INTEGER, SpatialSetFunctions::Spatialset_srid));
    
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
        "SRID", 
        {SpatialSetType::geogset()}, LogicalType::INTEGER, SpatialSetFunctions::Spatialset_srid));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
		"setSRID", 
        {SpatialSetType::geomset(), LogicalType::INTEGER}, SpatialSetType::geomset(), SpatialSetFunctions::Spatialset_set_srid));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
		"setSRID",
        {SpatialSetType::geogset(), LogicalType::INTEGER}, SpatialSetType::geogset(), SpatialSetFunctions::Spatialset_set_srid));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
		"transform", 
        {SpatialSetType::geomset(), LogicalType::INTEGER}, SpatialSetType::geomset(), SpatialSetFunctions::Spatialset_transform));
    
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
		"transform",
        {SpatialSetType::geogset(), LogicalType::INTEGER}, SpatialSetType::geogset(), SpatialSetFunctions::Spatialset_transform));

    // transformPipeline(<geomset|geogset>, pipeline text, srid int = 0,
    //                   is_forward bool = true)
    for (auto &set_type : {SpatialSetType::geomset(), SpatialSetType::geogset()}) {
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
            "transformPipeline",
            {set_type, LogicalType::VARCHAR},
            set_type, SpatialSetFunctions::Spatialset_transform_pipeline));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
            "transformPipeline",
            {set_type, LogicalType::VARCHAR, LogicalType::INTEGER},
            set_type, SpatialSetFunctions::Spatialset_transform_pipeline));
        duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
            "transformPipeline",
            {set_type, LogicalType::VARCHAR, LogicalType::INTEGER, LogicalType::BOOLEAN},
            set_type, SpatialSetFunctions::Spatialset_transform_pipeline));
    }

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
		"startValue", {SpatialSetType::geomset()},
		GeoTypes::GEOMETRY(),
		SpatialSetFunctions::Set_start_value
	));    

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
        "endValue", 
        {SpatialSetType::geomset()}, GeoTypes::GEOMETRY(), SpatialSetFunctions::Set_end_value));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
        "numValues",
        {SpatialSetType::geomset()}, LogicalType::INTEGER, SpatialSetFunctions::Set_num_values));
    
    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
        "numValues",
        {SpatialSetType::geogset()}, LogicalType::INTEGER, SpatialSetFunctions::Set_num_values));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
        "valueN", {SpatialSetType::geomset(), LogicalType::INTEGER},
        GeoTypes::GEOMETRY(),
        SpatialSetFunctions::Set_value_n
    ));

    duckdb::RegisterSerializedScalarFunction(loader,  ScalarFunction(
        "set", {LogicalType::LIST(GeoTypes::GEOMETRY())},
        SpatialSetType::geomset(),
        SpatialSetFunctions::Geomset_constructor
    ));

    // Binary / EWKB / HexWKB / Text / EWKT parsers — route to the
    // subtype-agnostic MEOS `set_from_wkb` / `set_from_hexwkb` /
    // `set_in` dispatchers.  The format encodes (or the caller-side
    // basetype dictates) the target type.
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "geomsetFromBinary", {LogicalType::BLOB},    SpatialSetType::geomset(), SetFunctions::Set_from_binary));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "geomsetFromEWKB",   {LogicalType::BLOB},    SpatialSetType::geomset(), SetFunctions::Set_from_binary));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "geomsetFromHexWKB", {LogicalType::VARCHAR}, SpatialSetType::geomset(), SetFunctions::Set_from_hexwkb));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "geomsetFromText",   {LogicalType::VARCHAR}, SpatialSetType::geomset(), SpatialSetFunctions::Geomset_from_text));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "geomsetFromEWKT",   {LogicalType::VARCHAR}, SpatialSetType::geomset(), SpatialSetFunctions::Geomset_from_text));

    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "geogsetFromBinary", {LogicalType::BLOB},    SpatialSetType::geogset(), SetFunctions::Set_from_binary));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "geogsetFromEWKB",   {LogicalType::BLOB},    SpatialSetType::geogset(), SetFunctions::Set_from_binary));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "geogsetFromHexWKB", {LogicalType::VARCHAR}, SpatialSetType::geogset(), SetFunctions::Set_from_hexwkb));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "geogsetFromText",   {LogicalType::VARCHAR}, SpatialSetType::geogset(), SpatialSetFunctions::Geogset_from_text));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "geogsetFromEWKT",   {LogicalType::VARCHAR}, SpatialSetType::geogset(), SpatialSetFunctions::Geogset_from_text));

    // asBinary / asHexWKB for geomset / geogset — output side of the
    // I/O round-trip.  `set_as_wkb` / `set_as_hexwkb` are
    // subtype-agnostic; the format encodes the source basetype.
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "asBinary", {SpatialSetType::geomset()}, LogicalType::BLOB, SetFunctions::Set_as_binary));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "asBinary", {SpatialSetType::geogset()}, LogicalType::BLOB, SetFunctions::Set_as_binary));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "asHexWKB", {SpatialSetType::geomset()}, LogicalType::VARCHAR, SetFunctions::Set_as_hexwkb));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "asHexWKB", {SpatialSetType::geogset()}, LogicalType::VARCHAR, SetFunctions::Set_as_hexwkb));
}

// --- Constructor: set(LIST(GEOMETRY)) -> geomset ---
void SpatialSetFunctions::Geomset_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_input = args.data[0];

    UnaryExecutor::Execute<list_entry_t, string_t>(
        list_input, result, args.size(),
        [&](list_entry_t list_entry) -> string_t {
            auto &child = ListVector::GetEntry(list_input);
            child.Flatten(args.size());

            idx_t offset = list_entry.offset;
            idx_t length = list_entry.length;

            if (length == 0) {
                throw InvalidInputException("The input array cannot be empty");
            }

            GSERIALIZED **values = (GSERIALIZED **)malloc(sizeof(GSERIALIZED *) * length);
            for (idx_t i = 0; i < length; ++i) {
                idx_t idx = offset + i;
                string_t blob = FlatVector::GetData<string_t>(child)[idx];
                values[i] = GeometryToGSerialized(blob, 0);
            }

            Set *s = geoset_make(values, (int)length);
            for (idx_t i = 0; i < length; ++i) {
                free(values[i]);
            }
            free(values);

            if (!s) {
                throw InvalidInputException("Failed to construct geomset");
            }
            size_t size = set_mem_size(s);
            string_t blob = StringVector::AddStringOrBlob(result, (const char *)s, size);
            free(s);
            return blob;
        });
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

// --- WKT/EWKT parsers ---
// `geomsetFromText` / `geomsetFromEWKT` route here when the result type
// is geomset; `geogsetFromText` / `geogsetFromEWKT` route via the
// geogset variant.  `set_in` is the MEOS dispatcher that handles both
// WKT and EWKT input for spatial-set basetypes.

namespace {

inline void GeosetFromTextImpl(DataChunk &args, Vector &result, meosType basetype, const char *func_name) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            Set *r = set_in(s.c_str(), basetype);
            if (!r) {
                throw InvalidInputException(std::string(func_name) + ": invalid input");
            }
            size_t sz = set_mem_size(r);
            string_t stored = StringVector::AddStringOrBlob(
                result, string_t(reinterpret_cast<const char *>(r), sz));
            free(r);
            return stored;
        });
}

} // namespace

void SpatialSetFunctions::Geomset_from_text(DataChunk &args, ExpressionState &state, Vector &result) {
    GeosetFromTextImpl(args, result, T_GEOMSET, "geomsetFromText/EWKT");
}

void SpatialSetFunctions::Geogset_from_text(DataChunk &args, ExpressionState &state, Vector &result) {
    GeosetFromTextImpl(args, result, T_GEOGSET, "geogsetFromText/EWKT");
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

/* transformPipeline(<spatial-set>, pipeline text, srid int = 0,
 *                    is_forward bool = true)
 * Apply a PROJ pipeline string to every element of the spatial set.
 */
void SpatialSetFunctions::Spatialset_transform_pipeline(DataChunk &args, ExpressionState &state, Vector &result_vec) {
    const idx_t row_count = args.size();
    for (idx_t i = 0; i < args.ColumnCount(); i++) args.data[i].Flatten(row_count);
    const idx_t cc = args.ColumnCount();
    auto in_set = FlatVector::GetData<string_t>(args.data[0]);
    auto in_pipe = FlatVector::GetData<string_t>(args.data[1]);
    auto &v0 = FlatVector::Validity(args.data[0]);
    auto &v1 = FlatVector::Validity(args.data[1]);
    auto out_data = FlatVector::GetData<string_t>(result_vec);
    auto &out_validity = FlatVector::Validity(result_vec);
    for (idx_t row = 0; row < row_count; row++) {
        if (!v0.RowIsValid(row) || !v1.RowIsValid(row)) {
            out_validity.SetInvalid(row);
            continue;
        }
        size_t sz = in_set[row].GetSize();
        Set *s = (Set *) malloc(sz);
        memcpy(s, in_set[row].GetData(), sz);
        int32_t srid = (cc > 2) ? FlatVector::GetData<int32_t>(args.data[2])[row] : 0;
        bool is_fwd = (cc > 3) ? FlatVector::GetData<bool>(args.data[3])[row]    : true;
        std::string pipe = in_pipe[row].GetString();
        Set *ret = spatialset_transform_pipeline(s, pipe.c_str(), srid, is_fwd);
        free(s);
        if (!ret) {
            out_validity.SetInvalid(row);
            continue;
        }
        size_t rsz = set_mem_size(ret);
        out_data[row] = StringVector::AddStringOrBlob(result_vec, (const char *) ret, rsz);
        free(ret);
    }
    if (row_count == 1) result_vec.SetVectorType(VectorType::CONSTANT_VECTOR);
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
        string_t geometry_blob = GSerializedToGeometry(g, state, result);
        string_t str = StringVector::AddStringOrBlob(result, geometry_blob);
		result_data[i] = str;
		free(g);
		free(s);
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
        string_t geometry_blob = GSerializedToGeometry(g, state, result);
        string_t str = StringVector::AddStringOrBlob(result, geometry_blob);
        result_data[i] = str;
        free(g);
        free(s);
    }
}

// --- numValues ---
void SpatialSetFunctions::Set_num_values(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &input = args.data[0];
    input.Flatten(args.size());
    auto input_data = FlatVector::GetData<string_t>(input);
    // numValues is registered as returning INTEGER (INT32); DuckDB 1.4 asserts
    // the result Vector's template type matches the declared type.
    auto result_data = FlatVector::GetData<int32_t>(result);

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
    // `valueN` is registered with second argument LogicalType::INTEGER.
    auto n_data = FlatVector::GetData<int32_t>(n);
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
        if (!found) {
            free(s);
            FlatVector::SetNull(result, i, true);
            continue;
        }
        GSERIALIZED *g = DatumGetGserializedP(d);
        string_t geometry_blob = GSerializedToGeometry(g, state, result);
        string_t str = StringVector::AddStringOrBlob(result, geometry_blob);
        result_data[i] = str;
        free(g);
        free(s);
    }
}

} // namespace duckdb
