# MobilityDuck parity status — surface-level audit

Generated 2026-04-30. **Active scope** (temporal + geo): 597/1261 names covered (47.3%). Deferred families (cbuffer, npoint, pose, rgeo) listed in an appendix and not counted in headline coverage.

**Methodology**: parsed `CREATE FUNCTION` from `mobilitydb/sql/**/*.in.sql` and `RegisterFunction(ScalarFunction("name",...))` (plus aggregate / table-function variants) from `MobilityDuck/src/**/*.cpp`. Match is by **function name only**, case-insensitive. A name registered in MobilityDuck is treated as covering all its overloads; per-overload signature parity is not verified at this granularity.

**Caveats**:
- A name match doesn't prove signature parity. e.g. `before(temporal, temporal)` registered in MobilityDuck does not necessarily cover MobilityDB's `before(tstzspan, temporal)`; a per-overload audit is needed for the full picture.
- Some MobilityDB names are internal helpers (gist/spgist support functions, transition functions for aggregates) — these never need user-facing SQL registration but they show as 'missing' here. Sections dominated by these are flagged in the per-section detail.
- DuckDB rejects multi-character operator tokens (`<<#`, `|>>`, `<#>`, `|=|`, `~=`); equivalent named functions are registered. See `docs/DuckDB-Parity-Gaps.md` for the catalogue.

Regenerate with `python3 scripts/parity-audit.py --mdb ../MobilityDB --mduck . --out docs/parity-status.md`. Deferred families are configured at the top of that script.

## Active-scope coverage summary

| Section | MDB names | Covered | Missing | Coverage | MDB operators |
|---|---:|---:|---:|---:|---:|
| `geo/050_geoset.in.sql` | 56 | 31 | 25 | 55% | 46 |
| `geo/051_stbox.in.sql` | 83 | 59 | 24 | 71% | 29 |
| `geo/052_tgeo.in.sql` | 80 | 58 | 22 | 72% | 12 |
| `geo/052_tpoint.in.sql` | 78 | 59 | 19 | 76% | 12 |
| `geo/053_tgeo_inout.in.sql` | 18 | 3 | 15 | 17% | 0 |
| `geo/053_tpoint_inout.in.sql` | 18 | 3 | 15 | 17% | 0 |
| `geo/054_tgeo_compops.in.sql` | 7 | 0 | 7 | 0% | 36 |
| `geo/054_tpoint_compops.in.sql` | 6 | 0 | 6 | 0% | 36 |
| `geo/056_tgeo_spatialfuncs.in.sql` | 17 | 10 | 7 | 59% | 0 |
| `geo/056_tpoint_spatialfuncs.in.sql` | 28 | 22 | 6 | 79% | 0 |
| `geo/058_tgeo_tile.in.sql` | 5 | 0 | 5 | 0% | 0 |
| `geo/058_tpoint_tile.in.sql` | 11 | 0 | 11 | 0% | 0 |
| `geo/060_tgeo_boxops.in.sql` | 13 | 5 | 8 | 38% | 50 |
| `geo/060_tpoint_boxops.in.sql` | 13 | 5 | 8 | 38% | 50 |
| `geo/062_tgeo_posops.in.sql` | 16 | 0 | 16 | 0% | 76 |
| `geo/062_tpoint_posops.in.sql` | 16 | 0 | 16 | 0% | 76 |
| `geo/064_tgeo_distance.in.sql` | 4 | 2 | 2 | 50% | 16 |
| `geo/064_tpoint_distance.in.sql` | 4 | 2 | 2 | 50% | 21 |
| `geo/066_tpoint_similarity.in.sql` | 5 | 2 | 3 | 40% | 0 |
| `geo/068_tgeo_aggfuncs.in.sql` | 9 | 0 | 9 | 0% | 0 |
| `geo/068_tpoint_aggfuncs.in.sql` | 12 | 0 | 12 | 0% | 0 |
| `geo/070_tgeo_spatialrels.in.sql` | 14 | 11 | 3 | 79% | 0 |
| `geo/070_tpoint_spatialrels.in.sql` | 12 | 11 | 1 | 92% | 0 |
| `geo/072_tgeo_tempspatialrels.in.sql` | 6 | 5 | 1 | 83% | 0 |
| `geo/072_tpoint_tempspatialrels.in.sql` | 5 | 5 | 0 | 100% | 0 |
| `geo/073_tgeo_gist.in.sql` | 8 | 0 | 8 | 0% | 0 |
| `geo/073_tpoint_gist.in.sql` | 3 | 0 | 3 | 0% | 0 |
| `geo/074_tgeo_spgist.in.sql` | 9 | 0 | 9 | 0% | 0 |
| `geo/076_tgeo_analytics.in.sql` | 13 | 1 | 12 | 8% | 0 |
| `geo/076_tpoint_analytics.in.sql` | 18 | 3 | 15 | 17% | 0 |
| `geo/078_tpoint_datagen.in.sql` | 1 | 0 | 1 | 0% | 0 |
| `temporal/001_set.in.sql` | 82 | 35 | 47 | 43% | 38 |
| `temporal/002_set_ops.in.sql` | 11 | 11 | 0 | 100% | 176 |
| `temporal/003_span.in.sql` | 68 | 35 | 33 | 51% | 30 |
| `temporal/005_span_ops.in.sql` | 12 | 12 | 0 | 100% | 160 |
| `temporal/007_spanset.in.sql` | 81 | 50 | 31 | 62% | 30 |
| `temporal/009_spanset_ops.in.sql` | 14 | 12 | 2 | 86% | 280 |
| `temporal/011_span_indexes.in.sql` | 19 | 0 | 19 | 0% | 0 |
| `temporal/012_spanset_indexes.in.sql` | 3 | 0 | 3 | 0% | 0 |
| `temporal/013_set_indexes.in.sql` | 10 | 0 | 10 | 0% | 0 |
| `temporal/015_span_aggfuncs.in.sql` | 10 | 0 | 10 | 0% | 0 |
| `temporal/019_geo_constructors.in.sql` | 7 | 0 | 7 | 0% | 0 |
| `temporal/021_tbox.in.sql` | 60 | 48 | 12 | 80% | 21 |
| `temporal/022_temporal.in.sql` | 117 | 76 | 41 | 65% | 24 |
| `temporal/023_temporal_inout.in.sql` | 16 | 3 | 13 | 19% | 0 |
| `temporal/025_temporal_tile.in.sql` | 16 | 0 | 16 | 0% | 0 |
| `temporal/026_tnumber_mathfuncs.in.sql` | 17 | 8 | 9 | 47% | 24 |
| `temporal/028_tbool_boolops.in.sql` | 4 | 1 | 3 | 25% | 7 |
| `temporal/029_ttext_textfuncs.in.sql` | 4 | 3 | 1 | 75% | 3 |
| `temporal/030_temporal_compops.in.sql` | 19 | 0 | 19 | 0% | 180 |
| `temporal/032_temporal_boxops.in.sql` | 11 | 3 | 8 | 27% | 100 |
| `temporal/034_temporal_posops.in.sql` | 8 | 0 | 8 | 0% | 112 |
| `temporal/036_tnumber_distance.in.sql` | 2 | 1 | 1 | 50% | 17 |
| `temporal/038_temporal_similarity.in.sql` | 5 | 2 | 3 | 40% | 0 |
| `temporal/040_temporal_aggfuncs.in.sql` | 40 | 0 | 40 | 0% | 0 |
| `temporal/042_temporal_waggfuncs.in.sql` | 8 | 0 | 8 | 0% | 0 |
| `temporal/043_temporal_gist.in.sql` | 14 | 0 | 14 | 0% | 0 |
| `temporal/044_temporal_spgist.in.sql` | 10 | 0 | 10 | 0% | 0 |
| `temporal/046_temporal_analytics.in.sql` | 4 | 0 | 4 | 0% | 0 |
| `temporal/999_oid_cache.in.sql` | 1 | 0 | 1 | 0% | 0 |
| **TOTAL (active)** | **1261** | **597** | **664** | **47%** | — |

