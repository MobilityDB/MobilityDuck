#include "pointcloud/tpcpatch.hpp"
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
#include "duckdb_version_compat.hpp"
#include "mobilityduck/meos_registration.hpp"
#include "tydef.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_geo.h>
    #include <meos_internal.h>
    #include <meos_internal_geo.h>
}

// The pgpointcloud module header meos_pointcloud.h is intentionally not
// included here.  MEOS exposes NO type-specific tpcpatch_* temporal
// entry points (no tpcpatch_in / tpcpatch_out / tpcpatch_from_mfjson);
// the canonical MobilityDB SQL binds tpcpatch_in / tpcpatch_out to the
// subtype-agnostic generic Temporal_* dispatch (temporal_in(str,
// T_TPCPATCH) / temporal_out).  Only the base pgpointcloud value type
// pcpatch has type-specific symbols, and just the schema-free ones are
// declared locally below (the same local-extern technique the sibling
// ports use).  Pcpatch is an opaque varlena (pgpointcloud
// SERIALIZED_PATCH); the schema-aware coordinate accessors
// (atGeometry, eIntersects, the per-dimension getters) require a
// registered PCSCHEMA from the pgpointcloud catalog, which is not
// available in a standalone DuckDB context, so they are deliberately
// not bound.
extern "C" {
    typedef struct Pcpatch Pcpatch;
    extern Pcpatch *pcpatch_hex_in(const char *str);
    extern char *pcpatch_hex_out(const Pcpatch *pt, int maxdd);
}


namespace duckdb {

LogicalType TPcpatchTypes::pcpatch() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("pcpatch");
    return type;
}

LogicalType TPcpatchTypes::tpcpatch() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("tpcpatch");
    return type;
}

/*
 * Constructors
*/








/*
 * Accessor Functions
*/






/* Temporal_start_value/Temporal_end_value retired: startValue/endValue are now
 * generated (return the pcpatch base value, rendered via the pcpatch->VARCHAR cast). */

/* Base pcpatch value (BLOB-alias) -> VARCHAR render cast, mirroring the cbuffer
 * sibling's Cbuffer_out_cast. The generated startValue/endValue return the pcpatch
 * base value; DuckDB renders it through this cast via the schema-free pcpatch_hex_out
 * (hex-WKB text), the same form the retired hand accessors produced. */
bool TpcpatchFunctions::Pcpatch_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t s) -> string_t {
            std::string str(s.GetData(), s.GetSize());
            Pcpatch *pt = pcpatch_hex_in(str.c_str());
            if (!pt)
                throw InvalidInputException("Failed to read a pcpatch from text");
            string_t out = StringVector::AddStringOrBlob(result, (const char *)pt, VARSIZE(pt));
            free(pt);
            return out;
        });
    return true;
}

bool TpcpatchFunctions::Pcpatch_out_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            size_t sz = blob.GetSize();
            uint8_t *copy = (uint8_t *)malloc(sz);
            memcpy(copy, blob.GetData(), sz);
            Pcpatch *pt = reinterpret_cast<Pcpatch *>(copy);
            char *str = pcpatch_hex_out(pt, 15);
            free(pt);
            if (!str)
                throw InvalidInputException("Failed to convert pcpatch value to text");
            std::string s(str);
            free(str);
            return StringVector::AddString(result, s);
        });
    return true;
}







void TPcpatchTypes::RegisterScalarFunctions(ExtensionLoader &loader) {







    /* getValue, startValue and endValue are now GENERATED (they return the pcpatch base value,
     * rendered as hex-WKB text via the pcpatch->VARCHAR cast), mirroring the cbuffer
     * sibling; the hand VARCHAR registrations are retired to avoid an ambiguous overload. */

    // tpcpatchInst(tpcpatch) and shiftScaleTime(tpcpatch, interval, interval) are generated from
    // the catalog (meos_temporal_transf).
}

void TPcpatchTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType( "pcpatch", TPcpatchTypes::pcpatch());
    loader.RegisterType( "tpcpatch", TPcpatchTypes::tpcpatch());
}


}
