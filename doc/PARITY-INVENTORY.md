# MobilityDuck — per-function parity reference

This is the per-function detail behind [`PARITY.md`](PARITY.md). If
you're coming from MobilityDB and want to know whether a specific
function is callable today, this is the file to grep.

Status flags:
- ✓ — shipped, callable today.
- ◯ — not yet covered; can be added on demand.
- ✗ — won't be added (architectural mismatch with DuckDB, or a
  PostgreSQL-only utility).

## How much of MobilityDB is here

The MobilityDB SQL surface is **487 user-facing names** (after
excluding ~250 PostgreSQL-extension internals — the `*_in` / `*_out`,
`*_typmod_*`, `*_transfn`, `*_sel`, GiST/SP-GiST/GIN opclass-support
functions, etc.; you don't normally call those by name even on
PostgreSQL).

| | Names | |
|---|---:|---|
| Available under the same name | ~310 | ✓ |
| Available under their named-function form *and* via the equivalent operator (`tbool_and(t1,t2)` ↔ `t1 & t2`, `temporal_teq(t1,t2)` ↔ `t1 = t2`, `tnumber_add` ↔ `+`, `ttext_cat` ↔ `\|\|`, …) — both spellings work on both engines | ~23 | ✓ |
| Reachable only via the named-function form because DuckDB's parser rejects the operator (the `#` time-axis operators `<<#`/`&<#`/`#>>`/`#&>` and the `\|`-bearing Y/Z-axis operators) — call `before`, `overBefore`, `after`, `overAfter`, `above`, `below`, `front`, `back`, etc. | 12 | ✓ via named form |
| Renamed in MobilityDuck with the `*Agg` suffix on aggregate functions | 12 | ✓ |
| Architectural blocks (no separate `geography` SQL type, no MVT) | 2 | ✗ |
| **Effective coverage** | ~100% of the in-scope surface | |

## What's intentionally not here

### `geography` as a separate SQL type  ✗

DuckDB-spatial exposes only `geometry`; geodetic values are stored
as `geometry` with the geodetic flag set. The geographic *temporal*
types `tgeography` and `tgeogpoint` work normally. The only
materially affected MobilityDB signature is
`setUnion(geography) → geogset`; `setUnionAgg(geomset)` and
`setUnionAgg(geogset)` both work.

### `asMVTGeom` (Mapbox Vector Tile rendering)  ✗

Specialised renderer; not currently included.

### `transform_gk` (Gauss-Krüger projection)  ✗

