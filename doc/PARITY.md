# Coming from MobilityDB to MobilityDuck

If you've used MobilityDB on PostgreSQL and are trying out MobilityDuck
on DuckDB, this document is for you. It tells you what works the same,
what works under a different name, and what's intentionally missing.

For the per-function reference of MobilityDuck's parity surface,
see [`PARITY-INVENTORY.md`](PARITY-INVENTORY.md).

## TL;DR

MobilityDuck implements **~100% of MobilityDB's in-scope user-facing SQL surface**
across the temporal types (`tint`, `tfloat`, `tbool`, `ttext`),
spatio-temporal types (`tgeompoint`, `tgeogpoint`, `tgeometry`,
`tgeography`), spans / sets / spansets, and the `stbox` / `tbox`
bounding-box types.

**Most MobilityDB queries run unchanged.** The differences you'll
actually run into:

  - DuckDB has no separate `geography` SQL type. Geographic *temporal*
    values (`tgeography`, `tgeogpoint`) work the same way; geographic
    *static* values are stored as `geometry` with the geodetic flag.
  - Aggregate functions are spelled with an `Agg` suffix
    (`tcountAgg`, `tavgAgg`, `tcentroidAgg`, …) to avoid name collisions
    with same-named scalar functions in DuckDB. The semantics are the
    same.
  - Indexes are R-tree (`TRTREE`) rather than SP-GiST / GiST. The
    `WHERE col && q` predicate still gets pushed into an index scan;
    the index name and definition syntax are different.

## What I can paste in unchanged

Function names you've used in MobilityDB — `tnumber_abs(t)`,
`atTime(temp, ts)`, `length(tgeompoint)`, `eContains(geom, tgeo)`,
`speed(tgeompoint)`, `tbool_and(t1, t2)`, the operators `&&`, `<->`,
`|=|`, `@>`, `<@`, the named `before` / `overBefore` / … position
predicates — all work as-is in MobilityDuck.

A few feature areas where MobilityDuck is on parity:

- **Value access and modifiers**: `at`, `minus`, `atValues`,
  `atTime`, `minusTime`, `atTbox`, `atStbox`.
