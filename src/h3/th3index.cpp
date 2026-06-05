/* MobilityDuck binding for the MEOS H3 cell index types (h3index +
 * th3index).  Wraps every export from `meos_h3.h` so DuckDB SQL can
 * call the full H3 surface — primarily for the cross-platform
 * BerlinMOD benchmark prefilter (matching MobilitySpark PR #9).
 *
 * H3INDEX is surfaced as BIGINT (the 64-bit cell id reinterprets
 * losslessly).  TH3INDEX is a Temporal* blob stored as BLOB.
 */

#include "h3/th3index.hpp"
#include "temporal/temporal.hpp"
#include "geo/tgeompoint.hpp"
#include "geo/tgeogpoint.hpp"
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
    #include <h3api.h>
}

namespace {

/* MEOS commit beddae670 declares `h3index_in` and `h3index_out` in
 * `meos_h3.h` but does not define them in the source tree.  They are
 * thin wrappers around h3's `stringToH3` / `h3ToString` — implement
 * locally so MobilityDuck's H3INDEX cast / text-output paths link.
 *
 * Drop these definitions once upstream MEOS ships its own versions.
 */
extern "C" H3Index h3index_in(const char *str) {
    H3Index out = 0;
    H3Error err = stringToH3(str, &out);
    if (err != E_SUCCESS) {
        return 0;
    }
    return out;
}

extern "C" char *h3index_out(H3Index cell) {
    /* H3's textual form is "xxxxxxxxxxxxxxxx" — 16 hex digits +
     * NUL.  Allocate slightly more for safety. */
    char *buf = (char *) malloc(32);
    if (!buf) return nullptr;
    H3Error err = h3ToString(cell, buf, 32);
    if (err != E_SUCCESS) {
        buf[0] = '\0';
    }
    return buf;
}

}

namespace duckdb {

LogicalType H3IndexTypes::H3INDEX() {
    /* 64-bit unsigned cell id; surface as BIGINT (signed reinterpretation
     * is safe — equality / ordering care only about the bit pattern). */
    LogicalType type = LogicalType::BIGINT;
    type.SetAlias("H3INDEX");
    return type;
}

LogicalType H3IndexTypes::TH3INDEX() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("TH3INDEX");
    return type;
}

void H3IndexTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType("H3INDEX", H3INDEX());
    loader.RegisterType("TH3INDEX", TH3INDEX());
}

void H3IndexTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    loader.RegisterCastFunction(LogicalType::VARCHAR, H3INDEX(),
        H3IndexFunctions::H3index_in_cast);
    loader.RegisterCastFunction(H3INDEX(), LogicalType::VARCHAR,
        H3IndexFunctions::H3index_out_cast);
    loader.RegisterCastFunction(LogicalType::VARCHAR, TH3INDEX(),
        H3IndexFunctions::Th3index_in_cast);
    loader.RegisterCastFunction(TH3INDEX(), LogicalType::VARCHAR,
        H3IndexFunctions::Th3index_out_cast);
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

/* TINT → BIGINT result for the int-returning H3 predicates. */
inline bool IntToBool(int r) { return r != 0; }

} // namespace

/* =====================================================================
 * In / out — H3 cell scalar (BIGINT bit-pattern of uint64 H3Index)
 * ===================================================================== */

bool H3IndexFunctions::H3index_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, int64_t>(
        source, result, count,
        [&](string_t s) -> int64_t {
            std::string str(s.GetData(), s.GetSize());
            H3Index h = h3index_in(str.c_str());
            return static_cast<int64_t>(h);
        });
    return true;
}

bool H3IndexFunctions::H3index_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<int64_t, string_t>(
        source, result, count,
        [&](int64_t v) -> string_t {
            char *s = h3index_out(static_cast<H3Index>(v));
            std::string copy(s);
            free(s);
            return StringVector::AddString(result, copy);
        });
    return true;
}