## Missing function names per active section

### `geo/050_geoset.in.sql` — 25 missing of 56 (55% covered)

- `geogsetFromBinary`
- `geogsetFromEWKB`
- `geogsetFromEWKT`
- `geogsetFromHexWKB`
- `geogsetFromText`
- `geogset_in`
- `geogset_out`
- `geogset_recv`
- `geogset_send`
- `geogset_union_finalfn`
- `geomsetFromBinary`
- `geomsetFromEWKB`
- `geomsetFromEWKT`
- `geomsetFromHexWKB`
- `geomsetFromText`
- `geomset_in`
- `geomset_out`
- `geomset_recv`
- `geomset_send`
- `geomset_union_finalfn`
- `set_union_transfn` (4 overloads)
- `spatialset_analyze`
- `spatialset_sel`
- `transformPipeline` (2 overloads)
- `unnest` (2 overloads)

### `geo/051_stbox.in.sql` — 24 missing of 83 (71% covered)

- `box2d`
- `box3d`
- `geodstboxT` (2 overloads)
- `geodstboxZ`
- `geodstboxZT` (2 overloads)
- `geography`
- `perimeter`
- `quadSplit`
- `stboxFromHexWKB`
- `stboxT` (2 overloads)
- `stboxX`
- `stboxXT` (2 overloads)
- `stboxZ`
- `stboxZT` (2 overloads)
- `stbox_extent_combinefn`
- `stbox_extent_transfn`
- `stbox_hash`
- `stbox_hash_extended`
- `stbox_in`
- `stbox_out`
- `stbox_recv`
- `stbox_send`
- `tspatial_joinsel`
- `tspatial_sel`

### `geo/052_tgeo.in.sql` — 22 missing of 80 (72% covered)

- `temporal_hash` (2 overloads)
- `temporal_out` (2 overloads)
- `temporal_send` (2 overloads)
- `tgeography` (5 overloads)
- `tgeographyInst`
- `tgeographySeq` (3 overloads)
- `tgeographySeqSet` (3 overloads)
- `tgeographySeqSetGaps`
- `tgeography_in`
- `tgeography_recv`
- `tgeography_typmod_in`
- `tgeometrySeqSet` (3 overloads)
- `tgeometrySeqSetGaps`
- `tgeometry_in`
- `tgeometry_recv`
- `tgeometry_typmod_in`
- `timeSplit` (2 overloads)
- `tprecision` (2 overloads)
- `tsample` (2 overloads)
- `tspatial_analyze`
- `tspatial_typmod_out`
- `unnest` (2 overloads)

