#include "pointcloud/tpcpoint.hpp"
#include "geo/tgeompoint_functions.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include <regex>
#include <string>
#include <temporal/span.hpp>
#include "temporal/spanset.hpp"
#include "temporal/set.hpp"
#include "temporal/temporal_functions.hpp"
#include "geo/stbox.hpp"
#include "geo/geoset.hpp"
#include<time_util.hpp>
#include "geo_util.hpp"
#include "spatial/spatial_types.hpp"
#include "mobilityduck/meos_exec_serial.hpp"
#include "tydef.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_geo.h>
    #include <meos_internal.h>
    #include <meos_internal_geo.h>
}

// The pgpointcloud module header meos_pointcloud.h is intentionally not
// included here.  MEOS exposes NO type-specific tpcpoint_* temporal
// entry points (no tpcpoint_in / tpcpoint_out / tpcpoint_from_mfjson);
// the canonical MobilityDB SQL binds tpcpoint_in / tpcpoint_out to the
// subtype-agnostic generic Temporal_* dispatch (temporal_in(str,
// T_TPCPOINT) / temporal_out).  Only the base pgpointcloud value type
// pcpoint has type-specific symbols, and just the schema-free ones are
// declared locally below (the same local-extern technique the sibling
// ports use).  Pcpoint is an opaque varlena (pgpointcloud
// SERIALIZED_POINT); the schema-aware coordinate accessors
// (pcpoint_get_x/y/z, getDim) read a value through the PCSCHEMA its pcid
// names, which a database states as rows and registers through
// pointCloudSchemaRegister (src/pointcloud/pcschema.cpp).
extern "C" {
    typedef struct Pcpoint Pcpoint;
    extern Pcpoint *pcpoint_hex_in(const char *str);
    extern char *pcpoint_hex_out(const Pcpoint *pt, int maxdd);
    extern uint32_t pcpoint_get_pcid(const Pcpoint *pt);
}


namespace duckdb {

LogicalType TPcpointTypes::pcpoint() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("pcpoint");
    return type;
}

LogicalType TPcpointTypes::tpcpoint() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("tpcpoint");
    return type;
}

/*
 * Constructors
*/

static void Tpcpoint_constructor(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_geom_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_geom_vec, result, count,
        [&](string_t input_geom_str) -> string_t {
            std::string input = input_geom_str.GetString();

            // No tpcpoint_in exists; the canonical SQL binds tpcpoint_in
            // to the generic Temporal_in, i.e. temporal_in(str,
            // T_TPCPOINT).
            Temporal *tinst = temporal_in(input.c_str(), T_TPCPOINT);
            if (!tinst) {
                throw InvalidInputException("Invalid tpcpoint input: " + input);
            }

            size_t data_size = temporal_mem_size(tinst);

            uint8_t *data_buffer = (uint8_t*)malloc(data_size);
            if (!data_buffer) {
                free(tinst);
                throw InvalidInputException("Failed to allocate memory for tpcpoint data");
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




/*
 * Conversions
*/

static void Temporal_to_tstzspan(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_geom_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_geom_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));

            if (!temp) {
                throw InvalidInputException("Invalid tpcpoint data: null pointer");
            }

            Span *timespan = temporal_to_tstzspan(temp);

            if (!timespan) {
                throw InvalidInputException("Failed to extract timespan from tpcpoint");
            }

            size_t span_size = sizeof(Span);

            uint8_t *span_buffer = (uint8_t*)malloc(span_size);
            if (!span_buffer) {
                free(timespan);
                throw InvalidInputException("Failed to allocate memory for timespan data");
            }

            memcpy(span_buffer, timespan, span_size);

            string_t span_string_t(reinterpret_cast<const char*>(span_buffer), span_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, span_string_t);

            free(span_buffer);
            free(timespan);

            return stored_data;
        }
    );

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}

/*
 * Transformations
*/

