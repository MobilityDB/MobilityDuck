// MobilityDuck `GEOGRAPHY` LogicalType — see `doc/geography-boundary.md` for
// the full boundary design. This translation unit ships the foundation only:
// the LogicalType alias and its registration with the ExtensionLoader.
//
// Casts (GEOMETRY ⇄ GEOGRAPHY, GEOGRAPHY ⇄ TGEOGPOINT) and the I/O UDFs
// (ST_GeogFromText, ST_AsText, ST_AsBinary, ST_GeogFromBinary) land in
// follow-up PRs that build on this registration.
//
// Include order mirrors the existing static-type pattern (see stbox.cpp):
// meos_wrapper_simple.hpp first so meos.h's Interval/Timestamp typedefs land
// in C linkage before any DuckDB header pulls in the duckdb:: variants.
#include "meos_wrapper_simple.hpp"

#include "common.hpp"
#include "geo/geography.hpp"

#include "duckdb/common/types.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

LogicalType GeographyType::GEOGRAPHY() {
    LogicalType type(LogicalTypeId::BLOB);
    type.SetAlias("GEOGRAPHY");
    return type;
}

void GeographyType::RegisterType(ExtensionLoader &loader) {
    loader.RegisterType("GEOGRAPHY", GEOGRAPHY());
}

} // namespace duckdb