### `geo/052_tpoint.in.sql` — 19 missing of 78 (76% covered)

- `temporal_hash` (2 overloads)
- `temporal_out` (2 overloads)
- `temporal_send` (2 overloads)
- `tgeogpoint` (5 overloads)
- `tgeogpointInst`
- `tgeogpointSeq` (3 overloads)
- `tgeogpointSeqSet` (3 overloads)
- `tgeogpointSeqSetGaps`
- `tgeogpoint_in`
- `tgeogpoint_recv`
- `tgeogpoint_typmod_in`
- `tgeompointSeqSetGaps`
- `tgeompoint_in`
- `tgeompoint_recv`
- `tgeompoint_typmod_in`
- `timeSplit` (2 overloads)
- `tprecision` (2 overloads)
- `tsample` (2 overloads)
- `unnest` (2 overloads)

### `geo/053_tgeo_inout.in.sql` — 15 missing of 18 (17% covered)

- `asEWKB` (2 overloads)
- `asHexEWKB` (2 overloads)
- `asMFJSON` (2 overloads)
- `tgeographyFromBinary`
- `tgeographyFromEWKB`
- `tgeographyFromEWKT`
- `tgeographyFromHexEWKB`
- `tgeographyFromMFJSON`
- `tgeographyFromText`
- `tgeometryFromBinary`
- `tgeometryFromEWKB`
- `tgeometryFromEWKT`
- `tgeometryFromHexEWKB`
- `tgeometryFromMFJSON`
- `tgeometryFromText`

### `geo/053_tpoint_inout.in.sql` — 15 missing of 18 (17% covered)

- `asEWKB` (2 overloads)
- `asHexEWKB` (2 overloads)
- `asMFJSON` (2 overloads)
- `tgeogpointFromBinary`
- `tgeogpointFromEWKB`
- `tgeogpointFromEWKT`
- `tgeogpointFromHexEWKB`
- `tgeogpointFromMFJSON`
- `tgeogpointFromText`
- `tgeompointFromBinary`
- `tgeompointFromEWKB`
- `tgeompointFromEWKT`
- `tgeompointFromHexEWKB`
- `tgeompointFromMFJSON`
- `tgeompointFromText`

### `geo/054_tgeo_compops.in.sql` — 7 missing of 7 (0% covered)

- `always_eq` (6 overloads)
- `always_ne` (6 overloads)
- `ever_eq` (6 overloads)
- `ever_ne` (6 overloads)
- `tgeo_teq` (12 overloads)
- `tgeo_tne` (12 overloads)
- `tspatial_supportfn`

### `geo/054_tpoint_compops.in.sql` — 6 missing of 6 (0% covered)

- `always_eq` (6 overloads)
- `always_ne` (6 overloads)
- `ever_eq` (6 overloads)
- `ever_ne` (6 overloads)
- `tgeo_teq` (12 overloads)
- `tgeo_tne` (12 overloads)

### `geo/056_tgeo_spatialfuncs.in.sql` — 7 missing of 17 (59% covered)

- `centroid` (2 overloads)
- `convexHull`
- `tCentroid`
- `tgeogpoint`
- `tgeography` (2 overloads)
- `transformPipeline` (2 overloads)
- `traversedArea` (2 overloads)

### `geo/056_tpoint_spatialfuncs.in.sql` — 6 missing of 28 (79% covered)

- `bearing` (8 overloads)
- `convexHull`
- `tdirection` (2 overloads)
- `tgeogpoint`
- `transformPipeline` (3 overloads)
- `transform_gk` (2 overloads)

### `geo/058_tgeo_tile.in.sql` — 5 missing of 5 (0% covered)

- `spaceBoxes` (3 overloads)
- `spaceSplit` (3 overloads)
- `spaceTimeBoxes` (3 overloads)
- `spaceTimeSplit` (3 overloads)
- `timeBoxes`

### `geo/058_tpoint_tile.in.sql` — 11 missing of 11 (0% covered)

- `getSpaceTile` (3 overloads)
- `getSpaceTimeTile` (3 overloads)
- `getStboxTimeTile`
- `spaceBoxes` (3 overloads)
- `spaceSplit` (3 overloads)
- `spaceTiles` (3 overloads)
- `spaceTimeBoxes` (3 overloads)
- `spaceTimeSplit` (3 overloads)
- `spaceTimeTiles` (3 overloads)
- `timeBoxes`
- `timeTiles`

### `geo/060_tgeo_boxops.in.sql` — 8 missing of 13 (38% covered)

- `splitEachNStboxes` (2 overloads)
- `splitNStboxes` (2 overloads)
- `stboxes` (2 overloads)
- `temporal_adjacent` (10 overloads)
- `temporal_contained` (10 overloads)
- `temporal_contains` (10 overloads)
- `temporal_overlaps` (10 overloads)
- `temporal_same` (10 overloads)

### `geo/060_tpoint_boxops.in.sql` — 8 missing of 13 (38% covered)

- `splitEachNStboxes` (4 overloads)
- `splitNStboxes` (4 overloads)
- `stboxes` (4 overloads)
- `temporal_adjacent` (10 overloads)
- `temporal_contained` (10 overloads)
- `temporal_contains` (10 overloads)
- `temporal_overlaps` (10 overloads)
- `temporal_same` (10 overloads)

