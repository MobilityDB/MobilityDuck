/* MobilityDuck binding for the MEOS pose chain types (posechain + tposechain).
 *
 * posechain is a varlena value (a directed chain of rigid-body poses, one
 * topocentric frame at the outside and a link per joint), surfaced as a BLOB.
 * tposechain is the temporal pose chain, stored as a Temporal* blob (BLOB).
 *
 * Only the type registration + text I/O boundary is hand-written here; the
 * operation surface is generated from the MEOS-API catalog into src/generated/.
 */

#include "posechain/tposechain.hpp"
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
    #include <meos_pose.h>
    #include <meos_internal.h>
}

#include <cstdlib>
#include <cstring>
#include <string>

namespace duckdb {

LogicalType PosechainTypes::posechain() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("posechain");
    return type;
}

LogicalType PosechainTypes::tposechain() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("tposechain");
    return type;
}

void PosechainTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType("posechain",  posechain());
    loader.RegisterType("tposechain", tposechain());
}

namespace {

/* posechain is a varlena value: the stored BLOB holds the full varlena bytes,
 * so copy-in by the blob size and copy-out by VARSIZE. */
inline PoseChain *BlobToPosechain(string_t blob) {
    size_t sz = blob.GetSize();
    void *copy = malloc(sz);
    memcpy(copy, blob.GetData(), sz);
    return reinterpret_cast<PoseChain *>(copy);
}

inline string_t PosechainToBlob(Vector &result, PoseChain *pc) {
    if (!pc) return string_t();
    /* PoseChain is a 4-byte-header varlena (int32 vl_len_); VARSIZE reads it. */
    size_t sz = VARSIZE(pc);
    string_t out = StringVector::AddStringOrBlob(
        result, string_t(reinterpret_cast<const char *>(pc), sz));
    free(pc);
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
 * In / out — static posechain value
 * ===================================================================== */

bool PosechainFunctions::Posechain_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t s) -> string_t {
            std::string str(s.GetData(), s.GetSize());
            PoseChain *pc = posechain_in(str.c_str());
            return PosechainToBlob(result, pc);
        });
    return true;
}

bool PosechainFunctions::Posechain_out_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            PoseChain *pc = BlobToPosechain(blob);
            char *str = posechain_out(pc, OUT_DEFAULT_DECIMAL_DIGITS);
            free(pc);
            std::string copy(str);
            free(str);
            return StringVector::AddString(result, copy);
        });
    return true;
}

/* =====================================================================
 * In / out — tposechain temporal value
 * ===================================================================== */

bool PosechainFunctions::Tposechain_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t s) -> string_t {
            std::string str(s.GetData(), s.GetSize());
            Temporal *t = tposechain_in(str.c_str());
            return TempToBlob(result, t);
        });
    return true;
}

bool PosechainFunctions::Tposechain_out_cast(
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

void PosechainTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, posechain(),
        PosechainFunctions::Posechain_in_cast);
    RegisterMeosCastFunction(loader, posechain(), LogicalType::VARCHAR,
        PosechainFunctions::Posechain_out_cast);
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, tposechain(),
        PosechainFunctions::Tposechain_in_cast);
    RegisterMeosCastFunction(loader, tposechain(), LogicalType::VARCHAR,
        PosechainFunctions::Tposechain_out_cast);
}

} // namespace duckdb