static void Temporal_to_tinstant(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_geom_vec = args.data[0];

    UnaryExecutor::Execute<string_t, string_t>(
        input_geom_vec, result, count,
        [&](string_t input_str) -> string_t {
            std::string input = input_str.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));

            if (!temp) {
                throw InvalidInputException("Invalid tpcpoint data: null pointer");
            }

            TInstant *inst = temporal_as_tinstant(temp);
            if (!inst) {
                throw InvalidInputException("Failed to convert tpcpoint to TInstant");
            }

            size_t inst_size = temporal_mem_size((Temporal*)inst);

            uint8_t *inst_buffer = (uint8_t*)malloc(inst_size);
            if (!inst_buffer) {
                free(inst);
                throw InvalidInputException("Failed to allocate memory for TInstant data");
            }

            memcpy(inst_buffer, inst, inst_size);

            string_t inst_string_t(reinterpret_cast<const char*>(inst_buffer), inst_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, inst_string_t);

            free(inst_buffer);
            free(inst);

            return stored_data;
        }
    );

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}


static void Temporal_set_interp(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &tgeom_vec = args.data[0];
    auto &interp_vec = args.data[1];

    tgeom_vec.Flatten(count);
    interp_vec.Flatten(count);

    BinaryExecutor::Execute<string_t, string_t, string_t>(
        tgeom_vec, interp_vec, result, count,
        [&](string_t tgeom_str_t, string_t interp_str_t) -> string_t {

            std::string input = tgeom_str_t.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));
            if (!temp) {
                throw InvalidInputException("Invalid tpcpoint data: null pointer");
            }

            std::string interp_str = interp_str_t.GetString();
            interpType new_interp = interptype_from_string(interp_str.c_str());

            Temporal *result_temp = temporal_set_interp(temp, new_interp);
            if (!result_temp) {
                throw InvalidInputException("Failed to set interpolation");
            }

            // Serialize result back to binary
            size_t result_size = temporal_mem_size(result_temp);
            uint8_t *result_buffer = (uint8_t*)malloc(result_size);
            if (!result_buffer) {
                free(result_temp);
                throw InvalidInputException("Failed to allocate memory for result");
            }

            memcpy(result_buffer, result_temp, result_size);
            string_t result_string_t(reinterpret_cast<const char*>(result_buffer), result_size);
            string_t stored_data = StringVector::AddStringOrBlob(result, result_string_t);

            free(result_buffer);
            free(result_temp);

            return stored_data;
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}



/*
 * Accessor Functions
*/




// ---- pcpoint value accessors ----
// The tpcpoint value type is a pgpointcloud pcpoint: an opaque varlena
// (pgpointcloud SERIALIZED_POINT) that is not a registered DuckDB type.
// getValue / startValue / endValue surface it in its canonical hex-WKB
// text form via the schema-free pcpoint_hex_out, mirroring tpcpoint_out.
// pcid(tpcpoint) returns the pgpointcloud schema id via the schema-free
// pcpoint_get_pcid.  The schema-aware coordinate accessors
// (getX/getY/getZ/getDim) are not bound: they need a registered
// PCSCHEMA, which a standalone DuckDB process does not have.

static Pcpoint *pcpoint_from_instant_value(const TInstant *inst) {
    Datum d = tinstant_value(inst);
    return reinterpret_cast<Pcpoint*>(d);
}



/* Temporal_start_value/Temporal_end_value retired: startValue/endValue are now
 * generated (return the pcpoint base value, rendered via the pcpoint->VARCHAR cast). */

/* Base pcpoint value (BLOB-alias) -> VARCHAR render cast, mirroring the cbuffer
 * sibling's Cbuffer_out_cast. The generated startValue/endValue return the pcpoint
 * base value; DuckDB renders it through this cast via the schema-free pcpoint_hex_out
 * (hex-WKB text), the same form the retired hand accessors produced. */
bool TpcpointFunctions::Pcpoint_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t s) -> string_t {
            std::string str(s.GetData(), s.GetSize());
            Pcpoint *pt = pcpoint_hex_in(str.c_str());
            if (!pt)
                throw InvalidInputException("Failed to read a pcpoint from text");
            string_t out = StringVector::AddStringOrBlob(result, (const char *)pt, VARSIZE(pt));
            free(pt);
            return out;
        });
    return true;
}

bool TpcpointFunctions::Pcpoint_out_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            size_t sz = blob.GetSize();
            uint8_t *copy = (uint8_t *)malloc(sz);
            memcpy(copy, blob.GetData(), sz);
            Pcpoint *pt = reinterpret_cast<Pcpoint *>(copy);
            char *str = pcpoint_hex_out(pt, 15);
            free(pt);
            if (!str)
                throw InvalidInputException("Failed to convert pcpoint value to text");
            std::string s(str);
            free(str);
            return StringVector::AddString(result, s);
        });
    return true;
}