### `geo/062_tgeo_posops.in.sql` — 16 missing of 16 (0% covered)

- `temporal_above` (3 overloads)
- `temporal_after` (10 overloads)
- `temporal_back` (3 overloads)
- `temporal_before` (10 overloads)
- `temporal_below` (3 overloads)
- `temporal_front` (3 overloads)
- `temporal_left` (3 overloads)
- `temporal_overabove` (3 overloads)
- `temporal_overafter` (10 overloads)
- `temporal_overback` (3 overloads)
- `temporal_overbefore` (10 overloads)
- `temporal_overbelow` (3 overloads)
- `temporal_overfront` (3 overloads)
- `temporal_overleft` (3 overloads)
- `temporal_overright` (3 overloads)
- `temporal_right` (3 overloads)

### `geo/062_tpoint_posops.in.sql` — 16 missing of 16 (0% covered)

- `temporal_above` (3 overloads)
- `temporal_after` (10 overloads)
- `temporal_back` (3 overloads)
- `temporal_before` (10 overloads)
- `temporal_below` (3 overloads)
- `temporal_front` (3 overloads)
- `temporal_left` (3 overloads)
- `temporal_overabove` (3 overloads)
- `temporal_overafter` (10 overloads)
- `temporal_overback` (3 overloads)
- `temporal_overbefore` (10 overloads)
- `temporal_overbelow` (3 overloads)
- `temporal_overfront` (3 overloads)
- `temporal_overleft` (3 overloads)
- `temporal_overright` (3 overloads)
- `temporal_right` (3 overloads)

### `geo/064_tgeo_distance.in.sql` — 2 missing of 4 (50% covered)

- `nearestApproachInstant` (6 overloads)
- `tdistance` (6 overloads)

### `geo/064_tpoint_distance.in.sql` — 2 missing of 4 (50% covered)

- `nearestApproachInstant` (6 overloads)
- `tdistance` (6 overloads)

### `geo/066_tpoint_similarity.in.sql` — 3 missing of 5 (40% covered)

- `dynTimeWarpDistance` (2 overloads)
- `dynTimeWarpPath` (2 overloads)
- `frechetDistancePath` (2 overloads)

### `geo/068_tgeo_aggfuncs.in.sql` — 9 missing of 9 (0% covered)

- `tcount_transfn` (2 overloads)
- `temporal_app_tinst_transfn` (6 overloads)
- `temporal_app_tseq_transfn` (2 overloads)
- `temporal_append_finalfn` (2 overloads)
- `temporal_merge_transfn` (2 overloads)
- `tgeography_tagg_finalfn`
- `tgeometry_tagg_finalfn`
- `tspatial_extent_transfn` (2 overloads)
- `wcount_transfn` (2 overloads)

### `geo/068_tpoint_aggfuncs.in.sql` — 12 missing of 12 (0% covered)

- `tcentroid_combinefn`
- `tcentroid_finalfn`
- `tcentroid_transfn`
- `tcount_transfn` (2 overloads)
- `temporal_app_tinst_transfn` (6 overloads)
- `temporal_app_tseq_transfn` (2 overloads)
- `temporal_append_finalfn` (2 overloads)
- `temporal_merge_transfn` (2 overloads)
- `tgeogpoint_tagg_finalfn`
- `tgeompoint_tagg_finalfn`
- `tspatial_extent_transfn` (2 overloads)
- `wcount_transfn` (2 overloads)

### `geo/070_tgeo_spatialrels.in.sql` — 3 missing of 14 (79% covered)

- `_edisjoint` (6 overloads)
- `aCovers` (3 overloads)
- `eCovers` (3 overloads)

### `geo/070_tpoint_spatialrels.in.sql` — 1 missing of 12 (92% covered)

- `_edisjoint` (6 overloads)

### `geo/072_tgeo_tempspatialrels.in.sql` — 1 missing of 6 (83% covered)

- `tCovers` (3 overloads)

### `geo/073_tgeo_gist.in.sql` — 8 missing of 8 (0% covered)

- `stbox_gist_distance`
- `stbox_gist_penalty`
- `stbox_gist_picksplit`
- `stbox_gist_same`
- `stbox_gist_union`
- `tgeography_gist_consistent`
- `tgeometry_gist_consistent`
- `tspatial_gist_compress`

### `geo/073_tpoint_gist.in.sql` — 3 missing of 3 (0% covered)

- `stbox_gist_consistent`
- `tgeogpoint_gist_consistent`
- `tgeompoint_gist_consistent`

### `geo/074_tgeo_spgist.in.sql` — 9 missing of 9 (0% covered)

- `stbox_kdtree_choose`
- `stbox_kdtree_inner_consistent`
- `stbox_kdtree_picksplit`
- `stbox_quadtree_choose`
- `stbox_quadtree_inner_consistent`
- `stbox_quadtree_picksplit`
- `stbox_spgist_config`
- `stbox_spgist_leaf_consistent`
- `tspatial_spgist_compress`

### `geo/076_tgeo_analytics.in.sql` — 12 missing of 13 (8% covered)

