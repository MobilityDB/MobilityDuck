# Building MobilityDuck

MobilityDuck is a DuckDB extension. One CMake project builds DuckDB itself
(~1300 source files), the bundled `spatial` and `icu` extensions, and
MobilityDuck (~36 source files). The MEOS, GEOS, PROJ, GDAL and GSL
dependencies are built by vcpkg and cached.

## One-time full build

```bash
make            # release into build/release (use GEN=ninja for Ninja)
make debug      # debug into build/debug
```

The first run compiles DuckDB and the vcpkg dependencies from source and is the
slow one. The result is `build/release/extension/mobilityduck/mobilityduck.duckdb_extension`
and a `build/release/duckdb` shell with the extension linked in.

## Fast iteration on MobilityDuck

After the one-time build, rebuild only the extension:

```bash
make ext          # rebuild build/release extension only
make ext_debug    # rebuild build/debug extension only
```

`make ext` recompiles only the changed MobilityDuck translation units and
relinks the `.duckdb_extension`. DuckDB, `spatial`, `icu`, `parquet` and the
vcpkg packages are not recompiled, because they are unchanged in the build tree.

Two settings cut the per-change cost further and apply automatically:

- A precompiled header (`src/include/mobilityduck_pch.hpp`) parses the heavy
  DuckDB and MEOS headers once instead of in every translation unit.
- When `mold` is installed it is used to link the extension; the relink
  dominates a single-file change and mold is much faster than the default
  linker (`apt install mold`, or the equivalent for your platform). The build
  falls back to the default linker when mold is absent.

Keep the build tree in sync with the checked-out branch: switching branches (or
building a different worktree) against a tree configured for another state makes
CMake detect changed headers and recompile broadly. A persistent, path-independent
`ccache` keeps those recompiles cheap (see below).

## ccache across branches and worktrees

`ccache` caches compiled objects. Making it path-independent lets a fresh
worktree or a branch switch reuse DuckDB's unchanged objects instead of
recompiling them:

```bash
export CCACHE_DIR="$HOME/.ccache"
export CCACHE_BASEDIR="$PWD"
ccache -M 30G
ccache -o hash_dir=false -o sloppiness=locale,time_macros
```

## vcpkg dependency cache

vcpkg stores built packages in `~/.cache/vcpkg/archives` and reuses them across
clean build trees. MEOS, GEOS, PROJ and GDAL are rebuilt only when their version
or the MEOS pin in `vcpkg_ports/meos/portfile.cmake` changes — ordinary
MobilityDuck edits reuse the cache.

## Installing without building

End users do not build the extension. They install the distributed binary:

```sql
INSTALL mobilityduck FROM '<extension-repository>';
LOAD mobilityduck;
```
