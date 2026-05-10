PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=mobilityduck
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile

# Test runner uses a non-UTC timezone (Europe/Brussels) to verify that the
# extension's forced meos_initialize_timezone("Europe/Brussels") call makes
# tests pass regardless of the OS timezone.  This is the project policy:
# tests MUST pass with any system TZ; Brussels is the default runner TZ to
# surface bugs that UTC hides.
test_release_internal:
	TZ=Europe/Brussels ./build/release/$(TEST_PATH) "$(PROJ_DIR)test/*"
test_debug_internal:
	TZ=Europe/Brussels ./build/debug/$(TEST_PATH) "$(PROJ_DIR)test/*"
test_reldebug_internal:
	TZ=Europe/Brussels ./build/reldebug/$(TEST_PATH) "$(PROJ_DIR)test/*"