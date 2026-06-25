#!/usr/bin/env bash
# regen-from-pin.sh — regenerate the MobilityDuck UDFs from the MEOS catalog (per GENERATION.md).
#
# Usage:  tools/regen-from-pin.sh <pin>
#   env:  CATALOG     = path to meos-idl.json produced by MEOS-API run.py (required)
#         MEOS_HEADERS = the installed MEOS headers dir for the pin-gate (required by the generator)
#
# NOTE: the catalog generator `tools/codegen_duck_udfs.py` is the target (GENERATION.md); it
# lands generate-then-retire alongside the hand-written UDFs. Until it is in the tree this
# script documents the invocation. Invoked standalone, or by tools/ecosystem-generate.sh.
set -euo pipefail
PIN="${1:?usage: regen-from-pin.sh <pin>}"
CATALOG="${CATALOG:?set CATALOG to the meos-idl.json from MEOS-API run.py}"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
GEN="$HERE/tools/codegen_duck_udfs.py"

if [ ! -f "$GEN" ]; then
  echo "NOTE: $GEN not present yet (the catalog generator lands generate-then-retire — see GENERATION.md)."
  echo "      Once it is in the tree, this script regenerates src/generated/generated_temporal_udfs.cpp."
  exit 0
fi

# generator CLI: codegen_duck_udfs.py <catalog> <out.cpp> <headers-dir>
python3 "$GEN" "$CATALOG" "$HERE/src/generated/generated_temporal_udfs.cpp" "${MEOS_HEADERS:?set MEOS_HEADERS to the installed pin headers}"

# build-verify the extension (the in-repo fast build target)
( cd "$HERE" && cmake --build build/release --target mobilityduck_loadable_extension ) \
  || echo "WARN: MobilityDuck extension build returned non-zero"
echo "[mobilityduck] regenerated UDFs from catalog at pin $PIN"
