#include "meos_wrapper_simple.hpp"
#include "duckdb_version_compat.hpp"

#include "common.hpp"
#include "geo/tgeogpoint.hpp"
#include "geo/tgeogpoint_functions.hpp"
#include "geo/tgeompoint_functions.hpp"
#include "geo/geoset.hpp"
#include "temporal/temporal_functions.hpp"
#include "geo/stbox.hpp"
#include "temporal/spanset.hpp"
#include "temporal/temporal.hpp"
#include "temporal/set.hpp"
#include "temporal/span.hpp"

#include "duckdb/common/types/blob.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "spatial/spatial_types.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

#include "duckdb/common/extension_type_info.hpp"
#include <regex>
#include <string>
#include "geo_util.hpp"
#include "time_util.hpp"

extern "C" {
    #include <meos_internal.h>
    #include <meos_internal_geo.h>
}

namespace duckdb {

LogicalType TgeogpointType::tgeogpoint() {
    LogicalType type(LogicalTypeId::BLOB);
    type.SetAlias("tgeogpoint");
    return type;
}

void TgeogpointType::RegisterType(ExtensionLoader &loader) {
    loader.RegisterType("tgeogpoint", tgeogpoint());
}

void TgeogpointType::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, 
        LogicalType::VARCHAR,
        tgeogpoint(),
        TgeogpointFunctions::Tpoint_in
    );

    RegisterMeosCastFunction(loader, 
        tgeogpoint(),
        LogicalType::VARCHAR,
        TemporalFunctions::Temporal_out
    );

    RegisterMeosCastFunction(loader, 
        tgeogpoint(),
        StboxType::stbox(),
        TgeompointFunctions::Tspatial_to_stbox_cast
    );

    RegisterMeosCastFunction(loader, 
        tgeogpoint(),
        SpanTypes::tstzspan(),
        TgeompointFunctions::Temporal_to_tstzspan_cast
    );
}

void TgeogpointType::RegisterScalarFunctions(ExtensionLoader &loader) {

    /* ***************************************************
     * In/out functions
     ****************************************************/

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asText",
            {tgeogpoint()},
            LogicalType::VARCHAR,
            TgeompointFunctions::Tspatial_as_text
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asEWKT",
            {tgeogpoint()},
            LogicalType::VARCHAR,
            TgeompointFunctions::Tspatial_as_ewkt
        )
    );

    const auto varchar_list = LogicalType::LIST(LogicalType::VARCHAR);

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asText",
            {LogicalType::LIST(tgeogpoint())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asText",
            {LogicalType::LIST(tgeogpoint()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_text
        )
    );

    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(tgeogpoint())},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );
    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "asEWKT",
            {LogicalType::LIST(tgeogpoint()), LogicalType::INTEGER},
            varchar_list,
            TgeompointFunctions::Spatialarr_as_ewkt
        )
    );


    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "stbox",
            {tgeogpoint()},
            StboxType::stbox(),
            TgeompointFunctions::Tspatial_to_stbox
        )
    );

    /* ***************************************************
     * Conversion functions
     ****************************************************/
    // timeSpan(tgeogpoint) (temporal_to_tstzspan, group meos_temporal_conversion) is
    // generated from the catalog in generated_temporal_udfs.cpp (RETIRED_GROUPS).

    /***************************************************
     * Transformation functions
     ****************************************************/











    /* ***************************************************
    * Accessor functions
    ****************************************************/




    // timestamps(tgeogpoint) is generated from the catalog (temporal_timestamps) in generated_temporal_udfs.cpp.

    /* ***************************************************
     * Shift and Scale functions
     ****************************************************/


    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "shiftScaleTime",
            {tgeogpoint(), LogicalType::INTERVAL, LogicalType::INTERVAL},
            tgeogpoint(),
            TemporalFunctions::Temporal_shift_scale_time
        )
    );

    /* ***************************************************
     * Restriction functions
     ****************************************************/


    /* ***************************************************
     * Modification function
     ****************************************************/












    /* ***************************************************
     * Stops function
     ****************************************************/

    /* ***************************************************
     * Spatial functions
     ****************************************************/










    duckdb::RegisterSerializedScalarFunction(loader,
        ScalarFunction(
            "makeSimple",
            {tgeogpoint()},
            LogicalType::LIST(tgeogpoint()),
            TgeompointFunctions::Tpoint_make_simple
        )
    );

    // round(tgeogpoint) at both arities is generated from the catalog temporal_round
    // signature (generated_temporal_udfs.cpp); no hand registration remains.

}