void H3IndexFunctions::H3index_from_text(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, int64_t>(
        args.data[0], result, args.size(),
        [&](string_t s) -> int64_t {
            std::string str(s.GetData(), s.GetSize());
            H3Index h = h3index_in(str.c_str());
            return static_cast<int64_t>(h);
        });
}

void H3IndexFunctions::H3index_as_text(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<int64_t, string_t>(
        args.data[0], result, args.size(),
        [&](int64_t v) -> string_t {
            char *s = h3index_out(static_cast<H3Index>(v));
            std::string copy(s);
            free(s);
            return StringVector::AddString(result, copy);
        });
}

/* =====================================================================
 * In / out — TH3INDEX temporal blob
 * ===================================================================== */

bool H3IndexFunctions::Th3index_in_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t s) -> string_t {
            std::string str(s.GetData(), s.GetSize());
            Temporal *t = th3index_in(str.c_str());
            return TempToBlob(result, t);
        });
    return true;
}

bool H3IndexFunctions::Th3index_out_cast(Vector &source, Vector &result, idx_t count, CastParameters &parameters) {
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
 * Constructor — th3indexinst_make wrapped as `th3index(cell, t)`
 * ===================================================================== */

void H3IndexFunctions::Th3index_make(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::Execute<int64_t, timestamp_tz_t, string_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](int64_t cell, timestamp_tz_t t) -> string_t {
            TInstant *inst = th3indexinst_make(static_cast<H3Index>(cell), ToMeosTimestamp(t));
            return TempToBlob(result, reinterpret_cast<Temporal *>(inst));
        });
}

/* =====================================================================
 * Accessors
 * ===================================================================== */

void H3IndexFunctions::Th3index_start_value(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, int64_t>(
        args.data[0], result, args.size(),
        [&](string_t blob) -> int64_t {
            Temporal *t = BlobToTemp(blob);
            H3Index v = th3index_start_value(t);
            free(t);
            return static_cast<int64_t>(v);
        });
}

void H3IndexFunctions::Th3index_end_value(DataChunk &args, ExpressionState &state, Vector &result) {
    UnaryExecutor::Execute<string_t, int64_t>(
        args.data[0], result, args.size(),
        [&](string_t blob) -> int64_t {
            Temporal *t = BlobToTemp(blob);
            H3Index v = th3index_end_value(t);
            free(t);
            return static_cast<int64_t>(v);
        });
}

void H3IndexFunctions::Th3index_value_n(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, int32_t, int64_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t blob, int32_t n, ValidityMask &mask, idx_t idx) -> int64_t {
            Temporal *t = BlobToTemp(blob);
            H3Index v;
            bool ok = th3index_value_n(t, n, &v);
            free(t);
            if (!ok) { mask.SetInvalid(idx); return 0; }
            return static_cast<int64_t>(v);
        });
}

void H3IndexFunctions::Th3index_values(DataChunk &args, ExpressionState &state, Vector &result) {
    /* H3Index[] → LIST<BIGINT>; surface as a list of cell ids. */
    auto &input = args.data[0];
    input.Flatten(args.size());
    auto in_data = FlatVector::GetData<string_t>(input);
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
        H3Index *vals = th3index_values(t, &n);
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
        for (int i = 0; i < n; i++) {
            child[total + i] = static_cast<int64_t>(vals[i]);
        }
        total += n;
        free(vals);
    }
}

void H3IndexFunctions::Th3index_value_at_timestamptz(DataChunk &args, ExpressionState &state, Vector &result) {
    BinaryExecutor::ExecuteWithNulls<string_t, timestamp_tz_t, int64_t>(
        args.data[0], args.data[1], result, args.size(),
        [&](string_t blob, timestamp_tz_t t, ValidityMask &mask, idx_t idx) -> int64_t {
            Temporal *temp = BlobToTemp(blob);
            H3Index v;
            bool ok = th3index_value_at_timestamptz(temp, ToMeosTimestamp(t), true, &v);
            free(temp);
            if (!ok) { mask.SetInvalid(idx); return 0; }
            return static_cast<int64_t>(v);
        });
}

