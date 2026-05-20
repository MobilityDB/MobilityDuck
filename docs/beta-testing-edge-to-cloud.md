# Beta-Testing Guide: Edge-to-Cloud Temporal Data Lake

This guide has two sections.

- **[Part 1 — For all beta testers](#part-1--for-all-beta-testers)**: what to
  install, what to run, what to check, where to send feedback.
- **[Part 2 — For MobilityDB committers](#part-2--for-mobilitydb-committers)**:
  PR / branch / implementation status, known engineering limitations.

---

## Part 1 — For all beta testers

### What you are testing

The **edge-to-cloud pipeline** for MobilityDB temporal data:

1. Raw GPS pings (CSV or inline values) are loaded into DuckDB.
2. They are assembled into typed `tgeogpointSeq` trajectories — geodetic
   (spheroidal-metre) sequences backed by MEOS.
3. The trajectories are written to a **TemporalParquet** shard: a standard
   Parquet file whose `BYTE_ARRAY` column carries MEOS-WKB values and whose
   file footer contains a `temporal` metadata key describing each column's
   type, encoding, and CRS.
4. The same shard is queryable on DuckDB, MobilityDB (PostgreSQL), and Spark —
   using an identical named-function SQL dialect.

### Time budget

Scenario A (synthetic data, no CSV): **~15 minutes** including the build.
Scenario B (your own GPS CSV): add ~10 minutes.

### Install

```bash
git clone --recurse-submodules --branch feat/edge-to-cloud-quickstart \
    https://github.com/MobilityDB/MobilityDuck.git
cd MobilityDuck
make        # first build: 5–10 min (downloads MEOS + dependencies)
            # subsequent builds: ~30 s
```

After the build, a DuckDB shell with MobilityDuck pre-loaded is at
`./build/release/duckdb`.

> A community extension (one-line `INSTALL`) is coming once this beta
> validates the feature.  For now, build from source is required.

### Scenario A — Zero-data quickstart

Generates 5 synthetic North Sea vessels from inline data — no CSV, no
download.  Demonstrates the full pipeline in under 2 seconds.

```bash
TZ=UTC ./build/release/duckdb -c ".read examples/quickstart/quickstart.sql"
```

**Expected output:**

Query A — geodetic distance and peak speed per vessel:
```
┌───────────┬────────────┬──────────┬──────────────┐
│ entity_id │ ping_count │ length_m │ max_speed_ms │
├───────────┼────────────┼──────────┼──────────────┤
│         5 │         12 │ 172001.0 │        26.22 │
│         1 │         12 │ 170169.0 │        25.93 │
│         2 │         12 │ 158771.0 │        24.21 │
│         3 │         12 │  83644.0 │         12.7 │
│         4 │         12 │  37155.0 │         5.64 │
└───────────┴────────────┴──────────┴──────────────┘
```

Key checkpoints:
- Distances are in **metres**, not degrees (vessel 5 ≈ 172 km — not 1.55°).
- Vessel 3 (Skagerrak) is present here but must **not** appear in Query B.

Query B — vessels that passed through the Copenhagen bounding box:
```
┌───────────┐
│ entity_id │
├───────────┤
│         1 │
│         2 │
│         4 │
│         5 │
└───────────┘
```

Query C — trip duration (all 5 vessels: 12 pings × 10 min = 1 h 50 min):
```
┌───────────┬───────────────┐
│ entity_id │ trip_duration │
├───────────┼───────────────┤
│         1 │ 01:50:00      │  (all five rows identical)
└───────────┴───────────────┘
```

### Scenario B — Same queries on MobilityDB (portability check)

```bash
psql -d <your_db> -f examples/quickstart/quickstart_mobilitydb.sql
```

Queries A, B, and C must produce **identical values** to Scenario A.
This is the portability claim: one named-function SQL file, two platforms.

### Scenario C — Your own GPS data

```bash
# Edit the five CONFIGURE macros at the top of the template:
$EDITOR examples/generic-ingest/generic_ingest.sql
# Set: csv_path, col_entity_id, col_lon, col_lat, col_ts

# Run:
TZ=UTC ./build/release/duckdb -c ".read examples/generic-ingest/generic_ingest.sql"
```

Output: `trajectories.parquet` in the current directory, readable by
MobilityDB, MobilitySpark, and PyMEOS without any MobilityDuck installation.

### Scenario D — Real-world AIS data (optional, ~1 million pings)

Download one day of Danish AIS data from the Maritime Authority:
<https://www.dma.dk/safety-at-sea/navigational-information/ais-data>

Place the downloaded CSV (e.g. `aisdk-2026-02-26.csv`) at any convenient path,
then edit the path at the top of the demo file before running:

```bash
# Set the CSV path in the demo file (one line to edit):
sed -i "s|../../meos/examples/data/aisdk-2026-02-26.csv|/path/to/your.csv|" \
    examples/ais-data-lake/ais_data_lake.sql

TZ=UTC ./build/release/duckdb -c ".read examples/ais-data-lake/ais_data_lake.sql"
```

The demo filters to Class A vessels and a 1-hour window, so it completes in
under 30 seconds on a laptop even with a full-day file.

### How to report feedback

Open an issue or leave a comment on the beta thread:
<https://github.com/MobilityDB/MobilityDB/discussions/913>

Please include:
- Platform + OS version
- Output of `gcc --version` or `clang --version`
- For build failures: the last 20 lines of `make` output
- For wrong results: the full query + actual vs expected output
- Any ergonomic friction (confusing errors, missing functions, surprising behaviour)

---

## Part 2 — For MobilityDB committers

### PR and branch

The feature lands via **MobilityDuck PR #113**:
<https://github.com/MobilityDB/MobilityDuck/pull/113>

Branch: `feat/edge-to-cloud-quickstart` (1 commit on top of `main`).

Related RFC threads:
- [Issue #830](https://github.com/MobilityDB/MobilityDB/issues/830) — TemporalParquet spec
- [PR #911](https://github.com/MobilityDB/MobilityDB/pull/911) — TemporalParquet doc PR
- [PR #917](https://github.com/MobilityDB/MobilityDB/pull/917) — edge-to-cloud SQL portability RFC
- [Discussion #861](https://github.com/MobilityDB/MobilityDB/discussions/861) — portable SQL naming
- [Discussion #913](https://github.com/MobilityDB/MobilityDB/discussions/913) — Temporal Data Lake architecture

### Test suite

```bash
make test    # 1446 assertions across 36 test files, all must pass
```

The new file `test/sql/tgeogpoint.test` (16 assertions) is the regression
guard for the SRID + geodetic-flag fix.

### Implementation status

| Function / type | Status |
|---|---|
| `TGEOGPOINT` construction + string parse | done |
| `TGEOGPOINT` Parquet round-trip (`asBinary` / `tgeogpointFromBinary`) | done |
| `eIntersects(GEOMETRY, tgeogpoint)` and all 12 `(GEOMETRY, temporal)` predicates | done (geodetic fix: `geom_to_geog()`) |
| `temporalFooter(MAP)` → TemporalParquet JSON | done |
| `asBinary`/`fromBinary` for spans, spansets, tgeometry, tcbuffer, tnpoint, tpose, th3index | **not yet wired** |
| Automatic footer injection on `COPY TO '*.parquet'` | **not yet** — call `KV_METADATA {'temporal': temporalFooter(...)}` explicitly |
| `tIntersects(GEOMETRY, tgeogpoint)` | **not yet** — MEOS roundoff error on geodetic sequences |
| `tDwithin(GEOMETRY, tgeogpoint, dist)` | **not yet** — only planar |

### Geodetic fix — what changed

Two bugs in `src/geo/tgeompoint_functions.cpp` (all 12 `(GEOMETRY, temporal)` functions):

**Bug 1** — SRID was hardcoded 0 before the temporal value was deserialized.
`tgeogpoint` has SRID 4326; the geometry had SRID 0 → "Operation on mixed SRID".
Fix: `srid = tspatial_srid(tgeom)` after deserialization.

**Bug 2** — `FLAGS_SET_GEODETIC(gs->gflags, 1)` alone corrupts the 2D bbox layout
(`FLAGS_NDIMS_BOX` changes from 2 → 3, shifting geometry data read offset by 16 bytes).
Fix: `geom_to_geog(gs)` (public MEOS API) properly rebuilds the GSERIALIZED with a
3D bounding box and GEODETIC=1, mirroring PostGIS's implicit `geometry → geography` cast.

### Review checklist (committer)

- [ ] `make test`: 1446 assertions pass
- [ ] `docs/tgeogpoint-design.md` — "Spatial Predicates" section is accurate
- [ ] `examples/quickstart/quickstart.sql` — readable and self-contained for a new user
- [ ] `examples/generic-ingest/generic_ingest.sql` — instructions clear, macros well-named
- [ ] No `Co-Authored-By` or internal planning references in commit messages
- [ ] Confirm `temporalFooter()` output matches the TemporalParquet spec in PR #911
