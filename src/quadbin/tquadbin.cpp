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
 * QUADBIN cells are represented as decimal uint64 integers in CARTO's
 * canonical form (e.g. 5193776270265024512).
 * ===================================================================== */

bool QuadbinFunctions::Quadbin_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, int64_t>(
        source, result, count,
        [&](string_t s) -> int64_t {
            std::string str(s.GetData(), s.GetSize());
            uint64_t v = std::stoull(str);
            return static_cast<int64_t>(v);
        });
    return true;
}

bool QuadbinFunctions::Quadbin_out_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<int64_t, string_t>(
        source, result, count,
        [&](int64_t v) -> string_t {
            std::string s = std::to_string(static_cast<uint64_t>(v));
            return StringVector::AddString(result, s);
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
 * Static cell helpers
 * ===================================================================== */

void QuadbinFunctions::Quadbin_tile_to_cell(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    TernaryExecutor::Execute<int32_t, int32_t, int32_t, int64_t>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [](int32_t x, int32_t y, int32_t z) -> int64_t {
            Quadbin cell = quadbin_tile_to_cell(
                static_cast<uint32_t>(x),
                static_cast<uint32_t>(y),
                static_cast<uint32_t>(z));
            return static_cast<int64_t>(cell);
        });
}

/* quadbinCellToTileX/Y/Z: expose each tile coordinate as a separate scalar.
 * DuckDB has no STRUCT type in the current MobilityDuck surface; three
 * functions mirror the (x,y,z) MobilityDB SQL pattern. */
void QuadbinFunctions::Quadbin_cell_to_tile_x(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    UnaryExecutor::Execute<int64_t, int32_t>(
        args.data[0], result, args.size(),
        [](int64_t v) -> int32_t {
            uint32_t x, y, z;
            quadbin_cell_to_tile(static_cast<Quadbin>(v), &x, &y, &z);
            return static_cast<int32_t>(x);
        });
}

void QuadbinFunctions::Quadbin_cell_to_tile_y(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    UnaryExecutor::Execute<int64_t, int32_t>(
        args.data[0], result, args.size(),
        [](int64_t v) -> int32_t {
            uint32_t x, y, z;
            quadbin_cell_to_tile(static_cast<Quadbin>(v), &x, &y, &z);
            return static_cast<int32_t>(y);
        });
}

void QuadbinFunctions::Quadbin_cell_to_tile_z(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    UnaryExecutor::Execute<int64_t, int32_t>(
        args.data[0], result, args.size(),
        [](int64_t v) -> int32_t {
            uint32_t x, y, z;
            quadbin_cell_to_tile(static_cast<Quadbin>(v), &x, &y, &z);
            return static_cast<int32_t>(z);
        });
}

void QuadbinFunctions::Quadbin_get_resolution(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    UnaryExecutor::Execute<int64_t, int32_t>(
        args.data[0], result, args.size(),
        [](int64_t v) -> int32_t {
            return static_cast<int32_t>(
                quadbin_get_resolution(static_cast<Quadbin>(v)));
        });
}

void QuadbinFunctions::Quadbin_is_valid_cell(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    UnaryExecutor::Execute<int64_t, bool>(
        args.data[0], result, args.size(),
        [](int64_t v) -> bool {
            return quadbin_is_valid_cell(static_cast<Quadbin>(v));
        });
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
 * Accessors
 * ===================================================================== */

void QuadbinFunctions::Tquadbin_value_n(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    BinaryExecutor::ExecuteWithNulls<string_t, int32_t, int64_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t blob, int32_t n, ValidityMask &mask, idx_t idx) -> int64_t {
            Temporal *t = BlobToTemp(blob);
            Quadbin v;
            bool ok = tquadbin_value_n(t, n, &v);
            free(t);
            if (!ok) { mask.SetInvalid(idx); return 0; }
            return static_cast<int64_t>(v);
        });
}

void QuadbinFunctions::Tquadbin_values(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    /* Quadbin[] → LIST<BIGINT>; cell ids returned as signed BIGINT. */
    auto &input = args.data[0];
    input.Flatten(args.size());
    auto in_data   = FlatVector::GetData<string_t>(input);
    auto list_entries = FlatVector::GetData<list_entry_t>(result);
    auto &out_validity = FlatVector::Validity(result);
    idx_t total = 0;

    for (idx_t row = 0; row < args.size(); row++) {
        if (!FlatVector::Validity(input).RowIsValid(row)) {
            out_validity.SetInvalid(row);
            list_entries[row] = list_entry_t{total, 0};
            continue;
        }
        Temporal *t = BlobToTemp(in_data[row]);
        int n = 0;
        Quadbin *vals = tquadbin_values(t, &n);
        free(t);
        if (!vals || n <= 0) {
            if (vals) free(vals);
            list_entries[row] = list_entry_t{total, 0};
            continue;
        }
        ListVector::Reserve(result, total + n);
        ListVector::SetListSize(result, total + n);
        list_entries[row] = list_entry_t{total, static_cast<uint64_t>(n)};
        auto child = FlatVector::GetData<int64_t>(ListVector::GetEntry(result));
        for (int i = 0; i < n; i++)
            child[total + i] = static_cast<int64_t>(vals[i]);
        total += n;
        free(vals);
    }
}