- **Boxop and posop predicates**: `&&`, `@>`, `<@`, `-|-`, `<<`,
  `>>`, `&<`, `&>`, plus the time-axis `before` / `overBefore` /
  `after` / `overAfter` named-function forms (DuckDB's parser
  doesn't accept `<<#`-style operator names).
- **Distance**: temporal distance (`<->`), nearest-approach
  (`nearestApproachDistance`, `nearestApproachInstant`),
  `shortestLine`, and the kNN-shaped `|=|` operator.
- **Trajectory analytics**: `length`, `speed`, `azimuth`,
  `direction`, `cumulativeLength`, `bearing`, `angularDifference`,
  `isSimple`, `makeSimple`.
- **Format I/O**: `asText`, `asEWKT`, MFJSON in/out
  (`asMFJSON`, `tintFromMFJSON`, …), `asHexWKB`, `asHexEWKB`,
  binary `asEWKB`.
- **Topological / spatial relationships**: `eContains` / `tContains`,
  `eCovers` / `tCovers` / `aCovers`, `eDisjoint` / `aDisjoint`,
  `eIntersects` / `aIntersects`, `eTouches` / `aTouches`,
  `eDwithin` / `aDwithin`.
- **Simplification**: Douglas-Peucker, max-dist, min-dist,
  min-time-delta variants.
- **Bins / tiles / splits**: `bins`, `timeBins`, `valueBins`,
  `spaceTiles`, `spaceTimeTiles`, `splitNTboxes`, `splitNStboxes`,
  `timeSplit`, `valueSplit`, `quadSplit`, `valueTimeSplit`,
  `spaceSplit`, `spaceTimeSplit`, `tboxes`, `stboxes`, `valueBoxes`,
  `timeBoxes`, `valueTimeBoxes`.
- **Similarity**: Frechet and DTW distances + their path-emitter
  variants.
- **Constructors**: typed `stbox`, `geodstbox`, `tbox`,
  WKB / hex-WKB parsers; `*SeqSet` / `*SeqSetGaps` for all 8
  temporal types.
- **Resampling**: `tsample`, `tprecision`.
- **Coordinate transformations**: `transform(temp, srid)`,
  `transformPipeline(temp|stbox, pipeline, srid, is_forward)`
  (PROJ pipeline strings) for all 4 spatio-temporal types and
  `stbox`.
- **Aggregates**: `tcountAgg`, `tavgAgg`, `tcentroidAgg`,
  `tminAgg` / `tmaxAgg` / `tsumAgg`, the windowed
  `wavgAgg` / `wcountAgg` / `wmin` / `wmax` / `wsumAgg`,
  `extent`, `mergeAgg`, `appendInstantAgg`, `appendSequenceAgg`,
  `spanUnionAgg`, `setUnionAgg`. (See
  [Aggregate naming](#aggregate-naming) for why `Agg` is suffixed.)
- **Misc**: `geoMeasure(tgeompoint, tfloat)`,
  `time_distance(tstzspan/spanset/timestamptz pair)`,
  `trend(tnumber)`.
- **R-tree indexing** across all 13 bbox-bearing types
  (`stbox`, `tbox`, the five spans, and all eight temporal types).

## Operator forms

A handful of MobilityDB named functions are also spelled as
operators. **Both spellings — the named function and its operator
form — exist in both engines** and refer to the same operation.
This isn't a MobilityDuck-vs-MobilityDB difference; it's just
useful to have the table when you're reading or writing queries.

| Named function (both engines) | Operator (both engines) | Notes |
|---|---|---|
| `temporal_teq(t1, t2)` | `t1 = t2` | Lifted equality. |
| `temporal_tne(t1, t2)` | `t1 <> t2` | |
| `temporal_tlt(t1, t2)`, `temporal_tle`, `temporal_tgt`, `temporal_tge` | `t1 < t2`, `<=`, `>`, `>=` | |
| `tgeo_teq(t1, t2)`, `tgeo_tne(t1, t2)` | `t1 = t2`, `t1 <> t2` on `tgeo*` types | |
| `tnumber_add(t1, v)`, `tnumber_sub`, `tnumber_mult`, `tnumber_div` | `t1 + v`, `t1 - v`, `t1 * v`, `t1 / v` | Both `(temporal, scalar)` and `(temporal, temporal)` shapes. |
| `tbool_and(t1, t2)` | `t1 & t2` | |
| `tbool_or(t1, t2)` | `t1 \| t2` | |
| `tbool_not(t)` | `~t` | |
| `ttext_cat(t1, t2)` | `t1 \|\| t2` | |
| `*_hash(value)`, `*_hash_extended(value, seed)` | `hash(value)` (DuckDB) | DuckDB's stock `hash` family covers the BLOB-backed types. |

The operator form is what most queries use; the named-function
form is what shows up if you transliterate MobilityDB SQL
verbatim. Either reads on either engine.

### Operators that DuckDB's parser rejects

DuckDB's lexer doesn't allow `#` or `|` adjacent to angle brackets in
operator names, so the MobilityDB position operators along the time
axis (anything with `#`) and along the Y-axis / Z-axis (anything
with `|`/`/`) can't be registered as operators in MobilityDuck. The
underlying *named* functions are wired up on both engines, so you
can keep the same query as long as you write the named form.

| Axis | MobilityDB operator | Named function (works on both engines) | Meaning |
|---|---|---|---|
| Time | `<<#` | `before(a, b)` | strictly before |
| Time | `&<#` | `overBefore(a, b)` | not after (overlap-before) |
| Time | `#>>` | `after(a, b)` | strictly after |
| Time | `#&>` | `overAfter(a, b)` | not before (overlap-after) |
| Y-axis (vertical) | `<<\|` | `below(a, b)` | strictly below |
| Y-axis | `&<\|` | `overBelow(a, b)` | not above |
| Y-axis | `\|>>` | `above(a, b)` | strictly above |
| Y-axis | `\|&>` | `overAbove(a, b)` | not below |
| Z-axis (depth, 3D) | `<</` | `front(a, b)` | strictly in front |
| Z-axis | `&</` | `overFront(a, b)` | not behind |
| Z-axis | `/>>` | `back(a, b)` | strictly behind |
| Z-axis | `/&>` | `overBack(a, b)` | not in front |

The X-axis operators (`<<`, `>>`, `&<`, `&>` — strictly-left,
strictly-right, overlap-left, overlap-right) **are** valid in
DuckDB and work as operators in both engines, alongside the named
forms `temporal_left`, `temporal_right`, etc.

## Things that are spelled differently in MobilityDuck

These are the actual MobilityDB → MobilityDuck *renames*. Calling a
MobilityDB query verbatim that uses one of these names won't work
on MobilityDuck; you need the right-hand spelling.

### Aggregate naming

MobilityDB's aggregate functions get an `Agg` suffix in MobilityDuck:

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

The reason: DuckDB function name resolution is case-insensitive, so
the MobilityDB scalar `Tmin(tbox) → tbox` and the MobilityDB aggregate
`tmin(tfloat) → tfloat` would collide on the same name in DuckDB. The
`Agg` suffix breaks the tie. The semantics are identical — internally
the aggregate calls the same MEOS C functions MobilityDB uses.

### Version stamps

| MobilityDB | MobilityDuck |
|---|---|
| `mobilitydb_version()` | `mobilityduck_version()` |
| `mobilitydb_full_version()` | `mobilityduck_full_version()` |

The original `mobilitydb_*` names are also registered as aliases that
return MobilityDuck's version stamp, so the same query text works on
either engine if you need cross-engine portability.

## Things that are different

### `geography` is not a separate SQL type

MobilityDB defines `geography` as a PostGIS-derived type distinct
from `geometry`. DuckDB-spatial (which MobilityDuck composes with)
exposes only `geometry`; the geodetic flag and SRID travel with the
value. In practice:

- Geographic *temporal* values (`tgeography`, `tgeogpoint`) work the
  same as in MobilityDB.
- Geographic *static* values use `geometry` with the geodetic flag
  set, not a separate `geography` type.
- If you mix planar and geodetic coordinates in one expression
  MobilityDuck raises the same MEOS error MobilityDB raises
  (*"Operation on mixed planar and geodetic coordinates"*); the
  difference is just that the error fires at runtime instead of at
  the type-system level.

The only aggregate signature this materially affects is
`setUnion(geography) → geogset` (the geometry-scalar input variant);
`setUnionAgg(geomset)` and `setUnionAgg(geogset)` both work.

### Composite-row return types

A few MobilityDB functions return `SETOF row_type` —
`frechetDistancePath` and `dynTimeWarpPath` return `SETOF warp` for
example. DuckDB scalar functions can't return a SET, so MobilityDuck
returns these as `LIST<STRUCT(...)>`. The split-with-bins emitters
(`valueTimeSplit`, `spaceSplit`, `spaceTimeSplit`) follow the same
pattern.

You recover row semantics with `unnest()`:

```sql
SELECT u.i, u.j FROM (
  SELECT unnest(frechetDistancePath(t1, t2)) AS u
) ORDER BY i, j;
```

### Indexes

DuckDB doesn't have SP-GiST, GiST, or GIN. MobilityDuck provides
`TRTREE`, an R-tree-style index that covers the same use cases:

```sql
CREATE INDEX ON trips USING TRTREE (path);
SELECT * FROM trips WHERE path && stbox 'STBOX X((0,0),(10,10))';
```

`TRTREE` works on all 13 bbox-bearing column types (`stbox`, `tbox`,
the five spans, and all eight temporal types); the `WHERE col && q`
predicate is rewritten into an index scan automatically.

The kNN operator `|=|` works syntactically (`ORDER BY col |=| q LIMIT k`
gives correct results), but currently falls back to seq-scan + sort +
limit because MEOS' R-tree doesn't yet expose a priority-queue scan.
Output is correct; throughput on large tables isn't.

The MobilityDB SP-GiST / GiST opclass support functions (`*_spgist_*`,
`*_kdtree_*`, `*_quadtree_*`, `*_gist_*`) and the GIN extract
callbacks (`set_gin_extract_*`) aren't exposed in MobilityDuck —
they're PostgreSQL planner-machinery glue with no DuckDB counterpart.
You wouldn't normally call them by name anyway.

## Type families that aren't included

These MobilityDB type families aren't part of MobilityDuck today:

- **`tcbuffer`** — temporal circular buffers.
- **`tnpoint`** — temporal network points.
- **`tpose`** — temporal poses.
- **`trgeo`** — temporal rigid geometries.
- **`th3index`** — temporal Uber H3 cells.

---

*Spotted something that should work but doesn't? Please open an
issue with the function signature you tried to call and the version
line from `mobilityduck_full_version()`.*
