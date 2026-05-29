// Single definition of the canonical DuckDB-blob <-> MEOS Temporal round-trip
// shared by every type binding.

#include "temporal/temporal_blob.hpp"

#include <cstdlib>
#include <cstring>

namespace duckdb {

string_t TemporalToBlob(Vector &result, Temporal *t) {
    size_t sz = temporal_mem_size(t);
    string_t out = StringVector::AddStringOrBlob(
        result, reinterpret_cast<const char *>(t), sz);
    free(t);
    return out;
}

Temporal *BlobToTemporal(string_t blob) {
    size_t sz = blob.GetSize();
    uint8_t *copy = static_cast<uint8_t *>(malloc(sz));
    memcpy(copy, blob.GetData(), sz);
    return reinterpret_cast<Temporal *>(copy);
}

} // namespace duckdb
