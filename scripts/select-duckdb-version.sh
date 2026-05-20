#!/usr/bin/env bash
# Switch the MobilityDuck submodules (duckdb, duckdb-spatial, extension-ci-tools)
# to the set that targets a given DuckDB upstream version.
#
# This is the foundation for multi-DuckDB-version support — same pattern as
# MobilityDB-on-Postgres compiling against PG 13-18 from one source. After
# running this script, the `make release` / `make debug` flow builds the
# MobilityDuck extension binary for the selected DuckDB version. The
# committed default in the repository targets the LTS release; CI matrix
# expansion (a follow-up PR) builds every supported version in parallel.
#
# Usage:
#   scripts/select-duckdb-version.sh v1.4.4
#   scripts/select-duckdb-version.sh v1.5.2
#
# The script:
#   1. Maps the requested DuckDB version to matching `duckdb-spatial` and
#      `extension-ci-tools` refs that target that DuckDB ABI.
#   2. Updates each submodule's tracked branch and fetches the new tip.
#   3. Reports the resulting SHAs so the caller can pin them if needed.
#
# It does NOT modify .gitmodules; the change is per-checkout only. Commit
# the resulting submodule SHA bump (in the parent repository) if you want
# the chosen version to be the new committed default.

set -eu -o pipefail

VERSION="${1:-}"
if [[ -z "$VERSION" ]]; then
    cat <<EOF >&2
Usage: $0 <duckdb-version>

Supported versions:
  v1.4.4   (LTS, ecosystem alignment target)
  v1.5.2   (latest 1.5.x line, opt-in)

The chosen version pins:
  duckdb              -> the matching DuckDB tag
  duckdb-spatial      -> the matching v<series>-<codename> branch HEAD
  extension-ci-tools  -> the matching v<series>-<codename> branch HEAD

For full context see doc/multi-duckdb-version.md.
EOF
    exit 2
fi

# Per-version manifest. Add a row to extend the supported set.
case "$VERSION" in
    v1.4.4)
        DUCKDB_REF="v1.4.4"
        SPATIAL_REF="v1.4-andium"
        CI_TOOLS_REF="v1.4.4"
        ;;
    v1.5.2)
        DUCKDB_REF="v1.5.2"
        SPATIAL_REF="v1.5-variegata"
        CI_TOOLS_REF="v1.5-variegata"
        ;;
    *)
        echo "Unsupported DuckDB version: $VERSION" >&2
        echo "Edit scripts/select-duckdb-version.sh to add a manifest row." >&2
        exit 2
        ;;
esac

cd "$(git rev-parse --show-toplevel)"

echo "[select-duckdb-version] target = DuckDB $VERSION"
echo "[select-duckdb-version]   duckdb              -> $DUCKDB_REF"
echo "[select-duckdb-version]   duckdb-spatial      -> $SPATIAL_REF"
echo "[select-duckdb-version]   extension-ci-tools  -> $CI_TOOLS_REF"

switch_submodule() {
    local path="$1" ref="$2"
    if [[ ! -d "$path/.git" && ! -f "$path/.git" ]]; then
        git submodule update --init "$path"
    fi
    (
        cd "$path"
        git fetch --tags origin "$ref"
        git checkout "$ref"
    )
}

switch_submodule duckdb              "$DUCKDB_REF"
switch_submodule duckdb-spatial      "$SPATIAL_REF"
switch_submodule extension-ci-tools  "$CI_TOOLS_REF"

echo
echo "[select-duckdb-version] resulting submodule SHAs:"
git submodule status
echo
echo "[select-duckdb-version] To make this the committed default for the parent"
echo "  repository, 'git add' each submodule path and commit."
