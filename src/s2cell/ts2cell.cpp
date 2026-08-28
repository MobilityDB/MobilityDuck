/* MobilityDuck binding for the MEOS S2 cell index types (s2cell + ts2cell).
 * Registers the two DuckDB types and the text I/O each of them casts through;
 * the callable surface is generated from the catalog into src/generated.
 *
 * S2CELL is surfaced as BIGINT (the 64-bit cell id reinterprets losslessly).
 * TS2CELL is the temporal cell index, stored as a Temporal* blob (BLOB).
 * The analogue of QUADBIN / TQUADBIN for the Google S2 spherical DGGS.
 *
 * The text form of a cell is the S2 hex token (47c3c3), which s2cell_in and
 * s2cell_out own: the cast delegates to them rather than reading the id as a
 * number, so the spelling a MobilityDB user writes is the spelling DuckDB
 * accepts.
 */

#include "s2cell/ts2cell.hpp"
#include "temporal/temporal.hpp"
#include "tydef.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

namespace duckdb {

LogicalType S2cellTypes::s2cell() {
    LogicalType type = LogicalType::BIGINT;
    type.SetAlias("s2cell");
    return type;
}

LogicalType S2cellTypes::ts2cell() {
    auto type = LogicalType(LogicalTypeId::BLOB);
    type.SetAlias("ts2cell");
    return type;
}

void S2cellTypes::RegisterTypes(ExtensionLoader &loader) {
    loader.RegisterType("s2cell",  s2cell());
    loader.RegisterType("ts2cell", ts2cell());
}

void S2cellTypes::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, s2cell(),
        S2cellFunctions::S2cell_in_cast);
    RegisterMeosCastFunction(loader, s2cell(), LogicalType::VARCHAR,
        S2cellFunctions::S2cell_out_cast);
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, ts2cell(),
        S2cellFunctions::Ts2cell_in_cast);
    RegisterMeosCastFunction(loader, ts2cell(), LogicalType::VARCHAR,
        S2cellFunctions::Ts2cell_out_cast);
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
        result, reinterpret_cast<const char *>(t), sz);
    free(t);
    return out;
}

} // namespace

/* =====================================================================
 * In / out — static S2CELL cell
 * ===================================================================== */

bool S2cellFunctions::S2cell_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, int64_t>(
        source, result, count,
        [&](string_t s) -> int64_t {
            std::string str(s.GetData(), s.GetSize());
            return static_cast<int64_t>(s2cell_in(str.c_str()));
        });
    return true;
}

bool S2cellFunctions::S2cell_out_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<int64_t, string_t>(
        source, result, count,
        [&](int64_t v) -> string_t {
            char *str = s2cell_out(static_cast<uint64_t>(v));
            std::string copy(str);
            free(str);
            return StringVector::AddString(result, copy);
        });
    return true;
}

/* =====================================================================
 * In / out — TS2CELL temporal value
 * ===================================================================== */

bool S2cellFunctions::Ts2cell_in_cast(
    Vector &source, Vector &result, idx_t count, CastParameters &parameters)
{
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t s) -> string_t {
            std::string str(s.GetData(), s.GetSize());
            Temporal *t = ts2cell_in(str.c_str());
            return TempToBlob(result, t);
        });
    return true;
}

bool S2cellFunctions::Ts2cell_out_cast(
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