A helper MobilityDB keeps in its PG layer to connect with
[Secondo](https://github.com/secondo-database/secondo). For
general-purpose projection in MobilityDuck use
`transformPipeline(...)` (PROJ pipeline strings).

### `create_trip(...)`  ✗

Trip-synthesis helper that lives in MobilityDB's `mobilitydb-tools`,
not in MEOS itself. Out of scope for MobilityDuck.

### Type families not yet included  ✗

- `tcbuffer` — temporal circular buffers.
- `tnpoint` — temporal network points.
- `tpose` — temporal poses.
- `trgeo` — temporal rigid geometries.
- `th3index` — temporal Uber H3 cells.

### SP-GiST / GiST / GIN opclass support  ✗

About 50 MobilityDB names match `*_spgist_*`, `*_kdtree_*`,
`*_quadtree_*`, `*_gist_*`, `set_gin_*`. They're PostgreSQL opclass
support functions — registered on PostgreSQL with
`CREATE OPERATOR CLASS … USING SPGIST` (or `USING GIST`). DuckDB
has no SP-GiST or GiST access method, so these names don't apply.
You don't normally call them by name on PostgreSQL either; what
they enable (a working spatial index) is provided in MobilityDuck
by `TRTREE`.

The MEOS-side primitives that underlie SP-GiST are being exposed
upstream via [MobilityDB
PR #740](https://github.com/MobilityDB/MobilityDB/pull/740). Once
those land, MobilityDuck could register a custom `TSPGTREE`
`BoundIndex` that uses them — but registering a PostgreSQL opclass
remains structurally impossible.

## What's shipped

### Sequence-set constructors with gaps  ✓

`tboolSeqSetGaps`, `tintSeqSetGaps`, `tfloatSeqSetGaps`,
`ttextSeqSetGaps`, `tgeompointSeqSetGaps`, `tgeogpointSeqSetGaps`,
`tgeometrySeqSetGaps`, `tgeographySeqSetGaps`. The plain
`tgeometrySeqSet` / `tgeographySeqSet` / `tgeogpointSeqSet`
constructors are also shipped.

### Covers predicates  ✓

`eCovers` / `tCovers` / `aCovers` across all 3 input shapes
(`geometry`/`tspatial`, `tspatial`/`geometry`, `tspatial`/`tspatial`)
for `tgeometry` / `tgeompoint` / `tgeography` / `tgeogpoint`.

### Tile and box list emitters  ✓

| Name | Notes |
|---|---|
| `tboxes(tnumber)` | one bounding tbox per sequence |
| `stboxes(tspatial)` | one bounding stbox per sequence (all 4 spatio-temporal types) |
| `spaceTiles(stbox, …)` | spatial partitioning |
| `spaceTimeTiles(stbox, …)` | spatial + time partitioning |
| `valueBoxes(tnumber, …)` | per-value-bin bounding tbox |
| `timeBoxes(tnumber, …)` | per-time-bin bounding tbox |
| `valueTimeBoxes(tnumber, …)` | per (value-bin, time-bin) bounding tbox |

### Split emitters  ✓

| Name | Notes |
|---|---|
| `timeSplit(temp, interval, ts)` | all 8 temporal types |
| `valueSplit(tint\|tfloat, size, origin)` | |
| `quadSplit(stbox)` | 4 quadrants in 2D, 8 octants in 3D |
| `valueTimeSplit(tnumber, vsize, dur, vorigin, torigin)` | emits `LIST<STRUCT(value, time, tnumber)>` |
| `spaceSplit(tgeompoint\|tgeometry, …)` | emits `LIST<STRUCT(part, space)>` |
| `spaceTimeSplit(tgeompoint\|tgeometry, …)` | emits `LIST<STRUCT(part, space, time)>` |

The split-with-bins variants follow the same `LIST<STRUCT(...)>`
shape MobilityDuck uses for `frechetDistancePath` /
`dynTimeWarpPath`. Use `unnest()` to recover row-shaped output.

### Misc analytics  ✓

| Name | Notes |
|---|---|
| `asEWKB(tspatial)` | binary EWKB output (the hex form is `asHexEWKB`) |
| `tprecision(temp, interval, ts)` | snap to a coarser time grid (tnumber + tspatial) |
| `tsample(temp, interval, ts [, interp])` | regular-interval resampling for any temporal type |
| `time_distance(span/spanset/timestamptz pair)` | distance in seconds along the time axis |
| `trend(tint\|tfloat) → tint` | sign of the derivative at each instant; requires linear interpolation |
| `transformPipeline(temp\|stbox, pipeline, srid, is_forward)` | PROJ pipeline projection for all 4 spatio-temporal types and `stbox` |
| `geoMeasure(tgeompoint, tfloat [, segmentize])` | geometry whose vertices carry the `tfloat` measure as the M coordinate |
| `atElevation(tgeompoint, floatspan)` / `minusElevation(...)` | restrict a 3D tgeompoint to / from a floatspan-bounded elevation range |

### Aggregates  ✓

| Name | Notes |
|---|---|
| `appendInstantAgg(temp)` | 1-arg form, all 8 temporal types |
| `appendInstantAgg(temp, interp text)` | 2-arg form, all 8 temporal types |
| `appendInstantAgg(temp, interp text, maxdist float, maxt interval)` | 4-arg form with gap thresholds; all 8 temporal types |
| Everything else from MobilityDB's aggregate surface | with the `Agg` suffix — see PARITY.md |

## Reporting a gap

Run `mobilityduck_full_version()` from the SQL shell to get the
extension version, the linked MEOS commit, the DuckDB version, and
the full toolchain. If you spot a missing function or unexpected
behaviour, please open an issue with that version line and the
function signature you tried to call.