/* =====================================================================
 * Casts to/from other temporal types — all `Temporal *fn(const Temporal *)`
 * ===================================================================== */

#define TH3_UNARY_TEMP(NAME, FN) \
void H3IndexFunctions::NAME(DataChunk &args, ExpressionState &state, Vector &result) { \
    UnaryExecutor::ExecuteWithNulls<string_t, string_t>( \
        args.data[0], result, args.size(), \
        [&](string_t blob, ValidityMask &mask, idx_t idx) -> string_t { \
            Temporal *t = BlobToTemp(blob); \
            Temporal *r = FN(t); \
            free(t); \
            if (!r) { mask.SetInvalid(idx); return string_t(); } \
            return TempToBlob(result, r); \
        }); \
}

TH3_UNARY_TEMP(Tbigint_to_th3index,            tbigint_to_th3index)
TH3_UNARY_TEMP(Th3index_to_tbigint,            th3index_to_tbigint)
TH3_UNARY_TEMP(Th3index_to_tgeogpoint,         th3index_to_tgeogpoint)
TH3_UNARY_TEMP(Th3index_to_tgeompoint,         th3index_to_tgeompoint)
TH3_UNARY_TEMP(Th3index_get_resolution,        th3index_get_resolution)
TH3_UNARY_TEMP(Th3index_get_base_cell_number,  th3index_get_base_cell_number)
TH3_UNARY_TEMP(Th3index_is_valid_cell,         th3index_is_valid_cell)
TH3_UNARY_TEMP(Th3index_is_res_class_iii,      th3index_is_res_class_iii)
TH3_UNARY_TEMP(Th3index_is_pentagon,           th3index_is_pentagon)
TH3_UNARY_TEMP(Th3index_cell_to_parent_next,   th3index_cell_to_parent_next)
TH3_UNARY_TEMP(Th3index_cell_to_center_child_next, th3index_cell_to_center_child_next)
TH3_UNARY_TEMP(Th3index_cell_to_boundary,      th3index_cell_to_boundary)
TH3_UNARY_TEMP(Th3index_is_valid_directed_edge,         th3index_is_valid_directed_edge)
TH3_UNARY_TEMP(Th3index_get_directed_edge_origin,       th3index_get_directed_edge_origin)
TH3_UNARY_TEMP(Th3index_get_directed_edge_destination,  th3index_get_directed_edge_destination)
TH3_UNARY_TEMP(Th3index_directed_edge_to_boundary,      th3index_directed_edge_to_boundary)
TH3_UNARY_TEMP(Th3index_vertex_to_latlng,      th3index_vertex_to_latlng)
TH3_UNARY_TEMP(Th3index_is_valid_vertex,       th3index_is_valid_vertex)

#undef TH3_UNARY_TEMP

#define TH3_TEMP_INT32_TEMP(NAME, FN) \
void H3IndexFunctions::NAME(DataChunk &args, ExpressionState &state, Vector &result) { \
    BinaryExecutor::ExecuteWithNulls<string_t, int32_t, string_t>( \
        args.data[0], args.data[1], result, args.size(), \
        [&](string_t blob, int32_t n, ValidityMask &mask, idx_t idx) -> string_t { \
            Temporal *t = BlobToTemp(blob); \
            Temporal *r = FN(t, n); \
            free(t); \
            if (!r) { mask.SetInvalid(idx); return string_t(); } \
            return TempToBlob(result, r); \
        }); \
}

