PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=mobilityduck
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

# MobilityDB / PostgreSQL regression output for timestamptz is stable when the session uses a
# fixed offset. Golden files in test/sql use UTC (+00). Without this, local TZ (e.g. +01) breaks
# string comparisons. Override unittest targets to match MobilityDB-style baselines.
test_release_internal:
	TZ=UTC ./build/release/$(TEST_PATH) "$(PROJ_DIR)test/*"
test_debug_internal:
	TZ=UTC ./build/debug/$(TEST_PATH) "$(PROJ_DIR)test/*"
test_reldebug_internal:
	TZ=UTC ./build/reldebug/$(TEST_PATH) "$(PROJ_DIR)test/*"