/* ***************************************************
 * Round-trip I/O for tgeogpoint: asEWKB / asHexWKB / asHexEWKB /
 * asMFJSON and the matching tgeogpointFromText / FromBinary / FromEWKB /
 * FromHexWKB / FromHexEWKB / FromMFJSON constructors.
 ****************************************************/

namespace {

constexpr uint8_t GEOG_WKB_BASE = 0x00;

inline Temporal *GeogBlobToTemp(string_t b) {
    size_t sz = b.GetSize();
    uint8_t *copy = (uint8_t *)malloc(sz);
    memcpy(copy, b.GetData(), sz);
    return reinterpret_cast<Temporal *>(copy);
}

string_t GeogTempToResultBlob(Vector &result, Temporal *t) {
    size_t sz = temporal_mem_size(t);
    string_t blob(reinterpret_cast<const char *>(t), sz);
    return StringVector::AddStringOrBlob(result, blob);
}

void TgeogAsWkbExec(DataChunk &args, ExpressionState &state, Vector &result, uint8_t variant) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            Temporal *t = GeogBlobToTemp(input);
            size_t sz = 0;
            uint8_t *wkb = temporal_as_wkb(t, variant, &sz);
            free(t);
            if (!wkb || sz == 0) {
                if (wkb) free(wkb);
                throw InternalException("temporal_as_wkb returned null");
            }
            string_t blob(reinterpret_cast<const char *>(wkb), sz);
            string_t stored = StringVector::AddStringOrBlob(result, blob);
            free(wkb);
            return stored;
        });
}

void TgeogAsHexWkbExec(DataChunk &args, ExpressionState &state, Vector &result, uint8_t variant) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            Temporal *t = GeogBlobToTemp(input);
            size_t sz = 0;
            char *hex = temporal_as_hexwkb(t, variant, &sz);
            (void) sz;
            free(t);
            if (!hex) throw InternalException("temporal_as_hexwkb returned null");
            string_t stored = StringVector::AddString(result, hex);
            free(hex);
            return stored;
        });
}

void TgeogFromWkbExec(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            if (input.GetSize() == 0) throw InvalidInputException("Empty WKB input");
            uint8_t *wkb = (uint8_t *)malloc(input.GetSize());
            memcpy(wkb, input.GetData(), input.GetSize());
            Temporal *t = temporal_from_wkb(wkb, input.GetSize());
            free(wkb);
            if (!t) throw InvalidInputException("temporal_from_wkb: invalid WKB");
            string_t stored = GeogTempToResultBlob(result, t);
            free(t);
            return stored;
        });
}

void TgeogFromHexWkbExec(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string hex(input.GetData(), input.GetSize());
            Temporal *t = temporal_from_hexwkb(hex.c_str());
            if (!t) throw InvalidInputException("temporal_from_hexwkb: invalid hex-WKB");
            string_t stored = GeogTempToResultBlob(result, t);
            free(t);
            return stored;
        });
}

void TgeogAsMfjsonExec(DataChunk &args, ExpressionState &state, Vector &result) {
    const idx_t row_count = args.size();
    for (idx_t i = 0; i < args.ColumnCount(); i++) args.data[i].Flatten(row_count);
    auto in = FlatVector::GetData<string_t>(args.data[0]);
    auto out_data = FlatVector::GetData<string_t>(result);
    auto &out_validity = FlatVector::Validity(result);
    const idx_t cc = args.ColumnCount();
    for (idx_t row = 0; row < row_count; row++) {
        if (!FlatVector::Validity(args.data[0]).RowIsValid(row)) {
            out_validity.SetInvalid(row);
            continue;
        }
        Temporal *t = GeogBlobToTemp(in[row]);
        bool with_bbox = (cc > 1) ? FlatVector::GetData<bool>(args.data[1])[row] : false;
        int flags = (cc > 2) ? FlatVector::GetData<int32_t>(args.data[2])[row] : 0;
        int precision = (cc > 3) ? FlatVector::GetData<int32_t>(args.data[3])[row] : 15;
        std::string srs;
        const char *srs_cstr = nullptr;
        if (cc > 4) {
            string_t s = FlatVector::GetData<string_t>(args.data[4])[row];
            srs.assign(s.GetData(), s.GetSize());
            srs_cstr = srs.empty() ? nullptr : srs.c_str();
        }
        char *json = temporal_as_mfjson(t, with_bbox ? 1 : 0, flags, precision, srs_cstr);
        free(t);
        if (!json) {
            out_validity.SetInvalid(row);
            continue;
        }
        out_data[row] = StringVector::AddString(result, json);
        free(json);
    }
    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
}