TH3_TEMP_INT32_TEMP(Tgeogpoint_to_th3index,         tgeogpoint_to_th3index)
TH3_TEMP_INT32_TEMP(Tgeompoint_to_th3index,         tgeompoint_to_th3index)
TH3_TEMP_INT32_TEMP(Th3index_cell_to_parent,        th3index_cell_to_parent)
TH3_TEMP_INT32_TEMP(Th3index_cell_to_center_child,  th3index_cell_to_center_child)
TH3_TEMP_INT32_TEMP(Th3index_cell_to_child_pos,     th3index_cell_to_child_pos)
TH3_TEMP_INT32_TEMP(Th3index_cell_to_vertex,        th3index_cell_to_vertex)

#undef TH3_TEMP_INT32_TEMP

/* th3index_child_pos_to_cell takes (Temporal *, Temporal *, int32). */
void H3IndexFunctions::Th3index_child_pos_to_cell(DataChunk &args, ExpressionState &state, Vector &result) {
    TernaryExecutor::ExecuteWithNulls<string_t, string_t, int32_t, string_t>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t a, string_t b, int32_t res, ValidityMask &mask, idx_t idx) -> string_t {
            Temporal *child_pos = BlobToTemp(a);
            Temporal *parent = BlobToTemp(b);
            Temporal *r = th3index_child_pos_to_cell(child_pos, parent, res);
            free(child_pos); free(parent);
            if (!r) { mask.SetInvalid(idx); return string_t(); }
            return TempToBlob(result, r);
        });
}

#define TH3_TEMP_TEMP_TEMP(NAME, FN) \
void H3IndexFunctions::NAME(DataChunk &args, ExpressionState &state, Vector &result) { \
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>( \
        args.data[0], args.data[1], result, args.size(), \
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> string_t { \
            Temporal *t1 = BlobToTemp(a); \
            Temporal *t2 = BlobToTemp(b); \
            Temporal *r = FN(t1, t2); \
            free(t1); free(t2); \
            if (!r) { mask.SetInvalid(idx); return string_t(); } \
            return TempToBlob(result, r); \
        }); \
}

TH3_TEMP_TEMP_TEMP(Th3index_are_neighbor_cells,        th3index_are_neighbor_cells)
TH3_TEMP_TEMP_TEMP(Th3index_cells_to_directed_edge,    th3index_cells_to_directed_edge)
TH3_TEMP_TEMP_TEMP(Th3index_grid_distance,             th3index_grid_distance)
TH3_TEMP_TEMP_TEMP(Th3index_cell_to_local_ij,          th3index_cell_to_local_ij)
TH3_TEMP_TEMP_TEMP(Th3index_local_ij_to_cell,          th3index_local_ij_to_cell)

#undef TH3_TEMP_TEMP_TEMP

/* tgeogpoint_great_circle_distance(a, b, unit) — Temporal × Temporal × VARCHAR. */
void H3IndexFunctions::Tgeogpoint_great_circle_distance(DataChunk &args, ExpressionState &state, Vector &result) {
    TernaryExecutor::ExecuteWithNulls<string_t, string_t, string_t, string_t>(
        args.data[0], args.data[1], args.data[2], result, args.size(),
        [&](string_t a, string_t b, string_t unit, ValidityMask &mask, idx_t idx) -> string_t {
            Temporal *t1 = BlobToTemp(a);
            Temporal *t2 = BlobToTemp(b);
            std::string u(unit.GetData(), unit.GetSize());
            Temporal *r = tgeogpoint_great_circle_distance(t1, t2, u.c_str());
            free(t1); free(t2);
            if (!r) { mask.SetInvalid(idx); return string_t(); }
            return TempToBlob(result, r);
        });
}

#define TH3_TEMP_TEXT_TEMP(NAME, FN) \
void H3IndexFunctions::NAME(DataChunk &args, ExpressionState &state, Vector &result) { \
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>( \
        args.data[0], args.data[1], result, args.size(), \
        [&](string_t blob, string_t unit, ValidityMask &mask, idx_t idx) -> string_t { \
            Temporal *t = BlobToTemp(blob); \
            std::string u(unit.GetData(), unit.GetSize()); \
            Temporal *r = FN(t, u.c_str()); \
            free(t); \
            if (!r) { mask.SetInvalid(idx); return string_t(); } \
            return TempToBlob(result, r); \
        }); \
}

