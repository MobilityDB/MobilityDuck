PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=mobilityduck
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

test_release_internal:
	TZ=UTC ./build/release/$(TEST_PATH) "$(PROJ_DIR)test/*"
test_debug_internal:
	TZ=UTC ./build/debug/$(TEST_PATH) "$(PROJ_DIR)test/*"
test_reldebug_internal:
	TZ=UTC ./build/reldebug/$(TEST_PATH) "$(PROJ_DIR)test/*"