- `affine` (2 overloads)
- `asMVTGeom`
- `douglasPeuckerSimplify`
- `maxDistSimplify`
- `minDistSimplify` (2 overloads)
- `minTimeDeltaSimplify` (2 overloads)
- `rotate` (3 overloads)
- `rotateX`
- `rotateY`
- `rotateZ`
- `translate` (2 overloads)
- `transscale`

### `geo/076_tpoint_analytics.in.sql` — 15 missing of 18 (17% covered)

- `affine` (2 overloads)
- `asMVTGeom`
- `douglasPeuckerSimplify`
- `geoMeasure` (2 overloads)
- `geography` (2 overloads)
- `maxDistSimplify`
- `minDistSimplify` (2 overloads)
- `minTimeDeltaSimplify` (2 overloads)
- `rotate` (3 overloads)
- `rotateX`
- `rotateY`
- `rotateZ`
- `tgeogpoint`
- `translate` (2 overloads)
- `transscale`

### `geo/078_tpoint_datagen.in.sql` — 1 missing of 1 (0% covered)

- `create_trip`

### `temporal/001_set.in.sql` — 47 missing of 82 (43% covered)

- `bigintsetFromBinary`
- `bigintsetFromHexWKB`
- `bigintset_in`
- `bigintset_out`
- `bigintset_recv`
- `bigintset_send`
- `bigintset_union_finalfn`
- `datesetFromBinary`
- `datesetFromHexWKB`
- `dateset_in`
- `dateset_out`
- `dateset_recv`
- `dateset_send`
- `dateset_union_finalfn`
- `floatsetFromBinary`
- `floatsetFromHexWKB`
- `floatset_in`
- `floatset_out`
- `floatset_recv`
- `floatset_send`
- `floatset_union_finalfn`
- `intsetFromBinary`
- `intsetFromHexWKB`
- `intset_in`
- `intset_out`
- `intset_recv`
- `intset_send`
- `intset_union_finalfn`
- `set_union_transfn` (12 overloads)
- `span_analyze`
- `span_joinsel`
- `span_sel`
- `textsetFromBinary`
- `textsetFromHexWKB`
- `textset_in`
- `textset_out`
- `textset_recv`
- `textset_send`
- `textset_union_finalfn`
- `tstzsetFromBinary`
- `tstzsetFromHexWKB`
- `tstzset_in`
- `tstzset_out`
- `tstzset_recv`
- `tstzset_send`
- `tstzset_union_finalfn`
- `unnest` (6 overloads)

### `temporal/003_span.in.sql` — 33 missing of 68 (51% covered)

- `_mobdb_span_joinsel`
- `_mobdb_span_sel` (5 overloads)
- `bigintspanFromBinary`
- `bigintspanFromHexWKB`
- `bigintspan_in`
- `bigintspan_out`
- `bigintspan_recv`
- `bigintspan_send`
- `datespanFromBinary`
- `datespanFromHexWKB`
- `datespan_in`
- `datespan_out`
- `datespan_recv`
- `datespan_send`
- `floatspanFromBinary`
- `floatspanFromHexWKB`
- `floatspan_in`
- `floatspan_out`
- `floatspan_recv`
- `floatspan_send`
- `intspanFromBinary`
- `intspanFromHexWKB`
- `intspan_in`
- `intspan_out`
- `intspan_recv`
- `intspan_send`
- `range` (4 overloads)
- `tstzspanFromBinary`
- `tstzspanFromHexWKB`
- `tstzspan_in`
- `tstzspan_out`
- `tstzspan_recv`
- `tstzspan_send`

### `temporal/007_spanset.in.sql` — 31 missing of 81 (62% covered)

- `bigintspansetFromBinary`
- `bigintspansetFromHexWKB`
- `bigintspanset_in`
- `bigintspanset_out`
- `bigintspanset_recv`
- `bigintspanset_send`
- `datespansetFromBinary`
- `datespansetFromHexWKB`
- `datespanset_in`
- `datespanset_out`
- `datespanset_recv`
- `datespanset_send`
- `floatspansetFromBinary`
- `floatspansetFromHexWKB`
- `floatspanset_in`
- `floatspanset_out`
- `floatspanset_recv`
- `floatspanset_send`
- `intspansetFromBinary`
- `intspansetFromHexWKB`
- `intspanset_in`
- `intspanset_out`
- `intspanset_recv`
- `intspanset_send`
- `multirange` (4 overloads)
- `tstzspansetFromBinary`
- `tstzspansetFromHexWKB`
- `tstzspanset_in`
- `tstzspanset_out`
- `tstzspanset_recv`
- `tstzspanset_send`

### `temporal/009_spanset_ops.in.sql` — 2 missing of 14 (86% covered)

- `time_distance` (5 overloads)
- `tprecision` (4 overloads)

### `temporal/011_span_indexes.in.sql` — 19 missing of 19 (0% covered)

- `bigintspan_spgist_config`
- `datespan_spgist_config`
- `floatspan_spgist_config`
- `intspan_spgist_config`
- `span_gist_consistent` (5 overloads)
- `span_gist_distance` (5 overloads)
- `span_gist_fetch`
- `span_gist_penalty`
- `span_gist_picksplit`
- `span_gist_same` (5 overloads)
- `span_gist_union`
- `span_kdtree_choose`
- `span_kdtree_inner_consistent`
- `span_kdtree_picksplit`
- `span_quadtree_choose`
- `span_quadtree_inner_consistent`
- `span_quadtree_picksplit`
- `span_spgist_leaf_consistent`
- `tstzspan_spgist_config`

