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
#
# LoadInternal also calls ExtensionHelper::AutoLoadExtension(db, "icu") so
# the timezone option is honoured. Autoload looks for the extension on disk
# at $HOME/.duckdb/extensions/<duckdb_version>/<platform>/icu.duckdb_extension
# and falls back to a hub download. That fails both inside the linux_amd64
# test docker container (empty path, no network egress) and on the macOS
# osx_arm64 test runner (hub icu not reliably resolvable). We copy the
# icu.duckdb_extension that was built locally as part of this extension's
# build (declared in extension_config.cmake) into the expected path,
# matched to the DuckDB platform string, before running the unittester.
DUCKDB_VERSION_TAG := v1.4.4

define stage_icu
	@if [ -f ./build/$(1)/extension/icu/icu.duckdb_extension ]; then \
	  case "$$(uname -s)-$$(uname -m)" in \
	    Linux-x86_64)  platform=linux_amd64 ;; \
	    Linux-aarch64) platform=linux_arm64 ;; \
	    Darwin-arm64)  platform=osx_arm64 ;; \
	    Darwin-x86_64) platform=osx_amd64 ;; \
	    *)             platform=$$(uname -m) ;; \
	  esac; \
	  target=$$HOME/.duckdb/extensions/$(DUCKDB_VERSION_TAG)/$$platform; \
	  mkdir -p "$$target" && cp -f ./build/$(1)/extension/icu/icu.duckdb_extension "$$target/" && \
	  echo "Staged icu.duckdb_extension at $$target/"; \
	fi
endef

test_release_internal:
	$(call stage_icu,release)
	./build/release/$(TEST_PATH) "$(PROJ_DIR)test/*"
test_debug_internal:
	$(call stage_icu,debug)
	./build/debug/$(TEST_PATH) "$(PROJ_DIR)test/*"
test_reldebug_internal:
	$(call stage_icu,reldebug)
	./build/reldebug/$(TEST_PATH) "$(PROJ_DIR)test/*"
