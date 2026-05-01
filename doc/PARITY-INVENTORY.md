# MobilityDuck parity inventory

Contributor-facing tracker of remaining MobilityDB ↔ MobilityDuck
parity gaps. For the user-facing "what's covered and how" map, see
[`PARITY.md`](PARITY.md).

Symbols:
- ✓ shipped
- ◯ open, MEOS APIs available, ready-to-pick-up
- ⊘ blocked on a MEOS-side API exposure
- ✗ architectural; not planned

## Counted vs. excluded names

The MobilityDB SQL surface is **487 user-facing names** (after
excluding ~250 PG-extension implementation helpers — see PARITY.md
"PG-internal surface excluded"). Of those:

| | Names | |
|---|---:|---|
| Resolved directly by name | ~310 | ✓ |
| Resolved via DuckDB operator (`=`, `<>`, `+`, `&`, `\|\|`, ...) | ~23 | ✓ |
| Resolved via `*Agg` suffix (RFC #827) | 12 | ✓ |
| Resolved via DuckDB-spatial native (`point`, `line`, `box`, ...) | 7 | ✓ |
| Architectural blocks (no `geography`, no MVT) | 2 | ✗ |
| Tracked residual user-visible gaps | ~12 | mix of ◯ and ⊘ — see sections below |
| **Effective coverage** | ~95% | |

## Architectural blocks (✗)

Not on a roadmap; would need new SQL types or specialised renderers in
the host environment.

| Name | Why blocked |
|---|---|
| `geography` | DuckDB-spatial has no separate geography SQL type; all geometric storage is `geometry` carrying SRID + geodetic flag. |
| `asMVTGeom` | Mapbox Vector Tile rendering. Specialised renderer; not currently included. |

## Out-of-scope type families (✗)

Not currently included:

- `tcbuffer` family
- `tnpoint` family
- `tpose` family
- `trgeo` family
- `th3index` family

## SP-GiST / GiST / GIN access-method support (✗ structural)

~50 MobilityDB names matching `*_spgist_*`, `*_kdtree_*`,
`*_quadtree_*`, `*_gist_*`, `set_gin_*`. DuckDB has none of those AMs.
Not counted in the coverage figures above.

The MEOS-side primitives that underlie SP-GiST are being exposed via
[MobilityDB PR #740](https://github.com/MobilityDB/MobilityDB/pull/740).
Once those primitives are public on MEOS, MobilityDuck can register
a custom `TSPGTREE` `BoundIndex` using them — but it can't register
an opclass.

## Residual gaps

### `*SeqSetGaps` constructors  ✓

`tboolSeqSetGaps`, `tintSeqSetGaps`, `tfloatSeqSetGaps`,
`ttextSeqSetGaps`, `tgeompointSeqSetGaps`, `tgeogpointSeqSetGaps`,
`tgeometrySeqSetGaps`, `tgeographySeqSetGaps`, plus the previously
missing `tgeometrySeqSet` / `tgeographySeqSet` / `tgeogpointSeqSet`
plain-SeqSet aliases. Test 062.

### Covers predicates  ✓

`eCovers(geometry|tgeo, …)`, `tCovers(…)` for tgeometry / tgeompoint
/ tgeography / tgeogpoint. Test 063.

`aCovers` ⊘ — MEOS public surface exposes only `ecovers_*` and
`tcovers_*`; the always-covers path goes through MobilityDB-internal
`ea_covers_*` (with `ALWAYS` flag) which isn't on `meos_geo.h` yet.
A one-line MEOS publicize change would unblock this.

### Z-axis (elevation) restrict  ⊘

`atElevation`, `minusElevation`. MEOS' `tgeo_restrict_elevation` is in
the upstream `meos/include/meos_internal_geo.h` but not in the
vcpkg-shipped libmeos used by MobilityDuck. Needs MEOS-side exposure
plus a vcpkg bump.

### Alt-name tile / box emitters  ✓

| Name | Status | Notes |
|---|---|---|
| `tboxes(tnumber)` | ✓ | wraps `tnumber_tboxes` |
| `stboxes(tspatial)` for all 4 spatial types | ✓ | wraps `tgeo_stboxes` |
| `spaceTiles(stbox, …)` | ✓ | wraps `stbox_space_tiles` |
| `spaceTimeTiles(stbox, …)` | ✓ | wraps `stbox_space_time_tiles` |
| `valueBoxes(tnumber, …)` | ✓ | wraps `tint_value_boxes` / `tfloat_value_boxes` |
| `timeBoxes(tnumber, …)` | ✓ | wraps `tint_time_boxes` / `tfloat_time_boxes` |
| `valueTimeBoxes(tnumber, …)` | ✓ | wraps `tint_value_time_boxes` / `tfloat_value_time_boxes` |

Tests 064, 066.

### Split-emitter complement  ✓ partial

| Name | Status | Notes |
|---|---|---|
| `timeSplit(temp, interval, ts)` | ✓ | wraps `temporal_time_split`; all 8 temporal types. |
| `valueSplit(tint\|tfloat, size, origin)` | ✓ | wraps `tnumber_value_split`. |
| `quadSplit(stbox)` | ✓ | wraps `stbox_quad_split`; 4 quadrants 2D / 8 octants 3D. |
| `valueTimeSplit(tnumber, …)` | ⊘ | needs second emission channel for the per-bin side-array (Datum list) — single-`LIST<temporal>` shape would silently drop it. |
| `spaceSplit(tspatial, …)` | ⊘ | same: bin = geometry list. |
| `spaceTimeSplit(tspatial, …)` | ⊘ | same. |

The 3 ⊘ ones need a parallel `LIST<geometry>` / `LIST<bigint>` emit
channel (or a `STRUCT(parts, bins)` return shape). Test 065.

### Misc analytics  ✓ partial

| Name | Status | Notes |
|---|---|---|
| `asEWKB(tspatial)` | ✓ | binary form of `asHexEWKB`; wraps `temporal_as_wkb` returning `BLOB`. |
| `tprecision(temp, interval, ts)` | ✓ | sample-and-snap to a coarser grid; tnumber + tspatial. |
| `tsample(temp, interval, ts [, interp])` | ✓ | regular-interval resampling for any temporal type. |
| `time_distance(...)` | ✓ | distance in seconds between tstzspan / tstzspanset / timestamptz arguments. |
| `geomeasure(tpoint, tfloat)` | ◯ | wraps `tpoint_tfloat_to_geomeas`. Builds a geometry tagged with M values. |
| `transformpipeline(geometry, str)` | ◯ | wraps PROJ's transform-pipeline string. MEOS has `stbox_transform_pipeline`; the geometry-side is `ST_Transform` in DuckDB-spatial. |
| `trend(temporal)` | ⊘ | not in MEOS public surface. |
| `transform_gk(geometry)` | ⊘ | German Gauss-Krüger projection helper; PG-only utility. |
| `create_trip(...)` | ⊘ | trip-synthesis helper; lives in MobilityDB's `mobilitydb-tools`, not in MEOS. Out of scope. |

Test 066.

### Aggregate residual  ✓ partial

| Name | Status | Notes |
|---|---|---|
| `appendInstantAgg(temp, interp text)` | ✓ | 2-arg variant for all 8 temporal types. |
| `appendInstantAgg(temp, interp text, maxdist float, maxt interval)` | ⊘ | DuckDB stock aggregate templates don't cover 4-ary cleanly; needs custom dispatch. |
| `setUnion(geography) → geogset` | ✗ | blocked on `geography` SQL type. |

## How to pick up an open item

1. Check the MEOS API exists in the vcpkg-shipped headers
   (`build/release/vcpkg_installed/x64-linux-release/include/meos*.h`).
   If yes, proceed. If no, the item belongs to the ⊘ bucket and needs
   MEOS-side work first.
2. Add the executor — most fit the existing template patterns
   (`UnaryExecutor::ExecuteWithNulls`, `BinaryExecutor::Execute`,
   list-vector emitters via `EmitTboxList` / `EmitSpanList` /
   inline stbox-list helpers).
3. Register in the appropriate `temporal.cpp` /
   `tgeometry_ops.cpp` / `tgeography_ops.cpp` / `stbox.cpp`
   block. Mirror MobilityDB's SQL signature shape (default args,
   return type) — see `mobilitydb/sql/{temporal,geo}/*.in.sql` for
   the reference.
4. Add a parity test in `test/sql/parity/0XX_<topic>.test`. Use
   `IS NOT NULL` / `len()` / numeric-tolerance forms — avoid
   embedding timestamp output in goldens (the harness's local-TZ
   default makes those flaky); pin timestamps with `+00` if the
   test depends on instant alignment.
