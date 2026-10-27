# This file is included by DuckDB's build system. It specifies which extension to load

# Extension from this repo
duckdb_extension_load(mobilityduck
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    LOAD_TESTS
)

# Any extra extensions that should be built
# e.g.: duckdb_extension_load(json)

# The spatial extension is not built here: MobilityDuck stacks on the spatial
# extension DuckDB publishes, which a session loads beside it, and the tests
# that call its ST_ functions install and load that published extension.

duckdb_extension_load(icu)
