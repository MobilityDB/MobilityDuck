PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=mobilityduck
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

# Single-timezone model (PGTZ-style): the extension's LoadInternal forces
# both MEOS (meos_initialize_timezone) and DuckDB (DBConfig::SetOptionByName
# "TimeZone") to Europe/Brussels.  Tests pass on any OS timezone — the
# extension is the single source of truth, no TZ env var needed.
test_release_internal:
	./build/release/$(TEST_PATH) "$(PROJ_DIR)test/*"
test_debug_internal:
	./build/debug/$(TEST_PATH) "$(PROJ_DIR)test/*"
test_reldebug_internal:
	./build/reldebug/$(TEST_PATH) "$(PROJ_DIR)test/*"