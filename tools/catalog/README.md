# MEOS catalog

`meos-idl.json` is the MEOS-API catalog that `tools/codegen_duck_udfs.py` reads to
generate `src/generated/*.cpp`. It is **not** vendored in this repository — it is
derived from the MobilityDB commit recorded as `_MEOS_REF` in
`vcpkg_ports/meos/portfile.cmake`, which is also the commit the vcpkg MEOS port
builds libmeos from.

- **In CI**, `.github/workflows/meos-surface-refresh.yml` derives it via the shared
  `MobilityDB/MEOS-API/.github/actions/provision-meos` action from MobilityDB
  **master**, regenerates `src/generated/`, advances `_MEOS_REF` to the commit it
  derived from, and opens a pull request carrying both. It runs on a daily schedule
  and on demand. No workflow fails a build on drift: the refresh pull request is how
  drift surfaces, and the distribution pipeline then gates that pull request like
  any other change.

- **Locally**, derive it from the recorded `_MEOS_REF` commit with the MEOS-API
  generator:

  ```sh
  MDB_SRC_ROOT=<mobilitydb-checkout> \
    python3 <meos-api>/run.py <mobilitydb-checkout>/meos/include
  cp <meos-api>/output/meos-idl.json tools/catalog/meos-idl.json
  python3 tools/codegen_duck_udfs.py \
    tools/catalog/meos-idl.json src/generated/generated_temporal_udfs.cpp
  ```

The committed `src/generated/*.cpp` is the buildable snapshot the distribution
pipeline compiles. It and libmeos come from the same recorded commit, so the surface
never calls into a libmeos that lacks it. Master moves ahead of that commit between
refreshes, so a derivation run against master's tip can differ from the committed
output; the daily refresh is what closes that gap, advancing the surface and the
recorded commit together.
