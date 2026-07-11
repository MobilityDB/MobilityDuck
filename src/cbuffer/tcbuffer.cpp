/* MobilityDuck binding for the MEOS circular-buffer types (cbuffer + tcbuffer).
 *
 * cbuffer is a varlena value (a point + a radius), surfaced as a BLOB.
 * tcbuffer is the temporal circular buffer, stored as a Temporal* blob (BLOB).
 *
 * Only the type registration + text I/O boundary is hand-written here; the
 * operation surface is generated from the MEOS-API catalog into src/generated/.
 */

#include "cbuffer/tcbuffer.hpp"
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
    #include <meos_cbuffer.h>
    #include <meos_internal.h>
}

#include <cstdlib>
#include <cstring>
#include <string>

namespace duckdb {

LogicalType CbufferTypes::cbuffer() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("cbuffer");
    return type;
}

LogicalType CbufferTypes::tcbuffer() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("tcbuffer");
    return type;
}

void CbufferTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType("cbuffer",  cbuffer());
    loader.RegisterType("tcbuffer", tcbuffer());
}

namespace {

/* cbuffer is a varlena value: the stored BLOB holds the full varlena bytes, so
 * copy-in by the blob size and copy-out by VARSIZE_ANY. */
inline Cbuffer *BlobToCbuffer(string_t blob) {
    size_t sz = blob.GetSize();
    void *copy = malloc(sz);
    memcpy(copy, blob.GetData(), sz);
    return reinterpret_cast<Cbuffer *>(copy);
}

inline string_t CbufferToBlob(Vector &result, Cbuffer *cb) {
    if (!cb) return string_t();
    /* Cbuffer is a 4-byte-header varlena (int32 vl_len_); VARSIZE reads it. */
    size_t sz = VARSIZE(cb);
    string_t out = StringVector::AddStringOrBlob(
        result, string_t(reinterpret_cast<const char *>(cb), sz));
    free(cb);
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
 * In / out — static cbuffer value
 * ===================================================================== */

bool CbufferFunctions::Cbuffer_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t s) -> string_t {
            std::string str(s.GetData(), s.GetSize());
            Cbuffer *cb = cbuffer_in(str.c_str());
            return CbufferToBlob(result, cb);
        });
    return true;
}

bool CbufferFunctions::Cbuffer_out_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            Cbuffer *cb = BlobToCbuffer(blob);
            char *str = cbuffer_out(cb, OUT_DEFAULT_DECIMAL_DIGITS);
            free(cb);
            std::string copy(str);
            free(str);
            return StringVector::AddString(result, copy);
        });
    return true;
}

/* =====================================================================
 * In / out — tcbuffer temporal value
 * ===================================================================== */

bool CbufferFunctions::Tcbuffer_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t s) -> string_t {
            std::string str(s.GetData(), s.GetSize());
            Temporal *t = tcbuffer_in(str.c_str());
            return TempToBlob(result, t);
        });
    return true;
}

bool CbufferFunctions::Tcbuffer_out_cast(
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

void CbufferTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, cbuffer(),
        CbufferFunctions::Cbuffer_in_cast);
    RegisterMeosCastFunction(loader, cbuffer(), LogicalType::VARCHAR,
        CbufferFunctions::Cbuffer_out_cast);
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, tcbuffer(),
        CbufferFunctions::Tcbuffer_in_cast);
    RegisterMeosCastFunction(loader, tcbuffer(), LogicalType::VARCHAR,
        CbufferFunctions::Tcbuffer_out_cast);
}

} // namespace duckdb
