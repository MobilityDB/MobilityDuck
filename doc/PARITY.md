# MobilityDB ↔ MobilityDuck SQL parity

This document is the answer to *"can MobilityDuck do X for me?"* It
maps the MobilityDB SQL surface onto MobilityDuck and explains what's
covered, what's covered under a different name, and what's deliberately
not covered.

For the full per-function inventory of remaining gaps, see
[`PARITY-INVENTORY.md`](PARITY-INVENTORY.md).

## At a glance

MobilityDuck implements **~98% of MobilityDB's user-facing SQL surface**
across the temporal types (`tint`, `tfloat`, `tbool`, `ttext`),
spatio-temporal types (`tgeompoint`, `tgeogpoint`, `tgeometry`,
`tgeography`), spans / sets / spansets, and the `stbox` / `tbox`
bounding-box types.

If you've used MobilityDB before, you can lift most of your queries
unchanged. The differences fall into three buckets:

  - DuckDB makes a few architectural choices differently (no separate
    `geography` SQL type, no SP-GiST or GiST access methods),
  - some MobilityDB-internal plumbing has no DuckDB counterpart and is
    deliberately not exposed,
  - a handful of named functions are still being wired up — see
    [Tracked gaps](#tracked-gaps).

### Workloads that work today

- **Temporal value access and modifiers** — `at`, `minus`, `atValues`,
  `atTime`, `minusTime`, `atTbox`, `atStbox`, etc.
- **Boxop and posop predicates** — `&&`, `@>`, `<@`, `-|-`, `<<`,
  `>>`, `&<`, `&>`, `<<#`, `#>>`, … plus their named-function forms
  for the operators DuckDB's parser can't accept (e.g. `before`,
  `overBefore`).
- **Distance** — temporal distance (`<->`), nearest-approach
  (`nearestApproachDistance` / `nearestApproachInstant`), and the
  kNN-shaped `|=|` operator. `shortestLine` returns a geometry segment.
- **Trajectory analytics** — `length`, `speed`, `azimuth`,
  `direction`, `cumulativeLength`, `bearing`, `angularDifference`,
  `isSimple`, `makeSimple`.
- **Aggregates** — `tcountAgg`, `tavgAgg`, `tcentroidAgg`,
  `tminAgg` / `tmaxAgg` / `tsumAgg`, the windowed
  `wavgAgg` / `wcountAgg` / `wmin` / `wmax` / `wsumAgg` family,
  `extent`, `mergeAgg`, `appendInstantAgg`, `appendSequenceAgg`,
  `spanUnionAgg`, `setUnionAgg`. (See
  [Naming conventions](#naming-conventions) for why `Agg` is suffixed.)
- **Format I/O** — `asText` / `asEWKT`, MFJSON in/out
  (`asMFJSON`, `tintFromMFJSON`, etc.), Hex(E)WKB
  (`asHexWKB`, `asHexEWKB`), binary EWKB (`asEWKB`).
- **R-tree indexing** — across all 13 bbox-bearing types
  (`stbox`, `tbox`, the five spans, and all eight temporal types),
  with `WHERE col && q` predicates rewriting into index scans.
- **Simplification** — Douglas-Peucker, max-dist, min-dist,
  min-time-delta variants.
- **Bins / tile / split list emitters** — `bins`, `timeBins`,
  `valueBins`, `spaceTiles`, `spaceTimeTiles`, `splitNTboxes`,
  `splitNStboxes`, `timeSplit`, `valueSplit`, `quadSplit`,
  `valueTimeSplit`, `spaceSplit`, `spaceTimeSplit`, `tboxes`,
  `stboxes`, `valueBoxes`, `timeBoxes`, `valueTimeBoxes`.
- **Similarity paths** — Frechet and DTW (with the path-emitter
  variants returning `LIST<STRUCT(i, j)>`; see
  [Composite-row return types](#composite-row-return-types)).
- **Typed STBox / TBox constructors** — `stbox`, `geodstbox`, `tbox`,
  WKB / hex parsing.
- **Gap-aware sequence-set constructors** — `*SeqSet`, `*SeqSetGaps`
  for all 8 temporal types.
- **Resampling and snapping** — `tsample`, `tprecision`.
- **Coordinate transformation** — `transform`, `transformPipeline`
  (PROJ pipeline strings) for tspatial values and `stbox`.
- **Topology** — `eContains`, `tContains`, `eCovers`, `tCovers`,
  `eDisjoint`, `aDisjoint`, `eIntersects`, `aIntersects`, `eTouches`,
  `aTouches`, `eDwithin`, `aDwithin`.
- **Misc** — `geoMeasure(tgeompoint, tfloat)`,
  `time_distance(tstzspan/spanset/ts)`, `trend(tnumber)`.

## How to read this document

Skip to the section that matches your question:

- **"Is *function `X`* available?"** → check [Direct
  equivalents](#direct-equivalents) and [Naming
  conventions](#naming-conventions). Many MobilityDB names are
  reachable via DuckDB operators (`=`, `<>`, `+`, `&`, `||`, ...) or
  under a slightly different name.
- **"Why isn't *type `Y`* there?"** →
  [DuckDB vs MobilityDB type-system](#duckdb-vs-mobilitydb-type-system).
- **"What about indexes?"** →
  [Index and access-method choices](#index-and-access-method-choices).
- **"What's *deliberately* excluded?"** →
  [PG-internal surface excluded](#pg-internal-surface-excluded).
- **"What's still missing that I might want?"** →
  [Tracked gaps](#tracked-gaps).

## Direct equivalents

### Identical names

Most MobilityDB functions are also available in MobilityDuck **under
the exact same name and signature**. If you read a MobilityDB query
that calls (for example) `tbool_and(t1, t2)`, `tnumber_abs(t)`,
`atTime(temp, ts)`, `length(tgeompoint)`, `eContains(geom, tgeo)`,
or `speed(tgeompoint)` — these all work as-is in MobilityDuck.
Assume same-name unless the inventory or a section in this document
tells you otherwise; the "under a different name" cases below are
the exceptions, not the rule.

### Reachable via DuckDB operators

A handful of MobilityDB *named functions* are also spelled as DuckDB
operators. **Both forms exist in both MobilityDB and MobilityDuck** —
calling `tbool_and(t1, t2)` and writing `t1 & t2` are two alternative
spellings of the same operation in either engine. The named-function
form is what shows up if you transliterate MobilityDB SQL verbatim;
the operator form is more idiomatic and is what most queries use.

| MobilityDB / MobilityDuck name | Operator form (both engines) | Notes |
|---|---|---|
| `temporal_teq(t1, t2)` | `t1 = t2` | Lifted equality. |
| `temporal_tne(t1, t2)` | `t1 <> t2` | |
| `temporal_tlt`, `temporal_tle`, `temporal_tgt`, `temporal_tge` | `<`, `<=`, `>`, `>=` | |
| `tgeo_teq`, `tgeo_tne` | `=`, `<>` on `tgeo*` types | |
| `tnumber_add(t1, v)`, `_sub`, `_mult`, `_div` | `+`, `-`, `*`, `/` | Both `(temporal, scalar)` and `(temporal, temporal)` shapes. |
| `tbool_and(t1, t2)` | `t1 & t2` | |
| `tbool_or(t1, t2)` | `t1 \| t2` | |
| `tbool_not(t)` | `~t` | |
| `ttext_cat(t1, t2)` | `t1 \|\| t2` | |
| `*_hash`, `*_hash_extended` | `hash(value)` | DuckDB has its own hash family for the BLOB-backed types. |

## Naming conventions

Two MobilityDuck-specific naming conventions exist, both for principled
reasons:

### `*Agg` suffix on aggregate functions (RFC #827)

DuckDB function name resolution is case-insensitive, which means
`Tmin(tbox) → tbox` (MobilityDB scalar) and `tmin(tfloat) → tfloat`
(MobilityDB aggregate) collapse to the same identifier and conflict at
extension load time. Resolved by giving MobilityDuck's aggregates the
`*Agg` suffix:

| MobilityDB aggregate | MobilityDuck name |
|---|---|
| `tand(tbool)` | `tandAgg(tbool)` |
| `tor(tbool)` | `torAgg(tbool)` |
| `tmin(tfloat)`, `tmin(tint)`, `tmin(ttext)` | `tminAgg(...)` |
| `tmax(...)`, `tsum(...)`, `tavg(...)`, `tcount(...)` | `*Agg(...)` |
| `tcentroid(tgeompoint)`, `tcentroid(tgeogpoint)` | `tcentroidAgg(...)` |
| `wavg`, `wcount`, `wmin`, `wmax`, `wsum` | `*Agg` |
| `merge(...)`, `appendInstant(...)`, `appendSequence(...)` | `*Agg(...)` |
| `setUnion(...)`, `spanUnion(...)` | `setUnionAgg(...)`, `spanUnionAgg(...)` |
| `extent(...)` | `extent(...)` *(unchanged — no scalar collision)* |

Internally the aggregate's transition / final / combine functions
delegate to the same MEOS C calls MobilityDB uses, so semantics match
exactly.

### Version-stamp functions (`mobilitydb_*` ↔ `mobilityduck_*`)

The `mobilitydb_version()` and `mobilitydb_full_version()` MobilityDB
functions have **direct MobilityDuck equivalents** under a renamed
prefix:

| MobilityDB | MobilityDuck equivalent |
|---|---|
| `mobilitydb_version()` | `mobilityduck_version()` |
| `mobilitydb_full_version()` | `mobilityduck_full_version()` |

For cross-engine query portability the original MobilityDB names
(`mobilitydb_version()` / `mobilitydb_full_version()`) are also
registered as aliases — they return the same MobilityDuck-stamped
output, so the same query text works against either engine without
lying about which one is talking. Use whichever spelling reads better
in your code.

## DuckDB vs MobilityDB type-system

### `geography` is not a separate SQL type

MobilityDB defines `geography` (and `geog_*` predicates) as a
PostGIS-derived type distinct from `geometry`. MobilityDuck doesn't:

- The DuckDB-spatial extension MobilityDuck composes with exposes only
  `geometry`. SRID and the geodetic flag travel with the value, but the
  type-system distinction (`geometry(Point, 4326)` vs
  `geography(Point, 4326)`) doesn't surface as separate SQL types.
- Geographic *temporal* values (`tgeography`, `tgeogpoint`) work fine —
  they carry the geodetic flag internally and dispatch to the
  appropriate MEOS routines.
- Operations that would require a planar-vs-geodetic distinction at the
  type-system level surface MEOS' runtime check
  ("Operation on mixed planar and geodetic coordinates" /
  "Only planar coordinates supported") rather than a planning-time
  binder error. Same constraint MobilityDB enforces; the surface where
  the check fires is just shifted.

The single MobilityDB aggregate signature this affects is
`setUnion(geography) → geogset`. The corresponding
`setUnionAgg(geomset)` and `setUnionAgg(geogset)` set-input variants
both work; only the geometry-scalar-to-geogset path is unreachable.

### Composite-row return types

MobilityDB's similarity-path family (`frechetDistancePath`,
`dynTimeWarpPath`) returns `SETOF warp` where `warp` is the
`CREATE TYPE warp AS (i integer, j integer)` row shape. DuckDB scalar
functions can't return a SET; MobilityDuck wraps the result in
`LIST<STRUCT(i INTEGER, j INTEGER)>`. You recover row semantics with
`unnest()`:

```sql
SELECT u.i, u.j FROM (
  SELECT unnest(frechetDistancePath(t1, t2)) AS u
) ORDER BY i, j;
```

## Index and access-method choices

DuckDB ships only two index access methods in core: ART (radix /
B-tree-equivalent) and HNSW (vector index). The DuckDB-spatial
extension adds R-tree.

MobilityDuck builds on this:

- **`TRTREE`** — MobilityDuck's R-tree-style `BoundIndex` over MEOS'
  in-memory `RTree`. Supports every bbox-bearing column type:
  `stbox`, `tbox`, all five spans (`intspan`, `bigintspan`,
  `floatspan`, `datespan`, `tstzspan`), and all eight temporal types.
  Wired through DuckDB's optimizer-extension hook so
  `WHERE col && q` predicates rewrite into index scans.
- **kNN scan via `|=|`** — `ORDER BY col |=| q LIMIT k` works
  syntactically, but the underlying MEOS `RTree` doesn't currently
  expose a priority-queue scan, so the planner falls back to seq-scan
  + sort + limit. The `|=|` operator surface and all the
  `nearestApproachDistance` / `nearestApproachInstant` / `shortestLine`
  forms are wired; only the index-driven fast path waits on a MEOS
  follow-up.

### SP-GiST and GiST opclass support — structural mismatch

MobilityDB exposes ~50 named functions like `tnumber_quadtree_choose`,
`stbox_kdtree_inner_consistent`, `span_gist_picksplit`,
`tgeo_gist_compress`, `*_spgist_get_*`, etc. These are PG opclass
support functions: they exist only to be registered with
`CREATE OPERATOR CLASS … USING SPGIST` / `USING GIST`. **DuckDB has
no SP-GiST or GiST access method**, so these names have no integration
point and aren't exposed. The same applies to GIN extract callbacks
(`set_gin_extract_value` and friends).

The end-user effect is none of these functions appear, but you
generally don't call them directly anyway — they exist to feed PG's
index machinery. The user-visible thing they enable (a working spatial
index) is provided by `TRTREE`.

## PG-internal surface excluded

Several MobilityDB-named functions are part of the PG-extension
implementation surface and have no DuckDB counterpart. They're
deliberately not registered:

| Surface | Examples | Why excluded |
|---|---|---|
| Type I/O (text / binary) | `*_in`, `*_out`, `*_recv`, `*_send` | PG-internal `pg_type` callback shape; DuckDB uses cast registration. |
| Type-modifier dispatch | `*_typmod_in`, `*_typmod_out`, `*_typmod` | PG type-modifier machinery; DuckDB has no equivalent. |
| Statistics target | `*_analyze`, `*_canonicalize` | PG `analyze` hook + range canonicalisation. |
| Aggregate state | `*_transfn`, `*_finalfn`, `*_combinefn`, `*_serialfn`, `*_deserialfn` | PG `CREATE AGGREGATE` `sfunc/ffunc`. MobilityDuck wires aggregates directly via DuckDB's `AggregateFunction` so you only see the aggregate name, not its sfunc. |
| Selectivity / planner cost | `*_sel`, `*_joinsel`, `*_supportfn` | PG planner cost hooks; MobilityDuck pushes index decisions through DuckDB's optimizer-extension. |
| GIN index callbacks | `set_gin_extract_value`, `set_gin_extract_query`, `set_gin_triconsistent` | DuckDB has no GIN. |
| GiST / SP-GiST opclass support | `*_gist_*`, `*_spgist_*`, `*_quadtree_*`, `*_kdtree_*` | DuckDB has no GIST or SP-GiST. |
| `fill_oid_cache` | `fill_oid_cache()` | PG OID-bootstrap helper. |

These exclusions account for ~250 MobilityDB names. None of them
represent capability MobilityDuck *can't* offer; they're simply the
plumbing layer of a different host database.

## Tracked gaps

The remaining residual gaps live in
[`PARITY-INVENTORY.md`](PARITY-INVENTORY.md), with status flags:

- ✓ — shipped.
- ◯ — open, can be added on demand.
- ✗ — architectural mismatch with DuckDB, or PG-only utility; not planned.

Run `mobilityduck_full_version()` from the SQL shell to see the
extension version, the linked MEOS commit, the DuckDB version, and the
full toolchain. That output is the single source of truth for *which*
MobilityDuck you're talking to.

## Out-of-scope type families

These MobilityDB type families are not currently included:

- **`tcbuffer`** — temporal circular buffers.
- **`tnpoint`** — temporal network points (graph-based).
- **`tpose`** — temporal poses.
- **`trgeo`** — temporal rigid geometries.
- **`th3index`** — temporal Uber H3 cells.

---

*If you spot a gap that's not mentioned here, please open an issue
with the function signature you tried to call.*