TH3_TEMP_TEXT_TEMP(Th3index_cell_area,    th3index_cell_area)
TH3_TEMP_TEXT_TEMP(Th3index_edge_length,  th3index_edge_length)

#undef TH3_TEMP_TEXT_TEMP

/* =====================================================================
 * Ever / always boolean predicates — int returning, with H3Index ↔ Temporal
 * ===================================================================== */

#define TH3_EA_H3_T(NAME, FN) \
void H3IndexFunctions::NAME(DataChunk &args, ExpressionState &state, Vector &result) { \
    BinaryExecutor::ExecuteWithNulls<int64_t, string_t, bool>( \
        args.data[0], args.data[1], result, args.size(), \
        [&](int64_t cell, string_t blob, ValidityMask &mask, idx_t idx) -> bool { \
            Temporal *t = BlobToTemp(blob); \
            int r = FN(static_cast<H3Index>(cell), t); \
            free(t); \
            if (r < 0) { mask.SetInvalid(idx); return false; } \
            return IntToBool(r); \
        }); \
}

TH3_EA_H3_T(Ever_eq_h3index_th3index,    ever_eq_h3index_th3index)
TH3_EA_H3_T(Ever_ne_h3index_th3index,    ever_ne_h3index_th3index)
TH3_EA_H3_T(Always_eq_h3index_th3index,  always_eq_h3index_th3index)
TH3_EA_H3_T(Always_ne_h3index_th3index,  always_ne_h3index_th3index)

#undef TH3_EA_H3_T

#define TH3_EA_T_H3(NAME, FN) \
void H3IndexFunctions::NAME(DataChunk &args, ExpressionState &state, Vector &result) { \
    BinaryExecutor::ExecuteWithNulls<string_t, int64_t, bool>( \
        args.data[0], args.data[1], result, args.size(), \
        [&](string_t blob, int64_t cell, ValidityMask &mask, idx_t idx) -> bool { \
            Temporal *t = BlobToTemp(blob); \
            int r = FN(t, static_cast<H3Index>(cell)); \
            free(t); \
            if (r < 0) { mask.SetInvalid(idx); return false; } \
            return IntToBool(r); \
        }); \
}

TH3_EA_T_H3(Ever_eq_th3index_h3index,    ever_eq_th3index_h3index)
TH3_EA_T_H3(Ever_ne_th3index_h3index,    ever_ne_th3index_h3index)
TH3_EA_T_H3(Always_eq_th3index_h3index,  always_eq_th3index_h3index)
TH3_EA_T_H3(Always_ne_th3index_h3index,  always_ne_th3index_h3index)

#undef TH3_EA_T_H3

#define TH3_EA_T_T(NAME, FN) \
void H3IndexFunctions::NAME(DataChunk &args, ExpressionState &state, Vector &result) { \
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, bool>( \
        args.data[0], args.data[1], result, args.size(), \
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> bool { \
            Temporal *t1 = BlobToTemp(a); \
            Temporal *t2 = BlobToTemp(b); \
            int r = FN(t1, t2); \
            free(t1); free(t2); \
            if (r < 0) { mask.SetInvalid(idx); return false; } \
            return IntToBool(r); \
        }); \
}

TH3_EA_T_T(Ever_eq_th3index_th3index,    ever_eq_th3index_th3index)
TH3_EA_T_T(Ever_ne_th3index_th3index,    ever_ne_th3index_th3index)
TH3_EA_T_T(Always_eq_th3index_th3index,  always_eq_th3index_th3index)
TH3_EA_T_T(Always_ne_th3index_th3index,  always_ne_th3index_th3index)

#undef TH3_EA_T_T

/* =====================================================================
 * Temporal equality / inequality — `Temporal *fn(...)` returning tbool
 * ===================================================================== */

