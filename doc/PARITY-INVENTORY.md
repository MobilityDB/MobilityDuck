# MobilityDuck parity inventory

The full per-function inventory of MobilityDuck against MobilityDB's
SQL surface. For the high-level "what's covered, what's different",
see [`PARITY.md`](PARITY.md).

Status flags:
- ✓ — shipped, callable today.
- ◯ — open; can be added on demand.
- ✗ — architectural mismatch with DuckDB, or PG-only utility; not
  planned.

## Coverage at a glance

The MobilityDB SQL surface is **487 user-facing names** (after
excluding ~250 PG-extension implementation helpers — see PARITY.md
"PG-internal surface excluded"). Of those:

| | Names | |
|---|---:|---|
| Resolved directly by name | ~310 | ✓ |
| Resolved via DuckDB operator (`=`, `<>`, `+`, `&`, `\|\|`, ...) | ~23 | ✓ |
| Resolved via `*Agg` suffix (RFC #827) | 12 | ✓ |
| Architectural blocks (no `geography`, no MVT) | 2 | ✗ |
| Tracked residual user-visible gaps | ~3 | see sections below |
| **Effective coverage** | ~99% | |

## Architectural blocks (✗)

DuckDB makes a different design choice for these; they're not on a
roadmap and would need new SQL types or specialised renderers in the
host environment.

| Name | Why blocked |
|---|---|
| `geography` (separate SQL type) | DuckDB-spatial has no separate geography SQL type; all geometric storage is `geometry` carrying SRID + a geodetic flag. The geographic *temporal* types `tgeography` / `tgeogpoint` work fine. |
| `asMVTGeom` | Mapbox Vector Tile rendering. Specialised renderer; not currently included. |

## Out-of-scope type families (✗)

These MobilityDB type families are not currently included:

- `tcbuffer` — temporal circular buffers.
- `tnpoint` — temporal network points.
- `tpose` — temporal poses.
- `trgeo` — temporal rigid geometries.
- `th3index` — temporal Uber H3 cells.

## SP-GiST / GiST / GIN access-method support (✗ structural)

~50 MobilityDB names matching `*_spgist_*`, `*_kdtree_*`,
`*_quadtree_*`, `*_gist_*`, `set_gin_*`. These are PG opclass support
functions: they only exist to be registered with `CREATE OPERATOR
CLASS … USING SPGIST` / `USING GIST`. **DuckDB has no SP-GiST or GiST
access method**, so these names have no integration point and aren't
exposed. They're not counted in the coverage figures above.

The user-visible thing they enable on the PG side (a working spatial
index) is provided by `TRTREE` on the DuckDB side; you generally don't
call any of these names directly in user SQL anyway.

The MEOS-side primitives that underlie SP-GiST are being exposed via
[MobilityDB PR #740](https://github.com/MobilityDB/MobilityDB/pull/740).
Once those primitives are public on MEOS, MobilityDuck can register
a custom `TSPGTREE` `BoundIndex` using them — but it can't register
an opclass.

## Shipped surfaces

### `*SeqSetGaps` constructors  ✓

`tboolSeqSetGaps`, `tintSeqSetGaps`, `tfloatSeqSetGaps`,
`ttextSeqSetGaps`, `tgeompointSeqSetGaps`, `tgeogpointSeqSetGaps`,
`tgeometrySeqSetGaps`, `tgeographySeqSetGaps`, plus the previously
missing `tgeometrySeqSet` / `tgeographySeqSet` / `tgeogpointSeqSet`
plain-SeqSet aliases. Test 062.

### Covers predicates  ✓

`eCovers` / `tCovers` / `aCovers` across all 3 signature shapes
(`geometry`/`tspatial`, `tspatial`/`geometry`, `tspatial`/`tspatial`)
for tgeometry / tgeompoint / tgeography / tgeogpoint. Test 063.

### Tile / box list emitters  ✓

| Name | Notes |
|---|---|
| `tboxes(tnumber)` | one bounding tbox per sequence |
| `stboxes(tspatial)` for all 4 spatial types | one bounding stbox per sequence |
| `spaceTiles(stbox, …)` | spatial partitioning |
| `spaceTimeTiles(stbox, …)` | spatial + time partitioning |
| `valueBoxes(tnumber, …)` | per-value-bin bounding tbox |
| `timeBoxes(tnumber, …)` | per-time-bin bounding tbox |
| `valueTimeBoxes(tnumber, …)` | per (value-bin, time-bin) bounding tbox |

Tests 064, 066.

### Split emitters  ✓

| Name | Notes |
|---|---|
| `timeSplit(temp, interval, ts)` | wraps `temporal_time_split`; all 8 temporal types. |
| `valueSplit(tint\|tfloat, size, origin)` | wraps `tnumber_value_split`. |
| `quadSplit(stbox)` | wraps `stbox_quad_split`; 4 quadrants 2D / 8 octants 3D. |
| `valueTimeSplit(tnumber, vsize, dur, vorigin, torigin)` | wraps `tnumber_value_time_split`; emits `LIST<STRUCT(value, time, tnumber)>`. |
| `spaceSplit(tgeompoint\|tgeometry, …)` | wraps `tgeo_space_split`; emits `LIST<STRUCT(part, space)>`. |
| `spaceTimeSplit(tgeompoint\|tgeometry, …)` | wraps `tgeo_space_time_split`; emits `LIST<STRUCT(part, space, time)>`. |

The split-with-bins variants follow the same `LIST<STRUCT(...)>`
shape as `frechetDistancePath`/`dynTimeWarpPath`: use `unnest()` if
you want row-shaped output. Test 065.

### Misc analytics  ✓

| Name | Status | Notes |
|---|---|---|
| `asEWKB(tspatial)` | ✓ | binary form of `asHexEWKB`; wraps `temporal_as_wkb` returning `BLOB`. |
| `tprecision(temp, interval, ts)` | ✓ | sample-and-snap to a coarser time grid; tnumber + tspatial. |
| `tsample(temp, interval, ts [, interp])` | ✓ | regular-interval resampling for any temporal type. |
| `time_distance(...)` | ✓ | distance in seconds between tstzspan / tstzspanset / timestamptz arguments. |
| `trend(tint\|tfloat) → tint` | ✓ | sign of the derivative at each instant. Wraps `tnumber_trend`. Requires linear interpolation. |
| `transformPipeline(temp\|stbox, pipeline, srid, is_forward)` | ✓ | applies a PROJ pipeline string. Wraps `tspatial_transform_pipeline` / `stbox_transform_pipeline`. All 4 spatio-temporal types + stbox. |
| `geoMeasure(tgeompoint, tfloat [, segmentize])` | ✓ | wraps `tpoint_tfloat_to_geomeas`. Builds a geometry whose vertices carry the tfloat measure as the M coordinate. |
| `create_trip(...)` | ✗ | trip-synthesis helper from MobilityDB's `mobilitydb-tools`; out of scope. |

`transform_gk(...)` (Gauss-Krüger projection) is intentionally not
in the parity scope: it's a helper kept in MobilityDB's PG layer to
connect with [Secondo](https://github.com/secondo-database/secondo).
For general-purpose projection in MobilityDuck use
`transformPipeline(...)` (PROJ pipeline strings).

Test 066.

### Z-axis (elevation) restrict  ◯

`atElevation`, `minusElevation` — wraps `tgeo_restrict_elevation`,
which exists in upstream MEOS but is bundled with the
`meosType` → `MeosType` rename ([MobilityDB
PR #790](https://github.com/MobilityDB/MobilityDB/pull/790))
and the simplified spatiotemporal-relationships API
([PR #778](https://github.com/MobilityDB/MobilityDB/pull/778)).
Lifting the vcpkg pin to pick up elevation therefore requires a
coordinated MobilityDuck adoption pass for those two breaking
changes. Tracked together as one task.

### Aggregate residual  ✓ partial

| Name | Status | Notes |
|---|---|---|
| `appendInstantAgg(temp, interp text)` | ✓ | 2-arg variant for all 8 temporal types. |
| `appendInstantAgg(temp, interp text, maxdist float, maxt interval)` | ◯ | needs a custom DuckDB aggregate dispatch — the stock `UnaryAggregate` / `TernaryAggregate` templates don't cover the 4-ary shape cleanly. The underlying MEOS API (`temporal_app_tinst_transfn`) is already public. |
| `setUnion(geography) → geogset` | ✗ | blocked on the `geography` SQL type (architectural). |

## Reporting a gap

Run `mobilityduck_full_version()` from the SQL shell to see the
extension version, the linked MEOS commit, the DuckDB version, and the
full toolchain. If you spot a missing function or unexpected
behaviour, please open an issue with that version line and the
function signature you tried to call.
