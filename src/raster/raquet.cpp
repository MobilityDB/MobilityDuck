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

/* Pixel type names, as in the MobilityDB raster SQL surface. The name table is
 * MEOS's (raquet_pixtype_from_string), so a new pixel type reaches every binding
 * at once instead of being re-tabulated here. An unknown name raises a MEOS
 * error, which MobilityduckMeosErrorHandler turns into an InvalidInputException.
 * The band's own dimensions and length are validated by the MEOS entry points
 * that receive them, so this file carries no copy of those checks. */
MeosPixType NameToPixType(string_t name) {
    std::string s(name.GetData(), name.GetSize());   /* string_t is not NUL-terminated */
    return raquet_pixtype_from_string(s.c_str());
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
 * Constructor
 * ===================================================================== */

/* raquet(pixels, width, height, cell, pixtype[, nodata]): the six-argument
 * form enables nodata filtering with the given sentinel, the five-argument
 * form leaves the tile without one. raquet_make takes whether the tile has a
 * nodata value as a parameter of its own, which its PostgreSQL wrapper sets
 * from whether the nodata argument is NULL and the catalog binds to no SQL
 * argument, so this constructor is not generated. */
void RaquetFunctions::Raquet_constructor(
    DataChunk &args, ExpressionState &state, Vector &result)
{
    const idx_t row_count = args.size();
    const bool has_nodata_arg = args.ColumnCount() > 5;
    for (idx_t c = 0; c < args.ColumnCount(); c++) args.data[c].Flatten(row_count);

    auto pixels  = FlatVector::GetData<string_t>(args.data[0]);
    auto width   = FlatVector::GetData<int32_t>(args.data[1]);
    auto height  = FlatVector::GetData<int32_t>(args.data[2]);
    auto cell    = FlatVector::GetData<int64_t>(args.data[3]);
    auto pixtype = FlatVector::GetData<string_t>(args.data[4]);
    auto out     = FlatVector::GetData<string_t>(result);
    auto &out_validity = FlatVector::Validity(result);

    for (idx_t row = 0; row < row_count; row++) {
        bool any_null = false;
        for (idx_t c = 0; c < args.ColumnCount(); c++) {
            if (c == 5) continue;   /* nodata NULL means "no nodata value" */
            if (!FlatVector::Validity(args.data[c]).RowIsValid(row)) any_null = true;
        }
        if (any_null) { out_validity.SetInvalid(row); continue; }

        bool has_nodata = has_nodata_arg &&
            FlatVector::Validity(args.data[5]).RowIsValid(row);
        double nodata = has_nodata
            ? FlatVector::GetData<double>(args.data[5])[row] : 0.0;

        MeosPixType pt = NameToPixType(pixtype[row]);

        Raquet *rq = raquet_make(
            static_cast<uint64_t>(cell[row]), width[row], height[row],
            pt, nodata, has_nodata,
            reinterpret_cast<const uint8_t *>(pixels[row].GetData()),
            static_cast<size_t>(pixels[row].GetSize()));
        if (!rq) { out_validity.SetInvalid(row); continue; }
        out[row] = RaquetToBlob(result, rq);
    }
    if (row_count == 1) result.SetVectorType(VectorType::CONSTANT_VECTOR);
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

void RaquetTypes::RegisterScalarFunctions(ExtensionLoader &loader) {
    const auto RQ  = raquet();
    const auto QB  = QuadbinTypes::quadbin();
    const auto BLB = LogicalType::BLOB;
    const auto I32 = LogicalType::INTEGER;
    const auto D   = LogicalType::DOUBLE;
    const auto V   = LogicalType::VARCHAR;

    duckdb::RegisterSerializedScalarFunction(loader, ScalarFunction(
        "raquet", {BLB, I32, I32, QB, V}, RQ,
        RaquetFunctions::Raquet_constructor));
    /* A NULL nodata value states that the tile has none, so the constructor
     * reads its NULL arguments itself rather than the call answering NULL */
    ScalarFunction raquet_with_nodata(
        "raquet", {BLB, I32, I32, QB, V, D}, RQ,
        RaquetFunctions::Raquet_constructor);
    raquet_with_nodata.null_handling = FunctionNullHandling::SPECIAL_HANDLING;
    duckdb::RegisterSerializedScalarFunction(loader, raquet_with_nodata);
}

} // namespace duckdb
