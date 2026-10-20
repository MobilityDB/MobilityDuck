/* MobilityDuck binding for the MEOS QUADBIN cell index types (quadbin +
 * tquadbin).  Wraps every implemented export from `meos_quadbin.h` so DuckDB
 * SQL can call the full static-cell and temporal-cell surface.
 *
 * QUADBIN is surfaced as BIGINT (the 64-bit cell id reinterprets losslessly).
 * TQUADBIN is the temporal cell index, stored as a Temporal* blob (BLOB).
 * The analogue of H3INDEX / TH3INDEX for the square Web-Mercator DGGS.
 */

#include "quadbin/tquadbin.hpp"
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
    #include <meos_quadbin.h>
    #include <meos_internal.h>
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace duckdb {

LogicalType QuadbinTypes::quadbin() {
    LogicalType type = LogicalType::BIGINT;
    type.SetAlias("quadbin");
    return type;
}

LogicalType QuadbinTypes::tquadbin() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("tquadbin");
    return type;
}

void QuadbinTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType("quadbin",  quadbin());
    loader.RegisterType("tquadbin", tquadbin());
}

void QuadbinTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, quadbin(),
        QuadbinFunctions::Quadbin_in_cast);
    RegisterMeosCastFunction(loader, quadbin(), LogicalType::VARCHAR,
        QuadbinFunctions::Quadbin_out_cast);
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, tquadbin(),
        QuadbinFunctions::Tquadbin_in_cast);
    RegisterMeosCastFunction(loader, tquadbin(), LogicalType::VARCHAR,
        QuadbinFunctions::Tquadbin_out_cast);
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
 * In / out — static QUADBIN cell (BIGINT)
 *
 * QUADBIN cells are represented as hexadecimal cell ids.
 * ===================================================================== */

bool QuadbinFunctions::Quadbin_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, int64_t>(
        source, result, count,
        [&](string_t s) -> int64_t {
            std::string str(s.GetData(), s.GetSize());
            Quadbin cell = quadbin_in(str.c_str());
            return static_cast<int64_t>(cell);
        });
    return true;
}

bool QuadbinFunctions::Quadbin_out_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<int64_t, string_t>(
        source, result, count,
        [&](int64_t v) -> string_t {
            char *str = quadbin_index_to_string(static_cast<Quadbin>(v));
            std::string copy(str);
            free(str);
            return StringVector::AddString(result, copy);
        });
    return true;
}

/* =====================================================================
 * In / out — TQUADBIN temporal value
 * ===================================================================== */

bool QuadbinFunctions::Tquadbin_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t s) -> string_t {
            std::string str(s.GetData(), s.GetSize());
            Temporal *t = tquadbin_in(str.c_str());
            return TempToBlob(result, t);
        });
    return true;
}

bool QuadbinFunctions::Tquadbin_out_cast(
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
 * Constructor — tquadbininst_make wrapped as `tquadbin(cell, t)`
 * ===================================================================== */

void QuadbinFunctions::Tquadbin_make(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    BinaryExecutor::Execute<int64_t, timestamp_tz_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](int64_t cell, timestamp_tz_t t) -> string_t {
            TInstant *inst = tquadbininst_make(
                static_cast<Quadbin>(cell), ToMeosTimestamp(t));
            return TempToBlob(result, reinterpret_cast<Temporal *>(inst));
        });
}

/* =====================================================================
 * Registration
 * ===================================================================== */

void QuadbinTypes::RegisterScalarFunctions(ExtensionLoader &loader) {
    const auto QB  = QuadbinTypes::quadbin();
    const auto TQB = QuadbinTypes::tquadbin();
    const auto B   = LogicalType::BOOLEAN;
    const auto V   = LogicalType::VARCHAR;
    const auto TS  = LogicalType::TIMESTAMP_TZ;

    /* Constructor */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "tquadbin", {QB, TS}, TQB,
        QuadbinFunctions::Tquadbin_make));

    /* Accessors — startValue/endValue, valueN, getValues and valueAtTimestamp are generated
     * from the catalog (meos_quadbin_accessor). */

    /* Quadkey conversion */

    /* tbigint ↔ tquadbin: deferred until TemporalTypes::TBIGINT() is published,
     * same as the th3index binding. */
}

} // namespace duckdb
