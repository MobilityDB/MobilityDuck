# Multi-DuckDB-version support

MobilityDuck targets multiple upstream DuckDB versions from one source tree, mirroring how MobilityDB-on-Postgres supports PG 13-18 from one source. This document explains the model, the per-version manifest, and how to add a new supported version.

## Supported versions

| DuckDB version | Track | DuckDB-spatial branch | extension-ci-tools branch | Notes |
|---|---|---|---|---|
| **v1.4.4** | LTS (default) | `v1.4-andium` | `v1.4.4` | The ecosystem alignment target. Full GeoArrow / GeoParquet 1.1 spatial surface in the bundled `spatial` extension. |
| **v1.5.2** | Latest 1.5.x | `v1.5-variegata` | `v1.5-variegata` | Opt-in. Bundled `spatial` has a reduced function surface in this series; row-group pruning still works via the MVB v3 scalar-AND-chain recipe on `covering.bbox` struct leaves. |

The committed submodule SHAs in `main` target v1.4.4. Switching to a different supported version is a working-tree operation: the submodules move, the source code does not.

## How a build picks the version

DuckDB extension ABI is version-specific — a binary built against v1.4.4 cannot load into v1.5.2 and vice versa. MobilityDuck therefore produces a separate binary per supported version (one extension build per `(duckdb_version, platform)`).

Selection is driven by a single source: the SHAs that the three submodules — `duckdb`, `duckdb-spatial`, `extension-ci-tools` — point at when the build runs:

```
   ┌──────────────────────────────────────────────────────────┐
   │              MobilityDuck source (.cpp + .hpp)           │
   │   ──────────────────────────────────────────────────     │
   │      #if DUCKDB_VERSION_NUM >= 010500                    │
   │        … 1.5.x API call shape …                          │
   │      #else                                               │
   │        … 1.4.x API call shape …                          │
   │      #endif                                              │
   └─────────────────────────┬────────────────────────────────┘
                             │ same source for every version
                             ▼
   ┌──────────────────────────────────────────────────────────┐
   │   Submodules (parameterised by DUCKDB_VERSION)           │
   │                                                          │
   │   duckdb              → tag matching the chosen version  │
   │   duckdb-spatial      → matching v<series>-<codename>    │
   │   extension-ci-tools  → matching ci-tools tag/branch     │
   └─────────────────────────┬────────────────────────────────┘
                             │
                             ▼
   ┌──────────────────────────────────────────────────────────┐
   │   One binary per (duckdb_version, platform):             │
   │     extensions.duckdb.org/v1.4.4/linux_amd64/            │
   │       mobilityduck.duckdb_extension                      │
   │     extensions.duckdb.org/v1.5.2/linux_amd64/            │
   │       mobilityduck.duckdb_extension                      │
   │   `INSTALL mobilityduck FROM community;` resolves to     │
   │   the right artefact automatically — the community       │
   │   repository keys on `<duckdb_version>/<platform>/`.     │
   └──────────────────────────────────────────────────────────┘
```

## Switching the working tree to a different version

```bash
# Switch to v1.5.2 (downloads + checks out the matching submodule SHAs)
make set-duckdb-version DUCKDB_VERSION=v1.5.2

# Then the normal build flow targets that version
make release
```

Under the hood `make set-duckdb-version` calls `scripts/select-duckdb-version.sh`, which looks up the requested version in its per-version manifest and runs `git checkout <ref>` inside each submodule. The script does not modify `.gitmodules`; the change is per-checkout. To make the chosen version the new committed default for the parent repository, `git add` each submodule path and commit.

To return to the committed default:

```bash
git submodule update --recursive
```

## Adding a new supported version

Edit two files:

1. `scripts/select-duckdb-version.sh` — add a `case` arm with the DuckDB tag and matching `duckdb-spatial` / `extension-ci-tools` refs.
2. `doc/multi-duckdb-version.md` (this file) — add the row to the "Supported versions" table.

A new version typically also requires:

- One or more `#if DUCKDB_VERSION_NUM >= …` conditionals in the source where the new DuckDB release broke ABI (the `ExtensionUtil` → `ExtensionLoader` rename between 1.4 and 1.5 is one example; the spatial-function surface reshuffle is another).
- Per-version expected-output variants in `test/sql/*.test` for any case where DuckDB's own behaviour changed (timezone, casting, etc.) — same pattern as MobilityDB's per-PG expected files.

These per-version source / test deltas are deliberately out of scope for the foundation PR; they land as the version is brought up.

## Relationship to the CI pipeline

The CI workflow (`.github/workflows/MainDistributionPipeline.yml`) currently builds for a single `duckdb_version: v1.4.4`. The matrix expansion to multiple DuckDB versions — calling the foundation script per row — is a separate PR stacked on this one.

## Adopter install

Once the multi-version matrix is published, the adopter side is unchanged:

```sql
INSTALL mobilityduck FROM community;
LOAD mobilityduck;
```

DuckDB's community-extension repository resolves the right binary for the running DuckDB version. Adopters on v1.4.4 LTS get the v1.4.4 build; adopters on v1.5.x get the v1.5.x build; the same MobilityDuck source produced both.