void TgeogFromMfjsonExec(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            Temporal *t = tgeogpoint_from_mfjson(s.c_str());
            if (!t) throw InvalidInputException("tgeogpoint_from_mfjson: invalid MFJSON");
            string_t stored = GeogTempToResultBlob(result, t);
            free(t);
            return stored;
        });
}

void TgeogFromTextExec(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t input) -> string_t {
            std::string s(input.GetData(), input.GetSize());
            Temporal *t = tgeogpoint_in(s.c_str());
            if (!t) throw InvalidInputException("tgeogpoint_in: invalid text");
            string_t stored = GeogTempToResultBlob(result, t);
            free(t);
            return stored;
        });
}

} // namespace

void TgeogpointType::RegisterRoundtripIO(ExtensionLoader &loader) {
    const auto T = tgeogpoint();
    const auto V = LogicalType::VARCHAR;
    const auto B = LogicalType::BLOB;
    const auto BL = LogicalType::BOOLEAN;
    const auto I = LogicalType::INTEGER;

    /* asBinary / asEWKB */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asBinary", {T}, B,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TgeogAsWkbExec(a, s, r, GEOG_WKB_BASE); }));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asEWKB", {T}, B,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TgeogAsWkbExec(a, s, r, WKB_EXTENDED); }));

    /* asHexEWKB (extended hex-WKB, includes SRID). asHexWKB (base) is generated from the
     * catalog as the inherited Temporal<T> output surface (generated_temporal_udfs.cpp). */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asHexEWKB", {T}, V,
        [](DataChunk &a, ExpressionState &s, Vector &r) { TgeogAsHexWkbExec(a, s, r, WKB_EXTENDED); }));

    /* asMFJSON: 1..5 arg overloads */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMFJSON", {T},              V, TgeogAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMFJSON", {T, BL},           V, TgeogAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMFJSON", {T, BL, I},        V, TgeogAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMFJSON", {T, BL, I, I},     V, TgeogAsMfjsonExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("asMFJSON", {T, BL, I, I, V},  V, TgeogAsMfjsonExec));

    /* tgeogpointFromText / FromEWKT */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeogpointFromText", {V}, T, TgeogFromTextExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeogpointFromEWKT", {V}, T, TgeogFromTextExec));

    /* tgeogpointFromBinary / FromEWKB — WKB carries type tag */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeogpointFromBinary", {B}, T, TgeogFromWkbExec));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeogpointFromEWKB",   {B}, T, TgeogFromWkbExec));

    /* tgeogpointFromHexWKB / FromHexEWKB */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction("tgeogpointFromHexWKB",  {V}, T, TgeogFromHexWkbExec));

    /* tgeogpointFromMFJSON */
}

// ============================================================
// TGeogpointType (new full-parity surface)
// ============================================================

LogicalType TGeogpointType::tgeogpoint() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("tgeogpoint");
    return type;
}

/*
 * Constructors
*/