### `temporal/012_spanset_indexes.in.sql` — 3 missing of 3 (0% covered)

- `span_gist_consistent` (5 overloads)
- `spanset_gist_compress`
- `spanset_spgist_compress`

### `temporal/013_set_indexes.in.sql` — 10 missing of 10 (0% covered)

- `bigintset_spgist_config`
- `floatset_spgist_config`
- `intset_spgist_config`
- `set_gin_extract_query` (3 overloads)
- `set_gin_extract_value` (3 overloads)
- `set_gin_triconsistent` (3 overloads)
- `set_gist_compress`
- `set_spgist_compress`
- `span_gist_consistent` (5 overloads)
- `span_gist_distance` (3 overloads)

### `temporal/015_span_aggfuncs.in.sql` — 10 missing of 10 (0% covered)

- `bigintspan_union_finalfn`
- `datespan_union_finalfn`
- `floatspan_union_finalfn`
- `intspan_union_finalfn`
- `set_extent_transfn` (5 overloads)
- `span_extent_combinefn` (5 overloads)
- `span_extent_transfn` (10 overloads)
- `spanset_extent_transfn` (5 overloads)
- `spanset_union_transfn` (5 overloads)
- `tstzspan_union_finalfn`

### `temporal/019_geo_constructors.in.sql` — 7 missing of 7 (0% covered)

- `box`
- `circle`
- `line`
- `lseg`
- `path`
- `point`
- `polygon`

### `temporal/021_tbox.in.sql` — 12 missing of 60 (80% covered)

- `tboxFromBinary`
- `tboxFromHexWKB`
- `tbox_extent_combinefn`
- `tbox_extent_transfn`
- `tbox_hash`
- `tbox_hash_extended`
- `tbox_in`
- `tbox_out`
- `tbox_recv`
- `tbox_send`
- `tnumber_joinsel`
- `tnumber_sel`

### `temporal/022_temporal.in.sql` — 41 missing of 117 (65% covered)

- `maxValue` (3 overloads)
- `minValue` (3 overloads)
- `mobilitydb_full_version`
- `mobilitydb_version`
- `tbool` (5 overloads)
- `tboolInst`
- `tboolSeq` (2 overloads)
- `tboolSeqSet` (2 overloads)
- `tboolSeqSetGaps`
- `tbool_in`
- `tbool_recv`
- `temporal_analyze`
- `temporal_hash` (4 overloads)
- `temporal_joinsel`
- `temporal_out` (4 overloads)
- `temporal_sel`
- `temporal_send` (4 overloads)
- `temporal_typmod_in`
- `temporal_typmod_out`
- `tfloatInst`
- `tfloatSeq` (2 overloads)
- `tfloatSeqSet` (2 overloads)
- `tfloatSeqSetGaps`
- `tfloat_in`
- `tfloat_recv`
- `tintInst`
- `tintSeq` (2 overloads)
- `tintSeqSet` (2 overloads)
- `tintSeqSetGaps`
- `tint_in`
- `tint_recv`
- `tprecision` (2 overloads)
- `tsample` (4 overloads)
- `ttext` (5 overloads)
- `ttextInst`
- `ttextSeq` (2 overloads)
- `ttextSeqSet` (2 overloads)
- `ttextSeqSetGaps`
- `ttext_in`
- `ttext_recv`
- `unnest` (3 overloads)

### `temporal/023_temporal_inout.in.sql` — 13 missing of 16 (19% covered)

- `asMFJSON` (4 overloads)
- `tboolFromBinary`
- `tboolFromHexWKB`
- `tboolFromMFJSON`
- `tfloatFromBinary`
- `tfloatFromHexWKB`
- `tfloatFromMFJSON`
- `tintFromBinary`
- `tintFromHexWKB`
- `tintFromMFJSON`
- `ttextFromBinary`
- `ttextFromHexWKB`
- `ttextFromMFJSON`

### `temporal/025_temporal_tile.in.sql` — 16 missing of 16 (0% covered)

- `bins` (10 overloads)
- `getBin` (5 overloads)
- `getTBoxTimeTile`
- `getValueTile`
- `getValueTimeTile`
- `timeBins` (4 overloads)
- `timeBoxes` (2 overloads)
- `timeSplit` (4 overloads)
- `timeTiles`
- `valueBins` (2 overloads)
- `valueBoxes` (2 overloads)
- `valueSplit` (2 overloads)
- `valueTiles`
- `valueTimeBoxes` (2 overloads)
- `valueTimeSplit` (2 overloads)
- `valueTimeTiles`

### `temporal/026_tnumber_mathfuncs.in.sql` — 9 missing of 17 (47% covered)

- `deltaValue` (2 overloads)
- `exp`
- `ln`
- `log10`
- `tnumber_add` (6 overloads)
- `tnumber_div` (6 overloads)
- `tnumber_mult` (6 overloads)
- `tnumber_sub` (6 overloads)
- `trend` (2 overloads)

### `temporal/028_tbool_boolops.in.sql` — 3 missing of 4 (25% covered)

- `tbool_and` (3 overloads)
- `tbool_not`
- `tbool_or` (3 overloads)

### `temporal/029_ttext_textfuncs.in.sql` — 1 missing of 4 (75% covered)

