# This file is included by DuckDB's build system. It specifies which extension to load

# Extension from this repo
duckdb_extension_load(mobilityduck
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    LOAD_TESTS
)

# Any extra extensions that should be built
# e.g.: duckdb_extension_load(json)

# The spatial extension at the head of its v1.5-variegata branch, the line
# DuckDB v1.5.5 belongs to. It carries the fix restricting the && spatial join
# rewrite to GEOMETRY operands (duckdb-spatial #853), without which the
# optimizer claims MobilityDuck's && joins. APPLY_PATCHES applies the patches
# DuckDB v1.5.5 ships for spatial under .github/patches/extensions/spatial.
duckdb_extension_load(spatial
    GIT_URL     https://github.com/duckdb/duckdb-spatial
    GIT_TAG     c0c9fc9e1f8c8fa8bacac54ac692cc488b9661ee
    INCLUDE_DIR src/spatial
    APPLY_PATCHES
)

duckdb_extension_load(icu)