inline void Tgeogpoint_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_geom_vec = args.data[0];
    
    UnaryExecutor::Execute<string_t, string_t>(
        input_geom_vec, result, count, 
        [&](string_t input_geom_str) -> string_t {
            std::string input = input_geom_str.GetString();
            
            Temporal *tinst = tgeogpoint_in(input.c_str());
            if (!tinst) {
                throw InvalidInputException("Invalid tgeogpoint input: " + input);
            }
            
            size_t data_size = temporal_mem_size(tinst);
            
            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(tinst);  
                throw InvalidInputException("Failed to allocate memory for tgeogpoint data");
            }
            
            memcpy(data_buffer, tinst, data_size);
            
            string_t data_string_t(reinterpret_cast<const char*>(data_buffer), data_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, data_string_t);
            
            free(data_buffer);
            free(tinst);  
            
            return stored_data;
        });
    
    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

TInstant **temparr_extract_gp(Vector &tgeogpoint_arr_vec, list_entry_t list_entry, int *count) {
    auto &child_vector = ListVector::GetEntry(tgeogpoint_arr_vec);
    auto list_size = list_entry.length;
    auto list_offset = list_entry.offset;
    
    if (list_size == 0) {
        *count = 0;
        return nullptr;
    }
    
    *count = list_size;
    
    TInstant **instants = (TInstant**)malloc(sizeof(TInstant*) * list_size);
    if (!instants) {
        *count = 0;
        return nullptr;
    }
    
    for (idx_t i = 0; i < list_size; i++) {
        auto element_idx = list_offset + i;
        string_t tgeom_blob = FlatVector::GetData<string_t>(child_vector)[element_idx];
        
        const uint8_t *data = reinterpret_cast<const uint8_t*>(tgeom_blob.GetData());
        size_t data_size = tgeom_blob.GetSize();
        
        if (data_size < sizeof(void*)) {
            for (idx_t j = 0; j < i; j++) {
                if (instants[j]) free(instants[j]);
            }
            free(instants);
            *count = 0;
            return nullptr;
        }
        
        uint8_t *data_copy = (uint8_t*)malloc(data_size);
        if (!data_copy) {
            for (idx_t j = 0; j < i; j++) {
                if (instants[j]) free(instants[j]);
            }
            free(instants);
            *count = 0;
            return nullptr;
        }
        memcpy(data_copy, data, data_size);
        
        Temporal *temp = reinterpret_cast<Temporal*>(data_copy);
        if (!temp) {
            free(data_copy);
            for (idx_t j = 0; j < i; j++) {
                if (instants[j]) free(instants[j]);
            }
            free(instants);
            *count = 0;
            return nullptr;
        }
        
        instants[i] = (TInstant*)temp;
    }
    
    return instants;
}







/*
 * Accessor Functions
*/















void TGeogpointType::RegisterScalarFunctions(ExtensionLoader &loader) {

    auto tgeogpoint_function = ScalarFunction(
        "tgeogpoint", 
        {LogicalType::VARCHAR}, 
        TGeogpointType::tgeogpoint(),
        Tgeogpoint_constructor
    );
    RegisterMeosFunction(loader, tgeogpoint_function);
        
    // timeSpan(tgeogpoint) is generated (meos_temporal_conversion, RETIRED_GROUPS).





    




    // ===================================================================
    // Foundational tgeogpoint surface — accessors, time/value-restrict,
    // modifiers, and comparison. The MEOS C functions delegated to here
    // are subtype-agnostic (they take Temporal *), so we reuse the same
    // generic handlers wired for tgeompoint in temporal_functions.cpp.
    // ===================================================================

    const LogicalType TGEOM = TGeogpointType::tgeogpoint();
    const LogicalType GEOM  = MobilityDuckGeometryType();
    const LogicalType TSTZ  = LogicalType::TIMESTAMP_TZ;
    const LogicalType IVAL  = LogicalType::INTERVAL;
    const LogicalType BIGI  = LogicalType::BIGINT;

    // ---- Accessors ----
    // timestamps(tgeogpoint) is generated from the catalog (temporal_timestamps) in generated_temporal_udfs.cpp.
    // ---- Modifiers (shift / scale / shiftScale / append / insert / update /
    // delete / round) ----
    RegisterMeosFunction(loader, ScalarFunction(
        "shiftScaleTime", {TGEOM, IVAL, IVAL}, TGEOM,
        TemporalFunctions::Temporal_shift_scale_time));
}

void TGeogpointType::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType( "tgeogpoint", TGeogpointType::tgeogpoint());
}


} // namespace duckdb