- `ttext_cat` (3 overloads)

### `temporal/030_temporal_compops.in.sql` — 19 missing of 19 (0% covered)

- `always_eq` (12 overloads)
- `always_ge` (9 overloads)
- `always_gt` (9 overloads)
- `always_le` (9 overloads)
- `always_lt` (9 overloads)
- `always_ne` (12 overloads)
- `ever_eq` (12 overloads)
- `ever_ge` (9 overloads)
- `ever_gt` (9 overloads)
- `ever_le` (9 overloads)
- `ever_lt` (9 overloads)
- `ever_ne` (12 overloads)
- `temporal_teq` (12 overloads)
- `temporal_tge` (10 overloads)
- `temporal_tgt` (10 overloads)
- `temporal_tle` (9 overloads)
- `temporal_tlt` (9 overloads)
- `temporal_tne` (12 overloads)
- `tnumber_supportfn`

### `temporal/032_temporal_boxops.in.sql` — 8 missing of 11 (27% covered)

- `splitEachNTboxes` (2 overloads)
- `splitNTboxes` (2 overloads)
- `tboxes` (2 overloads)
- `temporal_adjacent` (20 overloads)
- `temporal_contained` (20 overloads)
- `temporal_contains` (20 overloads)
- `temporal_overlaps` (20 overloads)
- `temporal_same` (20 overloads)

### `temporal/034_temporal_posops.in.sql` — 8 missing of 8 (0% covered)

- `temporal_after` (16 overloads)
- `temporal_before` (16 overloads)
- `temporal_left` (12 overloads)
- `temporal_overafter` (16 overloads)
- `temporal_overbefore` (16 overloads)
- `temporal_overleft` (12 overloads)
- `temporal_overright` (12 overloads)
- `temporal_right` (12 overloads)

### `temporal/036_tnumber_distance.in.sql` — 1 missing of 2 (50% covered)

- `tDistance` (6 overloads)

### `temporal/038_temporal_similarity.in.sql` — 3 missing of 5 (40% covered)

- `dynTimeWarpDistance` (2 overloads)
- `dynTimeWarpPath` (2 overloads)
- `frechetDistancePath` (2 overloads)

### `temporal/040_temporal_aggfuncs.in.sql` — 40 missing of 40 (0% covered)

- `taggstate_deserialize`
- `taggstate_serialize`
- `tavg_combinefn`
- `tavg_finalfn`
- `tavg_transfn` (2 overloads)
- `tbool_tagg_finalfn`
- `tbool_tand_combinefn`
- `tbool_tand_transfn`
- `tbool_tor_combinefn`
- `tbool_tor_transfn`
- `tcount_combinefn`
- `tcount_transfn` (8 overloads)
- `temporal_app_tinst_transfn` (12 overloads)
- `temporal_app_tseq_transfn` (4 overloads)
- `temporal_append_finalfn` (4 overloads)
- `temporal_extent_combinefn`
- `temporal_extent_transfn` (2 overloads)
- `temporal_merge_combinefn`
- `temporal_merge_transfn` (4 overloads)
- `tfloat_tagg_finalfn`
- `tfloat_tmax_combinefn`
- `tfloat_tmax_transfn`
- `tfloat_tmin_combinefn`
- `tfloat_tmin_transfn`
- `tfloat_tsum_combinefn`
- `tfloat_tsum_transfn`
- `tint_tagg_finalfn`
- `tint_tmax_combinefn`
- `tint_tmax_transfn`
- `tint_tmin_combinefn`
- `tint_tmin_transfn`
- `tint_tsum_combinefn`
- `tint_tsum_transfn`
- `tnumber_extent_combinefn`
- `tnumber_extent_transfn` (2 overloads)
- `ttext_tagg_finalfn`
- `ttext_tmax_combinefn`
- `ttext_tmax_transfn`
- `ttext_tmin_combinefn`
- `ttext_tmin_transfn`

### `temporal/042_temporal_waggfuncs.in.sql` — 8 missing of 8 (0% covered)

- `tfloat_wmax_transfn`
- `tfloat_wmin_transfn`
- `tfloat_wsum_transfn`
- `tint_wmax_transfn`
- `tint_wmin_transfn`
- `tint_wsum_transfn`
- `wavg_transfn` (2 overloads)
- `wcount_transfn` (2 overloads)

### `temporal/043_temporal_gist.in.sql` — 14 missing of 14 (0% covered)

- `tbool_gist_compress`
- `tbool_gist_consistent`
- `tbox_gist_consistent`
- `tbox_gist_distance`
- `tbox_gist_penalty`
- `tbox_gist_picksplit`
- `tbox_gist_same`
- `tbox_gist_union`
- `tfloat_gist_compress`
- `tfloat_gist_consistent`
- `tint_gist_compress`
- `tint_gist_consistent`
- `ttext_gist_compress`
- `ttext_gist_consistent`

### `temporal/044_temporal_spgist.in.sql` — 10 missing of 10 (0% covered)

- `tbox_kdtree_choose`
- `tbox_kdtree_inner_consistent`
- `tbox_kdtree_picksplit`
- `tbox_quadtree_choose`
- `tbox_quadtree_inner_consistent`
- `tbox_quadtree_picksplit`
- `tbox_spgist_config`
- `tbox_spgist_leaf_consistent`
- `temporal_spgist_compress`
- `tnumber_spgist_compress`