void QuadbinFunctions::Tquadbin_value_at_timestamptz(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, int64_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t blob, timestamp_tz_t ts,
            ValidityMask &mask, idx_t idx) -> int64_t {
            Temporal *t = BlobToTemp(blob);
            Quadbin v;
            bool ok = tquadbin_value_at_timestamptz(
                t, ToMeosTimestamp(ts), true, &v);
            free(t);
            if (!ok) { mask.SetInvalid(idx); return 0; }
            return static_cast<int64_t>(v);
        });
}

/* =====================================================================
 * Quadkey conversion — tquadbin → tvarchar (base-4 slippy-tile string)
 * ===================================================================== */

void QuadbinFunctions::Tquadbin_cell_to_quadkey(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
            Temporal *t = BlobToTemp(blob);
            Temporal *r = tquadbin_cell_to_quadkey(t);
            free(t);
            if (!r) { mask.SetInvalid(idx); return string_t(); }
            char *str = temporal_out(r, OUT_DEFAULT_DECIMAL_DIGITS);
            free(r);
            std::string copy(str);
            free(str);
            return StringVector::AddString(result, copy);
        });
}

/* =====================================================================
 * Casts to/from tbigint
 * ===================================================================== */

void QuadbinFunctions::Tbigint_to_tquadbin(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
            Temporal *t = BlobToTemp(blob);
            Temporal *r = tbigint_to_tquadbin(t);
            free(t);
            if (!r) { mask.SetInvalid(idx); return string_t(); }
            return TempToBlob(result, r);
        });
}

void QuadbinFunctions::Tquadbin_to_tbigint(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    UnaryExecutor::ExecuteWithNulls<string_t, string_t>(
        args.data[0], result, args.size(),
        [&](string_t blob, ValidityMask &mask, idx_t idx) -> string_t {
            Temporal *t = BlobToTemp(blob);
            Temporal *r = tquadbin_to_tbigint(t);
            free(t);
            if (!r) { mask.SetInvalid(idx); return string_t(); }
            return TempToBlob(result, r);
        });
}

/* =====================================================================
 * Registration
 * ===================================================================== */

void QuadbinTypes::RegisterScalarFunctions(ExtensionLoader &loader) {
    const auto QB  = QuadbinTypes::quadbin();
    const auto TQB = QuadbinTypes::tquadbin();
    const auto I32 = LogicalType::INTEGER;
    const auto B   = LogicalType::BOOLEAN;
    const auto V   = LogicalType::VARCHAR;
    const auto TS  = LogicalType::TIMESTAMP_TZ;

    /* Static cell helpers */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "quadbinTileToCell", {I32, I32, I32}, QB,
        QuadbinFunctions::Quadbin_tile_to_cell));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "quadbinCellToTileX", {QB}, I32,
        QuadbinFunctions::Quadbin_cell_to_tile_x));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "quadbinCellToTileY", {QB}, I32,
        QuadbinFunctions::Quadbin_cell_to_tile_y));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "quadbinCellToTileZ", {QB}, I32,
        QuadbinFunctions::Quadbin_cell_to_tile_z));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "quadbinGetResolution", {QB}, I32,
        QuadbinFunctions::Quadbin_get_resolution));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "quadbinIsValidCell", {QB}, B,
        QuadbinFunctions::Quadbin_is_valid_cell));

    /* Constructor */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "tquadbin", {QB, TS}, TQB,
        QuadbinFunctions::Tquadbin_make));

    /* Accessors — startValue/endValue are GENERATED (Tcell cell-id base value,
     * CELL_BASEVAL); only the not-yet-generatable out-param/list accessors stay hand. */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "valueN", {TQB, I32}, QB,
        QuadbinFunctions::Tquadbin_value_n));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "values", {TQB}, LogicalType::LIST(QB),
        QuadbinFunctions::Tquadbin_values));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "valueAtTimestamp", {TQB, TS}, QB,
        QuadbinFunctions::Tquadbin_value_at_timestamptz));

    /* Quadkey conversion */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "cellToQuadkey", {TQB}, V,
        QuadbinFunctions::Tquadbin_cell_to_quadkey));

    /* tbigint ↔ tquadbin: deferred until TemporalTypes::TBIGINT() is published,
     * same as the th3index binding. */
}

} // namespace duckdb
