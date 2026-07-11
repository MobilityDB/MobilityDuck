# MEOS catalog

`meos-idl.json` is the MEOS-API catalog that `tools/codegen_duck_udfs.py` reads to
generate `src/generated/*.cpp`. It is **not** vendored in this repository — it is
derived from MobilityDB so it can never drift from the libmeos the extension is
built against.

- **In CI**, `.github/workflows/generate.yml` derives it via the shared
  `MobilityDB/MEOS-API/.github/actions/provision-meos` action, using the MobilityDB
  commit pinned by the vcpkg MEOS port (`vcpkg_ports/meos/portfile.cmake` `REF`), then
  regenerates `src/generated/` and fails if the committed output has drifted.

- **Locally**, derive it from the same commit with the MEOS-API generator:

  ```sh
  MDB_SRC_ROOT=<mobilitydb-checkout> \
    python3 <meos-api>/run.py <mobilitydb-checkout>/meos/include
  cp <meos-api>/output/meos-idl.json tools/catalog/meos-idl.json
  python3 tools/codegen_duck_udfs.py \
    tools/catalog/meos-idl.json src/generated/generated_temporal_udfs.cpp
  ```

The committed `src/generated/*.cpp` is the buildable snapshot the distribution
pipeline compiles; the `generate` workflow keeps it equal to a fresh derivation.
