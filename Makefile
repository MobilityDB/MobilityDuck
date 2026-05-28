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
# LoadInternal auto-loads ICU so the named "Europe/Brussels" zone resolves.
# DuckDB autoloads ICU from
#   $HOME/.duckdb/extensions/<version>/<platform>/icu.duckdb_extension
# and otherwise falls back to a hub download — which fails inside the CI test
# docker (empty path, no network egress). So we stage the locally-built ICU
# into the expected path before the unittester runs. Version and platform are
# derived from the freshly-built duckdb binary (authoritative); the
# DUCKDB_VERSION_TAG and uname map are fallbacks only.
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

# Fast developer iteration: rebuild ONLY the MobilityDuck extension against the
# already-configured DuckDB build and vcpkg dependencies.  Run a full `make`
# (release) / `make debug` once; thereafter `make ext` / `make ext_debug`
# recompiles just the changed MobilityDuck translation units and relinks
# build/<config>/extension/mobilityduck/mobilityduck.duckdb_extension without
# rebuilding DuckDB, spatial, icu, parquet or the vcpkg packages.  See BUILDING.md.
.PHONY: ext ext_debug
ext:
	cmake --build build/release --target mobilityduck_loadable_extension --parallel
ext_debug:
	cmake --build build/debug --target mobilityduck_loadable_extension --parallel