#define TH3_T_H3_T_TEMP(NAME, FN) \
void H3IndexFunctions::NAME(DataChunk &args, ExpressionState &state, Vector &result) { \
    BinaryExecutor::ExecuteWithNulls<int64_t, string_t, string_t>( \
        args.data[0], args.data[1], result, args.size(), \
        [&](int64_t cell, string_t blob, ValidityMask &mask, idx_t idx) -> string_t { \
            Temporal *t = BlobToTemp(blob); \
            Temporal *r = FN(static_cast<H3Index>(cell), t); \
            free(t); \
            if (!r) { mask.SetInvalid(idx); return string_t(); } \
            return TempToBlob(result, r); \
        }); \
}

TH3_T_H3_T_TEMP(Teq_h3index_th3index,  teq_h3index_th3index)
TH3_T_H3_T_TEMP(Tne_h3index_th3index,  tne_h3index_th3index)

#undef TH3_T_H3_T_TEMP

#define TH3_T_T_H3_TEMP(NAME, FN) \
void H3IndexFunctions::NAME(DataChunk &args, ExpressionState &state, Vector &result) { \
    BinaryExecutor::ExecuteWithNulls<string_t, int64_t, string_t>( \
        args.data[0], args.data[1], result, args.size(), \
        [&](string_t blob, int64_t cell, ValidityMask &mask, idx_t idx) -> string_t { \
            Temporal *t = BlobToTemp(blob); \
            Temporal *r = FN(t, static_cast<H3Index>(cell)); \
            free(t); \
            if (!r) { mask.SetInvalid(idx); return string_t(); } \
            return TempToBlob(result, r); \
        }); \
}

TH3_T_T_H3_TEMP(Teq_th3index_h3index,  teq_th3index_h3index)
TH3_T_T_H3_TEMP(Tne_th3index_h3index,  tne_th3index_h3index)

#undef TH3_T_T_H3_TEMP

#define TH3_T_T_T_TEMP(NAME, FN) \
void H3IndexFunctions::NAME(DataChunk &args, ExpressionState &state, Vector &result) { \
    BinaryExecutor::ExecuteWithNulls<string_t, string_t, string_t>( \
        args.data[0], args.data[1], result, args.size(), \
        [&](string_t a, string_t b, ValidityMask &mask, idx_t idx) -> string_t { \
            Temporal *t1 = BlobToTemp(a); \
            Temporal *t2 = BlobToTemp(b); \
            Temporal *r = FN(t1, t2); \
            free(t1); free(t2); \
            if (!r) { mask.SetInvalid(idx); return string_t(); } \
            return TempToBlob(result, r); \
        }); \
}

TH3_T_T_T_TEMP(Teq_th3index_th3index,  teq_th3index_th3index)
TH3_T_T_T_TEMP(Tne_th3index_th3index,  tne_th3index_th3index)

#undef TH3_T_T_T_TEMP

/* =====================================================================
 * Registration
 * ===================================================================== */

