# MobilityDB ↔ MobilityDuck SQL parity

This document is the answer to *"can MobilityDuck do X for me?"* It maps
the MobilityDB SQL surface onto MobilityDuck and explains what's
covered, what's covered under a different name, and what's deliberately
not covered.

For the contributor-facing list of remaining gaps and how they map onto
shippable PRs, see [`PARITY-INVENTORY.md`](PARITY-INVENTORY.md).

## At a glance

MobilityDuck implements **~95% of MobilityDB's user-facing SQL surface**
across temporal types (`tint`, `tfloat`, `tbool`, `ttext`),
spatial-temporal types (`tgeompoint`, `tgeogpoint`, `tgeometry`,
`tgeography`), spans / sets / spansets, and the `stbox` / `tbox`
bounding-box types. The remaining 8% is split between

  - architectural choices DuckDB makes differently (no `geography` type,
    no SP-GiST or GiST access methods),
  - PG-extension implementation surface that has no DuckDB counterpart
    (selectivity helpers, GIN extract callbacks, aggregate transition
    functions, etc.),
  - a tracked tail of named functions that need future MEOS-side or
    DuckDB-side work — see `PARITY-INVENTORY.md`.

Workloads that work today: temporal value access and modifiers
(at/minus/atValues/...), boxop and posop predicates, distance
(temporal and scalar nearest-approach, plus the kNN-shaped `|=|`
operator), trajectory analytics (`length`, `speed`, `azimuth`,
`direction`, `cumulativeLength`, `bearing`), aggregates (`tcountAgg`,
`tavgAgg`, `tcentroidAgg`, `extent`, `mergeAgg`, `appendInstantAgg`,
the windowed `wavgAgg`/`wcountAgg`/`wmin/max/sumAgg` family,
`spanUnionAgg`, `setUnionAgg`), MFJSON / Hex(E)WKB / EWKT I/O including
type-specific FromMFJSON parsers, R-tree indexing across all 13
bbox-bearing types, simplification (Douglas-Peucker / max-dist /
min-dist / min-time-delta), bins / tile / split list emitters
(`bins`, `timeBins`, `valueBins`, `spaceTiles`, `spaceTimeTiles`,
`splitNTboxes`, `splitNStboxes`), similarity paths (Frechet / DTW),
typed STBox / TBox constructors, gap-aware sequence-set constructors
(`*SeqSetGaps`).

## How to read this document

Skip to the section that matches your question:

- **"Is *function `X`* available?"** → check
  [Direct equivalents](#direct-equivalents) and [Naming
  conventions](#naming-conventions). Many MobilityDB names are
  reachable via DuckDB operators (`=`, `<>`, `+`, `&`, `||`, ...) or
  under a slightly different name.
- **"Why isn't *type `Y`* there?"** → see
  [Type-system differences](#duckdb-vs-mobilitydb-type-system).
- **"What about indexes?"** → [Index and access-method
  choices](#index-and-access-method-choices).
- **"What's *deliberately* excluded?"** →
  [PG-internal surface excluded](#pg-internal-surface-excluded).
- **"What's still missing that I might want?"** →
  [Tracked gaps](#tracked-gaps).

## Direct equivalents

The following named MobilityDB functions are reachable in MobilityDuck
through a DuckDB operator. The DuckDB form is more idiomatic; the named
function is what you'd see if you were transliterating MobilityDB SQL.

| MobilityDB | MobilityDuck | Notes |
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
| `*_supportfn` | _(no DuckDB peer)_ | PG planner-support callbacks; MobilityDuck does its index pushdown via the `TRTreeIndexScanOptimizer`. |

## Naming conventions

Two MobilityDuck-specific naming conventions exist, both for principled
reasons:

### `*Agg` suffix on aggregate functions (RFC #827)

DuckDB function name resolution is case-insensitive, which means
`Tmin(tbox) → tbox` (MobilityDB scalar) and `tmin(tfloat) → tfloat`
(MobilityDB aggregate) collapse to the same identifier and conflict at
extension load time with a `GetAlterInfo not implemented` error.
Resolved by giving MobilityDuck's aggregates the `*Agg` suffix:

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
delegate to the same MEOS C calls MobilityDB uses
(`temporal_tcount_transfn`, `temporal_wagg_transform_transfn`,
`tnumber_extent_transfn`, etc.), so semantics match exactly.

### `mobilitydb_*` aliases for the version-stamp functions

`mobilitydb_version()` and `mobilitydb_full_version()` are registered
as aliases for `mobilityduck_version()` /
`mobilityduck_full_version()`, returning the same MobilityDuck-stamped
output. Cross-engine query portability without lying about which
engine is talking.

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
`setUnion(geography) → geogset`. The corresponding `setUnionAgg(geomset) → geomset`
and `setUnionAgg(geogset) → geogset` set-input variants both work; only
the geometry-scalar-to-geogset path is unreachable.

### Composite-row return types

MobilityDB's similarity-path family (`frechetDistancePath`,
`dynTimeWarpPath`) returns `SETOF warp` where `warp` is the
`CREATE TYPE warp AS (i integer, j integer)` row shape. DuckDB scalar
functions can't return a SET; MobilityDuck wraps the result in
`LIST<STRUCT(i INTEGER, j INTEGER)>`. The user recovers row semantics
with `unnest()`:

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
  Wired through DuckDB's optimizer-extension hook so `WHERE col && q`
  predicates rewrite into index scans.
- **kNN scan via `|=|`** — `ORDER BY col |=| q LIMIT k` works
  syntactically, but the underlying MEOS `RTree` doesn't currently
  expose a priority-queue scan, so the planner falls back to seq-scan
  + sort + limit. The `|=|` operator surface and all the
  `nearestApproachDistance` / `nearestApproachInstant` / `shortestLine`
  forms are wired; only the index-driven fast path waits on a MEOS
  follow-up.

### Out-of-scope: SP-GiST and GiST opclass support

MobilityDB exposes ~50 named functions like `tnumber_quadtree_choose`,
`stbox_kdtree_inner_consistent`, `span_gist_picksplit`,
`tgeo_gist_compress`, `*_spgist_get_*`, etc. These are PG opclass
support functions: they exist only to be registered with
`CREATE OPERATOR CLASS … USING SPGIST` / `USING GIST`. **DuckDB has
no SP-GiST or GiST access method**, so these names have no integration
point.

The MEOS-side hoist of the SP-GiST primitives ([MobilityDB
PR #740](https://github.com/MobilityDB/MobilityDB/pull/740)) makes the
underlying primitives (`getQuadrant8D`, `stboxnode_init`,
`distance_stbox_nodebox`, etc.) callable from libmeos. Once a
follow-up MEOS PR publicizes those headers via `meos_index.h` (drafted
on `meos_spgist-publish` on the fork), MobilityDuck can build a
custom `TSPGTREE` `BoundIndex` using them — but registering an
opclass remains structurally impossible.

The GiST `*_consistent` / `*_compress` / `*_picksplit` / `*_distance`
/ `*_penalty` / `*_same` / `*_union` / `*_fetch` family is in the
same bucket: PG-opclass-only.

## PG-internal surface excluded

Several MobilityDB-named functions are part of the PG-extension
implementation surface and have no DuckDB counterpart. They're
deliberately not registered:

| Surface | Examples | Why excluded |
|---|---|---|
| Type I/O (text / binary) | `*_in`, `*_out`, `*_recv`, `*_send` | PG-internal `pg_type` callback shape; DuckDB uses `LogicalType::CAST` registration. |
| Type-modifier dispatch | `*_typmod_in`, `*_typmod_out`, `*_typmod` | PG type-modifier machinery; DuckDB has no equivalent. |
| Statistics target | `*_analyze`, `*_canonicalize` | PG `analyze` hook + range canonicalisation. |
| Aggregate state | `*_transfn`, `*_finalfn`, `*_combinefn`, `*_serialfn`, `*_deserialfn` | PG aggregate `sfunc/ffunc` etc.; visible to PG's `CREATE AGGREGATE` only. MobilityDuck wires the aggregates directly via DuckDB's `AggregateFunction::*Aggregate*` templates so users only see the aggregate name, not its sfunc. |
| Selectivity / planner cost | `*_sel`, `*_joinsel`, `*_supportfn` | PG planner cost hooks; MobilityDuck pushes index decisions through DuckDB's optimizer-extension. |
| GIN index callbacks | `set_gin_extract_value`, `set_gin_extract_query`, `set_gin_triconsistent` | DuckDB has no GIN. |
| GiST / SP-GiST opclass support | `*_gist_*`, `*_spgist_*`, `*_quadtree_*`, `*_kdtree_*` | DuckDB has no GIST or SP-GiST. See [Out-of-scope: SP-GiST and GiST](#out-of-scope-sp-gist-and-gist-opclass-support). |
| `fill_oid_cache` | `fill_oid_cache()` | PG OID-bootstrap helper. |

These exclusions account for ~250 MobilityDB names. None of them
represent capability MobilityDuck *can't* offer; they're simply the
plumbing layer of a different host database.

## PG SQL row-shape constructors

PostgreSQL ships several geometric types that MobilityDB redefines for
symmetry with PostGIS:

| MobilityDB | MobilityDuck equivalent |
|---|---|
| `point(x, y)` | `ST_Point(x, y)` (DuckDB-spatial) |
| `box(p1, p2)` | `ST_MakeBox2D(p1, p2)` (DuckDB-spatial) |
| `line`, `lseg`, `path`, `polygon`, `circle` | `ST_GeomFromText('LINESTRING ...' / 'POLYGON ...')` (DuckDB-spatial) |

The capabilities are equivalent; the namespace differs.

## Tracked gaps

The remaining residual gaps live in
[`PARITY-INVENTORY.md`](PARITY-INVENTORY.md), with status flags:

- ✓ — shipped.
- ◯ — open, MEOS APIs available.
- ⊘ — blocked on MEOS-side API exposure.
- ✗ — architectural mismatch; not planned.

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
