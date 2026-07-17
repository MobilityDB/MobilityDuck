/* MobilityDuck binding for the MEOS H3 cell index types (h3index + th3index).
 *
 * H3INDEX is surfaced as BIGINT (the 64-bit cell id reinterprets losslessly).
 * TH3INDEX is the temporal cell index, stored as a Temporal* blob (BLOB).
 * H3 cells are geography-like (inherently WGS84 / EPSG:4326).
 *
 * Only the TYPE registration + text I/O casts are hand-written here (the
 * justified non-generatable layer, as for QUADBIN/TQUADBIN). The H3 function
 * surface is generated from the MEOS-API catalog. */

#include "h3/th3index.hpp"
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
    #include <meos_h3.h>
    #include <meos_internal.h>
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace duckdb {

LogicalType H3indexTypes::h3index() {
    LogicalType type = LogicalType::BIGINT;
    type.SetAlias("h3index");
    return type;
}

LogicalType H3indexTypes::th3index() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("th3index");
    return type;
}

/* An h3indexset is a MEOS Set of h3index cells (the WKB-serialized Set blob). It is the
 * static geometry->cells prefilter value produced by geoToH3IndexSet and consumed by
 * eEq(h3indexset, th3index); registering it as its own BLOB alias lets that overload
 * resolve distinctly from eEq(th3index, th3index). */
LogicalType H3indexTypes::h3indexset() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("h3indexset");
    return type;
}

void H3indexTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType("h3index",     h3index());
    loader.RegisterType("th3index",    th3index());
    loader.RegisterType("h3indexset",  h3indexset());
}

void H3indexTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, h3index(),
        H3indexFunctions::H3index_in_cast);
    RegisterMeosCastFunction(loader, h3index(), LogicalType::VARCHAR,
        H3indexFunctions::H3index_out_cast);
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, th3index(),
        H3indexFunctions::Th3index_in_cast);
    RegisterMeosCastFunction(loader, th3index(), LogicalType::VARCHAR,
        H3indexFunctions::Th3index_out_cast);
}

namespace {

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
 * In / out — static H3INDEX cell (BIGINT)
 *
 * H3 cells are represented as lowercase hexadecimal cell ids.
 * ===================================================================== */

bool H3indexFunctions::H3index_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, int64_t>(
        source, result, count,
        [&](string_t s) -> int64_t {
            std::string str(s.GetData(), s.GetSize());
            H3Index cell = h3index_in(str.c_str());
            return static_cast<int64_t>(cell);
        });
    return true;
}

bool H3indexFunctions::H3index_out_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<int64_t, string_t>(
        source, result, count,
        [&](int64_t v) -> string_t {
            char *str = h3index_out(static_cast<H3Index>(v));
            std::string copy(str);
            free(str);
            return StringVector::AddString(result, copy);
        });
    return true;
}

/* =====================================================================
 * In / out — TH3INDEX temporal value
 * ===================================================================== */

bool H3indexFunctions::Th3index_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t s) -> string_t {
            std::string str(s.GetData(), s.GetSize());
            Temporal *t = th3index_in(str.c_str());
            return TempToBlob(result, t);
        });
    return true;
}

bool H3indexFunctions::Th3index_out_cast(
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

} // namespace duckdb
