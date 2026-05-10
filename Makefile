PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=mobilityduck
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

# Test runner sets TZ=Europe/Brussels so that the OS timezone matches the
# hardcoded MEOS timezone (Europe/Brussels).  Brussels is a non-UTC zone
# chosen to surface bugs that UTC hides (e.g. off-by-one-hour errors).
# DuckDB session timestamps remain UTC (bare TIMESTAMPTZ outputs use +00);
# MEOS composite-type strings use Brussels (+01 in winter).
test_release_internal:
	TZ=Europe/Brussels ./build/release/$(TEST_PATH) "$(PROJ_DIR)test/*"
test_debug_internal:
	TZ=Europe/Brussels ./build/debug/$(TEST_PATH) "$(PROJ_DIR)test/*"
test_reldebug_internal:
	TZ=Europe/Brussels ./build/reldebug/$(TEST_PATH) "$(PROJ_DIR)test/*"