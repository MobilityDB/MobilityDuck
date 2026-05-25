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
# build (declared in extension_config.cmake) into the expected path.
#
# Target DuckDB is the v1.4.x LTS line, with later versions (v1.5.x) supported
# in a multi-version matrix (PRs #166/#167) the same way MobilityDB supports
# PostgreSQL 13-18 — so the staging path must NOT hardcode the version or the
# platform. We derive both from the freshly-built duckdb binary (authoritative
# for whatever version/platform is actually being tested); DUCKDB_VERSION_TAG
# and the uname map below are kept only as fallbacks if that query is
# unavailable.
DUCKDB_VERSION_TAG := v1.4.4

define stage_icu
	@if [ -f ./build/$(1)/extension/icu/icu.duckdb_extension ]; then \
	  duckdb_bin=./build/$(1)/duckdb; \
	  version_tag=$$( [ -x "$$duckdb_bin" ] && "$$duckdb_bin" --version 2>/dev/null | grep -oE 'v[0-9]+\.[0-9]+\.[0-9]+' | head -1 ); \
	  platform=$$( [ -x "$$duckdb_bin" ] && echo 'PRAGMA platform;' | "$$duckdb_bin" -noheader -list 2>/dev/null | tr -d '[:space:]' ); \
	  [ -n "$$version_tag" ] || version_tag=$(DUCKDB_VERSION_TAG); \
	  if [ -z "$$platform" ]; then \
	    case "$$(uname -s)-$$(uname -m)" in \
	      Linux-x86_64)  platform=linux_amd64 ;; \
	      Linux-aarch64) platform=linux_arm64 ;; \
	      Darwin-arm64)  platform=osx_arm64 ;; \
	      Darwin-x86_64) platform=osx_amd64 ;; \
	      *)             platform=$$(uname -m) ;; \
	    esac; \
	  fi; \
	  target=$$HOME/.duckdb/extensions/$$version_tag/$$platform; \
	  mkdir -p "$$target" && cp -f ./build/$(1)/extension/icu/icu.duckdb_extension "$$target/" && \
	  echo "Staged icu.duckdb_extension at $$target/ (duckdb $$version_tag / $$platform)"; \
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
