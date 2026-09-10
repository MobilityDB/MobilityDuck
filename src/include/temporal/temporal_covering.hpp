#pragma once

#include <string>

namespace duckdb {

/* A covering column TemporalParquet declares for a temporal column: the struct
 * column <column><column_suffix> carries the bounds named in `bounds` */
struct TemporalCovering {
    const char *key;               /* bbox, tspan or vspan; nullptr ends a list */
    const char *column_suffix;     /* the suffix naming the struct column */
    const char *const *bounds;     /* the bound names, ended by nullptr */
};

/* The TemporalParquet description a temporal base type fixes */
struct TemporalCoveringType {
    const char *base_type;
    const char *edges;                 /* GeoParquet edges, nullptr when the base fixes none */
    const TemporalCovering *coverings; /* ended by a covering whose key is nullptr */
};

/* Return the description of @p base_type, or nullptr for a type TemporalParquet
 * gives no covering. The table is generated from the MEOS-API catalog by
 * tools/codegen_duck_udfs.py into src/generated/generated_temporal_covering.cpp */
const TemporalCoveringType *TemporalCoveringOf(const std::string &base_type);

} // namespace duckdb
