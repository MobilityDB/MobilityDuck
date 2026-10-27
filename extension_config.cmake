# This file is included by DuckDB's build system. It specifies which extension to load

# Extension from this repo
duckdb_extension_load(mobilityduck
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    LOAD_TESTS
)

# Any extra extensions that should be built
# e.g.: duckdb_extension_load(json)

# The spatial extension at the commit DuckDB v1.5.6 builds
# (.github/config/extensions/spatial.cmake). It carries the fix restricting the
# && spatial join rewrite to GEOMETRY operands (duckdb-spatial #853), without
# which the optimizer claims MobilityDuck's && joins. DuckDB v1.5.6 ships no
# patch for spatial.
duckdb_extension_load(spatial
    GIT_URL     https://github.com/duckdb/duckdb-spatial
    GIT_TAG     04270fe7bb4eb412765cce8afde0276a9d81d3ba
    INCLUDE_DIR src/spatial
)

duckdb_extension_load(icu)
