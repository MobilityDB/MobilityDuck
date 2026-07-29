#pragma once

// Canonical round-trip between a DuckDB blob (string_t) and a MEOS Temporal
// value. Every type binding shares this single definition, so the
// function-pointer registrations across the bindings all resolve to one body.

#include "meos_wrapper_simple.hpp"
#include "common.hpp"

namespace duckdb {

// Copy t into result's string heap and free t; returns the stored blob.
string_t TemporalToBlob(Vector &result, Temporal *t);

// Copy the blob bytes into a freshly malloc'd Temporal* owned by the caller.
Temporal *BlobToTemporal(string_t blob);

} // namespace duckdb