static void Tpcpoint_pcid(DataChunk &args, ExpressionState &state, Vector &result) {
    auto count = args.size();
    auto &input_vec = args.data[0];

    UnaryExecutor::Execute<string_t, int32_t>(
        input_vec, result, count,
        [&](string_t input_str) -> int32_t {
            std::string input = input_str.GetString();

            Temporal *temp = reinterpret_cast<Temporal*>(const_cast<char*>(input.c_str()));

            // temporal_start_value returns a freshly allocated copy of
            // the pcpoint value (datum_copy), which the caller owns.
            Datum start_datum = temporal_start_value(temp);
            Pcpoint *pt = reinterpret_cast<Pcpoint*>(start_datum);

            // pcpoint_get_pcid is schema-free (reads the serialized
            // pcid field of the opaque pgpointcloud point).
            uint32_t pcid = pcpoint_get_pcid(pt);
            free(pt);
            return static_cast<int32_t>(pcid);
        });

    if (count == 1) {
        result.SetVectorType(VectorType::CONSTANT_VECTOR);
    }
}







void TPcpointTypes::RegisterScalarFunctions(ExtensionLoader &loader) {

    auto tpcpoint_function = ScalarFunction(
        "tpcpoint",
        {LogicalType::VARCHAR},
        TPcpointTypes::tpcpoint(),
        Tpcpoint_constructor
    );
    duckdb::RegisterSerializedScalarFunction(loader,  tpcpoint_function);

    auto tpcpoint_to_tinstant_function = ScalarFunction(
        "tpcpointInst",
        {TPcpointTypes::tpcpoint()},
        TPcpointTypes::tpcpoint(),
        Temporal_to_tinstant);
    duckdb::RegisterSerializedScalarFunction(loader,  tpcpoint_to_tinstant_function);







    /* getValue, startValue and endValue are now GENERATED (they return the pcpoint base value,
     * rendered as hex-WKB text via the pcpoint->VARCHAR cast), mirroring the cbuffer
     * sibling; the hand VARCHAR registrations are retired to avoid an ambiguous overload. */

    auto tpcpoint_pcid_function = ScalarFunction(
        "pcid",
        {TPcpointTypes::tpcpoint()},
        LogicalType::INTEGER,
        Tpcpoint_pcid
    );
    duckdb::RegisterSerializedScalarFunction(loader,  tpcpoint_pcid_function);






    // ===================================================================
    // Foundational tpcpoint surface — accessors, time/value-restrict,
    // modifiers, and comparison. The MEOS C functions delegated to here
    // are subtype-agnostic (they take Temporal *), so we reuse the same
    // generic handlers wired for tgeompoint in temporal_functions.cpp.
    // ===================================================================

    const LogicalType TGEOM = TPcpointTypes::tpcpoint();
    const LogicalType TSTZ  = LogicalType::TIMESTAMP_TZ;
    const LogicalType IVAL  = LogicalType::INTERVAL;

    // ---- Modifiers (shift / scale / shiftScale / append / insert / update /
    // delete) ----
    RegisterMeosFunction(loader, ScalarFunction(
        "shiftScaleTime", {TGEOM, IVAL, IVAL}, TGEOM,
        TemporalFunctions::Temporal_shift_scale_time));

    // ---- Comparison operators (eq, ne, lt, le, gt and ge are generated) ----
    struct CmpEntry {
        const char *name;
        scalar_function_t fn;
    };

    // Operator forms — mirror the registrations tgeometry.cpp does.
    const std::vector<CmpEntry> op_cmps = {
        {"=",  TemporalFunctions::Temporal_eq},
        {"<>", TemporalFunctions::Temporal_ne},
        {"<",  TemporalFunctions::Temporal_lt},
        {"<=", TemporalFunctions::Temporal_le},
        {">",  TemporalFunctions::Temporal_gt},
        {">=", TemporalFunctions::Temporal_ge},
    };
    for (const auto &c : op_cmps) {
        RegisterMeosFunction(loader, ScalarFunction(
            c.name, {TGEOM, TGEOM}, LogicalType::BOOLEAN, c.fn));
    }
}

void TPcpointTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType( "pcpoint", TPcpointTypes::pcpoint());
    loader.RegisterType( "tpcpoint", TPcpointTypes::tpcpoint());
}


}
