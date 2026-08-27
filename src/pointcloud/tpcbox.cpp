/* MobilityDuck binding for the MEOS temporal point cloud bounding box (tpcbox).
 *
 * tpcbox is an stbox — a space and time extent with an SRID — carrying the pcid
 * of the schema its coordinates are written in. It is a fixed-layout value
 * surfaced as a BLOB, the same shape as stbox and tbox.
 *
 * Only the type registration + text I/O boundary is hand-written here; the
 * operation surface is generated from the MEOS-API catalog into src/generated/.
 */

#include "pointcloud/tpcbox.hpp"

#include "common.hpp"
#include "tydef.hpp"
#include "duckdb/common/types/blob.hpp"
#include "duckdb/function/function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "mobilityduck/meos_exec_serial.hpp"

namespace duckdb {

LogicalType TpcboxType::tpcbox() {
    LogicalType type(LogicalTypeId::BLOB);
    type.SetAlias("tpcbox");
    return type;
}

void TpcboxType::RegisterType(ExtensionLoader &loader) {
    loader.RegisterType("tpcbox", tpcbox());
}

/* The blob holds the fixed-size TPCBox itself, so the value is copied in whole
 * and read back in place — the same marshalling stbox and tbox use. */
bool TpcboxFunctions::Tpcbox_in_cast(Vector &source, Vector &result, idx_t count,
                                     CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t input) -> string_t {
            std::string s = input.GetString();
            TPCBox *box = tpcbox_in(s.c_str());
            if (!box)
                throw InvalidInputException("Invalid tpcbox input: " + s);
            string_t stored = StringVector::AddStringOrBlob(
                result, reinterpret_cast<const char *>(box), sizeof(TPCBox));
            free(box);
            return stored;
        });
    return true;
}

bool TpcboxFunctions::Tpcbox_out_cast(Vector &source, Vector &result, idx_t count,
                                      CastParameters &parameters) {
    UnaryExecutor::Execute<string_t, string_t>(
        source, result, count,
        [&](string_t blob) -> string_t {
            if (blob.GetSize() < sizeof(TPCBox))
                throw InvalidInputException("Invalid tpcbox value: insufficient size");
            TPCBox box;
            memcpy(&box, blob.GetData(), sizeof(TPCBox));
            char *str = tpcbox_out(&box, OUT_DEFAULT_DECIMAL_DIGITS);
            if (!str)
                throw InternalException("Failure in Tpcbox_out: tpcbox_out returned null");
            std::string s(str);
            free(str);
            return StringVector::AddString(result, s);
        });
    return true;
}

void TpcboxType::RegisterCastFunctions(ExtensionLoader &loader) {
    RegisterMeosCastFunction(loader, LogicalType::VARCHAR, tpcbox(),
                             TpcboxFunctions::Tpcbox_in_cast);
    RegisterMeosCastFunction(loader, tpcbox(), LogicalType::VARCHAR,
                             TpcboxFunctions::Tpcbox_out_cast);
}

} // namespace duckdb
