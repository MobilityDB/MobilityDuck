/* MobilityDuck binding for the MEOS raster tile (`meos_raster.h`): the `raquet`
 * Web-Mercator raster tile addressed by a QUADBIN cell.
 *
 * RAQUET is stored as a BLOB holding the MEOS WKB serialisation of the tile,
 * so a tile survives a Parquet round trip and matches the hex-encoded text
 * form produced by the VARCHAR cast.
 *
 * Only the type registration, its text casts and the tile constructor are
 * hand-written here; the operation surface is generated from the MEOS-API
 * catalog into src/generated/.
 */

#include "raster/raquet.hpp"
#include "quadbin/tquadbin.hpp"
#include "tydef.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_internal.h>
    #include <meos_raster.h>
}

#include <cstdlib>
#include <cstring>
#include <string>

namespace duckdb {

LogicalType RaquetTypes::raquet() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("raquet");
    return type;
}

void RaquetTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType("raquet", raquet());
}

namespace {

/* The Raquet structure is opaque to callers, so a tile travels as its WKB
 * serialisation rather than as raw bytes of the structure. */
inline Raquet *BlobToRaquet(string_t blob) {
    Raquet *rq = raquet_from_wkb(
        reinterpret_cast<const uint8_t *>(blob.GetData()), blob.GetSize());
    if (!rq) {
        throw InvalidInputException("raquet: invalid tile serialisation");
    }
    return rq;
}

inline string_t RaquetToBlob(Vector &result, Raquet *rq) {
    size_t sz = 0;
    uint8_t *wkb = raquet_as_wkb(rq, 0, &sz);
    free(rq);
    if (!wkb) {
        throw InvalidInputException("raquet: cannot serialise tile");
    }
    string_t out = StringVector::AddStringOrBlob(
        result, string_t(reinterpret_cast<const char *>(wkb), sz));
    free(wkb);
    return out;
}

} // namespace

/* =====================================================================
 * In / out
 *
 * The text form of a tile is its hex-encoded WKB, the representation
 * MobilityDB's raquet_in / raquet_out use.
 * ===================================================================== */

bool RaquetFunctions::Raquet_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t s) -> string_t {
            std::string str(s.GetData(), s.GetSize());
            Raquet *rq = raquet_in(str.c_str());
            if (!rq) throw InvalidInputException("raquet_in: invalid tile text");
            return RaquetToBlob(result, rq);
        });
    return true;
}

bool RaquetFunctions::Raquet_out_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            Raquet *rq = BlobToRaquet(blob);
            char *str = raquet_out(rq);
            free(rq);
            if (!str) throw InvalidInputException("raquet_out: invalid tile");
            std::string copy(str);
            free(str);
            return StringVector::AddString(result, copy);
        });
    return true;
}

/* =====================================================================
 * Registration
 * ===================================================================== */

void RaquetTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, raquet(),
        RaquetFunctions::Raquet_in_cast);
    RegisterMeosCastFunction(loader, raquet(), LogicalType::VARCHAR,
        RaquetFunctions::Raquet_out_cast);
}

} // namespace duckdb
