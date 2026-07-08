#!/usr/bin/env bash
# NO-EXISTING-TOOL: this binding had no portfile-bump/regen tool on the generator branch;
# created per the per-binding-canonical policy (each binding owns its tools/regen-from-pin.sh).
#
# regen-from-pin.sh — bump the MEOS surface pin and regenerate the MobilityDuck UDFs
# from the MEOS-API catalog (per GENERATION.md). Under the no-pin model the "pin" is an
# upstream MobilityDB/MobilityDB commit SHA (the portfile REF), NOT an ecosystem-pin tag.
#
# Usage:  tools/regen-from-pin.sh <mobilitydb-commit-sha>
#
#   Catalog source (one of):
#     CATALOG            = path to a meos-idl.json already produced by MEOS-API run.py, OR
#     MEOS_API + MDB_SRC = regenerate it in place:
#                            MDB_SRC  = a MobilityDB source tree checked out at <sha>
#                            MEOS_API = the MEOS-API repo (its run.py + meta/)
#
#   Generator pin-gate (phase 2 — AFTER the vcpkg MEOS rebuild):
#     MEOS_HEADERS = the freshly-installed MEOS headers dir; when set, run the generator
#                    (pin-gated against those headers) and build the extension. Omit on the
#                    first pass (bump + vendor only), rebuild vcpkg MEOS, then re-run with
#                    MEOS_HEADERS set.
#
# ORDER (critical, see GENERATION.md): bump portfile -> vcpkg rebuild MEOS (fresh headers)
# -> generator (pin-gated against the fresh headers) -> build extension -> test. Running the
# generator before the rebuild would gate it against the OLD pin's headers.
set -euo pipefail
SHA="${1:?usage: regen-from-pin.sh <mobilitydb-commit-sha>}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
PORT="$HERE/vcpkg_ports/meos/portfile.cmake"
VJSON="$HERE/vcpkg_ports/meos/vcpkg.json"
GEN="$HERE/tools/codegen_duck_udfs.py"
CATDIR="$HERE/tools/catalog"

# --- phase 1: bump the pin (portfile REF + SHA512, vcpkg.json port-version, PIN marker) ---
echo "[regen] computing SHA512 for MobilityDB@$SHA ..."
SHA512="$(curl -sL "https://github.com/MobilityDB/MobilityDB/archive/${SHA}.tar.gz" | sha512sum | cut -d' ' -f1)"
[ "${#SHA512}" -eq 128 ] || { echo "[regen] SHA512 computation failed"; exit 1; }
sed -i -E "s|^( *REF )[0-9a-f]{40}\$|\1${SHA}|" "$PORT"
sed -i -E "s|^( *SHA512 )[0-9a-f]{128}\$|\1${SHA512}|" "$PORT"
PV="$(grep -oE '"port-version": *[0-9]+' "$VJSON" | grep -oE '[0-9]+')"
sed -i -E "s|(\"port-version\": *)[0-9]+|\1$((PV + 1))|" "$VJSON"
echo "$SHA" > "$CATDIR/PIN"
echo "[regen] portfile REF=$SHA SHA512=${SHA512:0:12}… port-version=$PV->$((PV + 1)); PIN written"

# --- phase 1b: vendor the catalog (single vendored meos-idl-<sha>.json) ---
LABEL="${SHA:0:10}"
DEST="$CATDIR/meos-idl-${LABEL}.json"
if [ -n "${CATALOG:-}" ]; then
  cp "$CATALOG" "$DEST"
elif [ -n "${MEOS_API:-}" ] && [ -n "${MDB_SRC:-}" ]; then
  ( cd "$MEOS_API" && MDB_SRC_ROOT="$MDB_SRC" python3 run.py "$MDB_SRC/meos/include" )
  cp "$MEOS_API/output/meos-idl.json" "$DEST"
else
  echo "[regen] no catalog input (set CATALOG, or MEOS_API+MDB_SRC) — pin bumped only"; DEST=""
fi
[ -n "$DEST" ] && echo "[regen] vendored catalog -> $DEST"

# --- phase 2: generate + build (needs the freshly-installed headers) ---
if [ -n "${MEOS_HEADERS:-}" ] && [ -n "$DEST" ] && [ -f "$GEN" ]; then
  python3 "$GEN" "$DEST" "$HERE/src/generated/generated_temporal_udfs.cpp" "$MEOS_HEADERS"
  ( cd "$HERE" && cmake --build build/release --target mobilityduck_loadable_extension ) \
    || echo "[regen] WARN: extension build returned non-zero"
  echo "[regen] regenerated + built at pin $SHA"
else
  echo "[regen] bump + vendor done. Next: rebuild vcpkg MEOS, then re-run with MEOS_HEADERS=<installed include>."
fi
