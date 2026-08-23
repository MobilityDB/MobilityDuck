/* MobilityDuck binding for the MEOS network-point types (npoint + nsegment + tnpoint).
 *
 * npoint is a network point (a route identifier + a fractional position), a
 * FIXED-size struct {int64 rid, double pos} surfaced as a BLOB (sizeof(Npoint)).
 * nsegment is a network segment {int64 rid, double pos1, double pos2}, likewise.
 * tnpoint is the temporal network point, stored as a Temporal* blob (BLOB).
 *
 * npoint is the 2D-planar twin of cbuffer: both are Spatial<T> base values,
 * non-geodetic. npoint carries NO SRID of its own — npoint_srid() returns
 * get_srid_ways(), so the `ways` network CSV must be loaded (meos_set_ways_csv,
 * done at extension init) for construction and geometry ops to resolve.
 *
 * Only the type registration + text I/O boundary is hand-written here; the
 * operation surface is generated from the MEOS-API catalog into src/generated/.
 */

#include "npoint/tnpoint.hpp"
#include "temporal/temporal.hpp"
#include "tydef.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "mobilityduck/meos_exec_serial.hpp"
#include "time_util.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_npoint.h>
    #include <meos_internal.h>
}

#include <cstdlib>
#include <cstring>
#include <string>

namespace duckdb {

LogicalType NpointTypes::npoint() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("npoint");
    return type;
}

LogicalType NpointTypes::nsegment() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("nsegment");
    return type;
}

LogicalType NpointTypes::tnpoint() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("tnpoint");
    return type;
}

void NpointTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType("npoint",   npoint());
    loader.RegisterType("nsegment", nsegment());
    loader.RegisterType("tnpoint",  tnpoint());
}

namespace {

/* Npoint / Nsegment are FIXED-size structs (palloc(sizeof(...))), so the stored
 * BLOB holds exactly sizeof bytes — copy-in/out by sizeof (like Span/STBox).
 * The size is CHECKED rather than assumed: the type is BLOB-backed, so what the
 * copy reads is a length the value carries rather than one the type guarantees,
 * and a shorter one would read past the end of a string_t whose 12 bytes sit
 * inline. Reading the length also gives the compiler the bound it otherwise
 * cannot prove, which is what keeps the copy free of a -Wstringop-overread. */
template <class T>
inline T *BlobToFixed(string_t blob, const char *type_name) {
    if (blob.GetSize() != sizeof(T)) {
        throw InvalidInputException("A %s value is %llu bytes, and this one holds %llu",
                                    type_name, (uint64_t) sizeof(T), (uint64_t) blob.GetSize());
    }
    T *copy = (T *) malloc(sizeof(T));
    memcpy(copy, blob.GetData(), sizeof(T));
    return copy;
}

inline Npoint *BlobToNpoint(string_t blob) {
    return BlobToFixed<Npoint>(blob, "npoint");
}

inline string_t NpointToBlob(Vector &result, Npoint *np) {
    if (!np) return string_t();
    string_t out = StringVector::AddStringOrBlob(
        result, string_t(reinterpret_cast<const char *>(np), sizeof(Npoint)));
    free(np);
    return out;
}

inline Nsegment *BlobToNsegment(string_t blob) {
    return BlobToFixed<Nsegment>(blob, "nsegment");
}

inline string_t NsegmentToBlob(Vector &result, Nsegment *ns) {
    if (!ns) return string_t();
    string_t out = StringVector::AddStringOrBlob(
        result, string_t(reinterpret_cast<const char *>(ns), sizeof(Nsegment)));
    free(ns);
    return out;
}

inline Temporal *BlobToTemp(string_t blob) {
    size_t sz = blob.GetSize();
    uint8_t *copy = (uint8_t *) malloc(sz);
    memcpy(copy, blob.GetData(), sz);
    return reinterpret_cast<Temporal *>(copy);
}

inline string_t TempToBlob(Vector &result, Temporal *t) {
    if (!t) return string_t();
    size_t sz = temporal_mem_size(t);
    string_t out = StringVector::AddStringOrBlob(
        result, string_t(reinterpret_cast<const char *>(t), sz));
    free(t);
    return out;
}

} // namespace

/* =====================================================================
 * In / out — static npoint value
 * ===================================================================== */

bool NpointFunctions::Npoint_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t s) -> string_t {
            std::string str(s.GetData(), s.GetSize());
            Npoint *np = npoint_in(str.c_str());
            return NpointToBlob(result, np);
        });
    return true;
}

bool NpointFunctions::Npoint_out_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            Npoint *np = BlobToNpoint(blob);
            char *str = npoint_out(np, OUT_DEFAULT_DECIMAL_DIGITS);
            free(np);
            std::string copy(str);
            free(str);
            return StringVector::AddString(result, copy);
        });
    return true;
}

/* =====================================================================
 * In / out — static nsegment value
 * ===================================================================== */

bool NpointFunctions::Nsegment_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t s) -> string_t {
            std::string str(s.GetData(), s.GetSize());
            Nsegment *ns = nsegment_in(str.c_str());
            return NsegmentToBlob(result, ns);
        });
    return true;
}

bool NpointFunctions::Nsegment_out_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            Nsegment *ns = BlobToNsegment(blob);
            char *str = nsegment_out(ns, OUT_DEFAULT_DECIMAL_DIGITS);
            free(ns);
            std::string copy(str);
            free(str);
            return StringVector::AddString(result, copy);
        });
    return true;
}

/* =====================================================================
 * In / out — tnpoint temporal value
 * ===================================================================== */

bool NpointFunctions::Tnpoint_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t s) -> string_t {
            std::string str(s.GetData(), s.GetSize());
            Temporal *t = tnpoint_in(str.c_str());
            return TempToBlob(result, t);
        });
    return true;
}

bool NpointFunctions::Tnpoint_out_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            Temporal *t = BlobToTemp(blob);
            char *str = temporal_out(t, OUT_DEFAULT_DECIMAL_DIGITS);
            free(t);
            std::string copy(str);
            free(str);
            return StringVector::AddString(result, copy);
        });
    return true;
}

/* =====================================================================
 * Registration
 * ===================================================================== */

void NpointTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, npoint(),
        NpointFunctions::Npoint_in_cast);
    RegisterMeosCastFunction(loader, npoint(), LogicalType::VARCHAR,
        NpointFunctions::Npoint_out_cast);
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, nsegment(),
        NpointFunctions::Nsegment_in_cast);
    RegisterMeosCastFunction(loader, nsegment(), LogicalType::VARCHAR,
        NpointFunctions::Nsegment_out_cast);
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, tnpoint(),
        NpointFunctions::Tnpoint_in_cast);
    RegisterMeosCastFunction(loader, tnpoint(), LogicalType::VARCHAR,
        NpointFunctions::Tnpoint_out_cast);
}

} // namespace duckdb
