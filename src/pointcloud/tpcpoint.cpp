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








/*
 * Accessor Functions
*/






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

    // tpcpointInst(tpcpoint) and shiftScaleTime(tpcpoint, interval, interval) are generated from
    // the catalog (meos_temporal_transf).
}

void TPcpointTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType( "pcpoint", TPcpointTypes::pcpoint());
    loader.RegisterType( "tpcpoint", TPcpointTypes::tpcpoint());
}


}