### `temporal/046_temporal_analytics.in.sql` — 4 missing of 4 (0% covered)

- `douglasPeuckerSimplify`
- `maxDistSimplify`
- `minDistSimplify`
- `minTimeDeltaSimplify`

### `temporal/999_oid_cache.in.sql` — 1 missing of 1 (0% covered)

- `fill_oid_cache`

## Deferred families (out of scope for current sweep)

These families (cbuffer, npoint, pose, rgeo) are deferred until the active temporal + geo surface stabilises. Re-include by editing `DEFERRED_FAMILIES` at the top of `scripts/parity-audit.py`. Listed here so the picture stays complete; not counted in headline coverage.

| Section | MDB names | Covered | Missing | Coverage |
|---|---:|---:|---:|---:|
| `cbuffer/150_cbuffer.in.sql` | 31 | 7 | 24 | 23% |
| `cbuffer/151_cbufferset.in.sql` | 42 | 32 | 10 | 76% |
| `cbuffer/152_tcbuffer.in.sql` | 84 | 62 | 22 | 74% |
| `cbuffer/154_tcbuffer_compops.in.sql` | 6 | 0 | 6 | 0% |
| `cbuffer/155_tcbuffer_spatialfuncs.in.sql` | 11 | 7 | 4 | 64% |
| `cbuffer/158_tcbuffer_topops.in.sql` | 7 | 2 | 5 | 29% |
| `cbuffer/159_tcbuffer_posops.in.sql` | 12 | 0 | 12 | 0% |
| `cbuffer/160_tcbuffer_distance.in.sql` | 5 | 2 | 3 | 40% |
| `cbuffer/161_tcbuffer_aggfuncs.in.sql` | 7 | 0 | 7 | 0% |
| `cbuffer/162_tcbuffer_spatialrels.in.sql` | 13 | 11 | 2 | 85% |
| `cbuffer/164_tcbuffer_tempspatialrels.in.sql` | 6 | 5 | 1 | 83% |
| `cbuffer/166_tcbuffer_indexes.in.sql` | 1 | 0 | 1 | 0% |
| `npoint/081_npoint.in.sql` | 41 | 6 | 35 | 15% |
| `npoint/082_npointset.in.sql` | 43 | 29 | 14 | 67% |
| `npoint/083_tnpoint.in.sql` | 77 | 60 | 17 | 78% |
| `npoint/085_tnpoint_compops.in.sql` | 6 | 0 | 6 | 0% |
| `npoint/087_tnpoint_spatialfuncs.in.sql` | 12 | 11 | 1 | 92% |
| `npoint/089_tnpoint_topops.in.sql` | 7 | 2 | 5 | 29% |
| `npoint/090_tnpoint_posops.in.sql` | 12 | 0 | 12 | 0% |
| `npoint/091_tnpoint_routeops.in.sql` | 4 | 0 | 4 | 0% |
| `npoint/092_tnpoint_gin.in.sql` | 3 | 0 | 3 | 0% |
| `npoint/093_tnpoint_distance.in.sql` | 4 | 2 | 2 | 50% |
| `npoint/095_tnpoint_aggfuncs.in.sql` | 8 | 0 | 8 | 0% |
| `npoint/098_tnpoint_indexes.in.sql` | 1 | 0 | 1 | 0% |
| `pose/100_pose.in.sql` | 34 | 8 | 26 | 24% |
| `pose/101_poseset.in.sql` | 46 | 32 | 14 | 70% |
| `pose/102_tpose.in.sql` | 85 | 60 | 25 | 71% |
| `pose/104_tpose_compops.in.sql` | 6 | 0 | 6 | 0% |
| `pose/105_tpose_spatialfuncs.in.sql` | 8 | 7 | 1 | 88% |
| `pose/108_tpose_topops.in.sql` | 7 | 2 | 5 | 29% |
| `pose/109_tpose_posops.in.sql` | 16 | 0 | 16 | 0% |
| `pose/111_tpose_aggfuncs.in.sql` | 7 | 0 | 7 | 0% |
| `pose/113_tpose_distance.in.sql` | 4 | 2 | 2 | 50% |
| `pose/114_tpose_indexes.in.sql` | 1 | 0 | 1 | 0% |
| `rgeo/122_trgeo.in.sql` | 87 | 62 | 25 | 71% |
| `rgeo/124_trgeo_compops.in.sql` | 6 | 0 | 6 | 0% |
| `rgeo/125_trgeo_spatialfuncs.in.sql` | 8 | 7 | 1 | 88% |
| `rgeo/128_trgeo_topops.in.sql` | 5 | 0 | 5 | 0% |
| `rgeo/129_trgeo_posops.in.sql` | 12 | 0 | 12 | 0% |
| `rgeo/131_trgeo_aggfuncs.in.sql` | 7 | 0 | 7 | 0% |
| `rgeo/133_trgeo_distance.in.sql` | 4 | 2 | 2 | 50% |
| `rgeo/133_trgeo_vclip.in.sql` | 6 | 0 | 6 | 0% |
| `rgeo/134_trgeo_indexes.in.sql` | 1 | 0 | 1 | 0% |
| **TOTAL (deferred)** | **793** | **420** | **373** | **53%** |