void H3IndexTypes::RegisterScalarFunctions(ExtensionLoader &loader) {
    const auto H3 = H3INDEX();
    const auto TH3 = TH3INDEX();
    const auto V = LogicalType::VARCHAR;
    const auto B = LogicalType::BOOLEAN;
    const auto I32 = LogicalType::INTEGER;
    const auto I64 = LogicalType::BIGINT;
    const auto TS = LogicalType::TIMESTAMP_TZ;

    /* --- I/O scalar text helpers --- */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "h3IndexFromText", {V}, H3, H3IndexFunctions::H3index_from_text));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "h3IndexAsText", {H3}, V, H3IndexFunctions::H3index_as_text));

    /* --- Constructor --- */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3index", {H3, TS}, TH3, H3IndexFunctions::Th3index_make));

    /* --- Accessors --- */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "startValue", {TH3}, H3, H3IndexFunctions::Th3index_start_value));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "endValue", {TH3}, H3, H3IndexFunctions::Th3index_end_value));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "valueN", {TH3, I32}, H3, H3IndexFunctions::Th3index_value_n));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "values", {TH3}, LogicalType::LIST(H3), H3IndexFunctions::Th3index_values));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "valueAtTimestamp", {TH3, TS}, H3, H3IndexFunctions::Th3index_value_at_timestamptz));

    /* --- Casts to/from other temporal types ---
     *
     * `th3index(tbigint)` / `tbigint(th3index)` round-trip the
     * 64-bit cell id through a generic temporal-bigint carrier.
     * MobilityDuck does not currently expose a `tbigint` type
     * (deferred until the larger temporal-pgtypes work lands), so
     * these two overloads stay unregistered.  Re-enable once
     * `TemporalTypes::TBIGINT()` is published.
     */
    /* Note: tgeompoint/tgeogpoint variants take a resolution arg. */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3index", {TgeogpointType::TGEOGPOINT(), I32}, TH3, H3IndexFunctions::Tgeogpoint_to_th3index));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3index", {TgeompointType::TGEOMPOINT(), I32}, TH3, H3IndexFunctions::Tgeompoint_to_th3index));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "tgeogpoint", {TH3}, TgeogpointType::TGEOGPOINT(), H3IndexFunctions::Th3index_to_tgeogpoint));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "tgeompoint", {TH3}, TgeompointType::TGEOMPOINT(), H3IndexFunctions::Th3index_to_tgeompoint));

    /* --- Ever / always predicates --- */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "everEq", {H3, TH3}, B, H3IndexFunctions::Ever_eq_h3index_th3index));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "everEq", {TH3, H3}, B, H3IndexFunctions::Ever_eq_th3index_h3index));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "everEq", {TH3, TH3}, B, H3IndexFunctions::Ever_eq_th3index_th3index));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "everNe", {H3, TH3}, B, H3IndexFunctions::Ever_ne_h3index_th3index));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "everNe", {TH3, H3}, B, H3IndexFunctions::Ever_ne_th3index_h3index));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "everNe", {TH3, TH3}, B, H3IndexFunctions::Ever_ne_th3index_th3index));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "alwaysEq", {H3, TH3}, B, H3IndexFunctions::Always_eq_h3index_th3index));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "alwaysEq", {TH3, H3}, B, H3IndexFunctions::Always_eq_th3index_h3index));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "alwaysEq", {TH3, TH3}, B, H3IndexFunctions::Always_eq_th3index_th3index));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "alwaysNe", {H3, TH3}, B, H3IndexFunctions::Always_ne_h3index_th3index));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "alwaysNe", {TH3, H3}, B, H3IndexFunctions::Always_ne_th3index_h3index));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "alwaysNe", {TH3, TH3}, B, H3IndexFunctions::Always_ne_th3index_th3index));

    /* --- Temporal equality / inequality (returns tbool) --- */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "tEq", {H3, TH3}, TemporalTypes::TBOOL(), H3IndexFunctions::Teq_h3index_th3index));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "tEq", {TH3, H3}, TemporalTypes::TBOOL(), H3IndexFunctions::Teq_th3index_h3index));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "tEq", {TH3, TH3}, TemporalTypes::TBOOL(), H3IndexFunctions::Teq_th3index_th3index));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "tNe", {H3, TH3}, TemporalTypes::TBOOL(), H3IndexFunctions::Tne_h3index_th3index));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "tNe", {TH3, H3}, TemporalTypes::TBOOL(), H3IndexFunctions::Tne_th3index_h3index));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "tNe", {TH3, TH3}, TemporalTypes::TBOOL(), H3IndexFunctions::Tne_th3index_th3index));

    /* --- H3 cell properties --- */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexGetResolution", {TH3}, TemporalTypes::TINT(), H3IndexFunctions::Th3index_get_resolution));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexGetBaseCellNumber", {TH3}, TemporalTypes::TINT(), H3IndexFunctions::Th3index_get_base_cell_number));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexIsValidCell", {TH3}, TemporalTypes::TBOOL(), H3IndexFunctions::Th3index_is_valid_cell));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexIsResClassIII", {TH3}, TemporalTypes::TBOOL(), H3IndexFunctions::Th3index_is_res_class_iii));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexIsPentagon", {TH3}, TemporalTypes::TBOOL(), H3IndexFunctions::Th3index_is_pentagon));

    /* --- Hierarchy --- */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexCellToParent", {TH3, I32}, TH3, H3IndexFunctions::Th3index_cell_to_parent));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexCellToParentNext", {TH3}, TH3, H3IndexFunctions::Th3index_cell_to_parent_next));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexCellToCenterChild", {TH3, I32}, TH3, H3IndexFunctions::Th3index_cell_to_center_child));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexCellToCenterChildNext", {TH3}, TH3, H3IndexFunctions::Th3index_cell_to_center_child_next));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexCellToChildPos", {TH3, I32}, TH3, H3IndexFunctions::Th3index_cell_to_child_pos));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexChildPosToCell", {TH3, TH3, I32}, TH3, H3IndexFunctions::Th3index_child_pos_to_cell));

    /* --- Geometry / boundary --- */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexCellToBoundary", {TH3}, TgeompointType::TGEOMPOINT(), H3IndexFunctions::Th3index_cell_to_boundary));

    /* --- Directed edges --- */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexAreNeighborCells", {TH3, TH3}, TemporalTypes::TBOOL(), H3IndexFunctions::Th3index_are_neighbor_cells));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexCellsToDirectedEdge", {TH3, TH3}, TH3, H3IndexFunctions::Th3index_cells_to_directed_edge));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexIsValidDirectedEdge", {TH3}, TemporalTypes::TBOOL(), H3IndexFunctions::Th3index_is_valid_directed_edge));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexGetDirectedEdgeOrigin", {TH3}, TH3, H3IndexFunctions::Th3index_get_directed_edge_origin));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexGetDirectedEdgeDestination", {TH3}, TH3, H3IndexFunctions::Th3index_get_directed_edge_destination));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexDirectedEdgeToBoundary", {TH3}, TgeompointType::TGEOMPOINT(), H3IndexFunctions::Th3index_directed_edge_to_boundary));

    /* --- Vertices --- */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexCellToVertex", {TH3, I32}, TH3, H3IndexFunctions::Th3index_cell_to_vertex));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexVertexToLatlng", {TH3}, TgeompointType::TGEOMPOINT(), H3IndexFunctions::Th3index_vertex_to_latlng));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexIsValidVertex", {TH3}, TemporalTypes::TBOOL(), H3IndexFunctions::Th3index_is_valid_vertex));

    /* --- Grid traversal --- */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexGridDistance", {TH3, TH3}, TemporalTypes::TINT(), H3IndexFunctions::Th3index_grid_distance));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexCellToLocalIj", {TH3, TH3}, TH3, H3IndexFunctions::Th3index_cell_to_local_ij));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexLocalIjToCell", {TH3, TH3}, TH3, H3IndexFunctions::Th3index_local_ij_to_cell));

    /* --- Cell area / edge length / great-circle distance --- */
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexCellArea", {TH3, V}, TemporalTypes::TFLOAT(), H3IndexFunctions::Th3index_cell_area));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "th3indexEdgeLength", {TH3, V}, TemporalTypes::TFLOAT(), H3IndexFunctions::Th3index_edge_length));
    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "tgeogpointGreatCircleDistance", {TgeogpointType::TGEOGPOINT(), TgeogpointType::TGEOGPOINT(), V},
        TemporalTypes::TFLOAT(), H3IndexFunctions::Tgeogpoint_great_circle_distance));
}

} // namespace duckdb
