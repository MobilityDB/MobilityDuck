# This file is included by DuckDB's build system. It specifies which extension to load

# Extension from this repo
duckdb_extension_load(mobilityduck
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}
    LOAD_TESTS
)

# Any extra extensions that should be built
# e.g.: duckdb_extension_load(json)

# A commit of the spatial extension pairs with ONE DuckDB line, and the pairing
# a DuckDB release states is the GIT_TAG of its own
# .github/config/extensions/spatial.cmake at that tag. The submodule holds the
# commit the v1.4 line pairs with, so a build of the v1.5 line names the commit
# that line pairs with and one tree serves both.
if(DUCKDB_MAJOR_VERSION EQUAL 1 AND DUCKDB_MINOR_VERSION GREATER_EQUAL 5)
  duckdb_extension_load(spatial
      GIT_URL     https://github.com/duckdb/duckdb-spatial
      GIT_TAG     b68b309d371dba936c5bb362980e559b7756b16d
      INCLUDE_DIR src/spatial
  )
else()
  duckdb_extension_load(spatial
      SOURCE_DIR  ${CMAKE_CURRENT_LIST_DIR}/duckdb-spatial
      INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/duckdb-spatial/src/spatial
  )
endif()

duckdb_extension_load(icu)
