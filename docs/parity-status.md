# MobilityDuck parity status — surface-level audit

Generated 2026-05-10. **Active addressable scope** (temporal + geo, excluding PG-only helpers): 811/960 names covered (84.5%).

**Out of scope** (PG-only — no DuckDB equivalent exists): 303 names skipped — 84 from PG-only sections (GiST/SPGiST opclasses, set/span/spanset index files, `019_geo_constructors.in.sql` PG geometric types, `999_oid_cache.in.sql`) plus 219 PG helper functions inside active sections (`*_in/_out/_recv/_send`, `*_transfn/_combinefn/_finalfn/_serialize/_deserialize`, `*_sel/_joinsel/_supportfn/_analyze`, `*_typmod_in/_typmod_out`).  Listed in appendix B; not counted in the headline.

**Deferred families** (cbuffer, npoint, pose, rgeo) appear in appendix C and are also excluded from the headline.

**Methodology**: parsed `CREATE FUNCTION` from `mobilitydb/sql/**/*.in.sql` and `RegisterFunction(ScalarFunction("name",...))` (plus aggregate / table-function variants) from `MobilityDuck/src/**/*.cpp`. Match is by **function name only**, case-insensitive. A name registered in MobilityDuck is treated as covering all its overloads; per-overload signature parity is not verified at this granularity.

**Caveats**:
- A name match doesn't prove signature parity. e.g. `before(temporal, temporal)` registered in MobilityDuck does not necessarily cover MobilityDB's `before(tstzspan, temporal)`; a per-overload audit is needed for the full picture.
- DuckDB rejects multi-character operator tokens (`<<#`, `|>>`, `<#>`, `|=|`, `~=`); equivalent named functions are registered. See `docs/DuckDB-Parity-Gaps.md` for the catalogue.

Regenerate with `python3 scripts/parity-audit.py --mdb ../MobilityDB --mduck . --out docs/parity-status.md`. The OUT_OF_SCOPE_SECTIONS / OUT_OF_SCOPE_NAME_SUFFIXES / DEFERRED_FAMILIES sets at the top of that script control bucketing.

## Active-scope coverage summary (addressable surface)

Per-section counts: `Addressable` = MDB names minus PG-only helpers (see appendix B).  PG-only helper count shown in `OOS` column for transparency.

| Section | Addressable | Covered | Missing | Coverage | OOS | MDB operators |
|---|---:|---:|---:|---:|---:|---:|
| `geo/050_geoset.in.sql` | 43 | 31 | 12 | 72% | 13 | 46 |
| `geo/051_stbox.in.sql` | 75 | 59 | 16 | 79% | 8 | 29 |
| `geo/052_tgeo.in.sql` | 70 | 64 | 6 | 91% | 10 | 12 |
| `geo/052_tpoint.in.sql` | 70 | 66 | 4 | 94% | 8 | 12 |
| `geo/053_tgeo_inout.in.sql` | 18 | 6 | 12 | 33% | 0 | 0 |
| `geo/053_tpoint_inout.in.sql` | 18 | 18 | 0 | 100% | 0 | 0 |
| `geo/054_tgeo_compops.in.sql` | 6 | 6 | 0 | 100% | 1 | 36 |
| `geo/054_tpoint_compops.in.sql` | 6 | 6 | 0 | 100% | 0 | 36 |
| `geo/056_tgeo_spatialfuncs.in.sql` | 17 | 15 | 2 | 88% | 0 | 0 |
| `geo/056_tpoint_spatialfuncs.in.sql` | 30 | 24 | 6 | 80% | 0 | 0 |
| `geo/058_tgeo_tile.in.sql` | 5 | 2 | 3 | 40% | 0 | 0 |
| `geo/058_tpoint_tile.in.sql` | 11 | 8 | 3 | 73% | 0 | 0 |
| `geo/060_tgeo_boxops.in.sql` | 13 | 10 | 3 | 77% | 0 | 50 |
| `geo/060_tpoint_boxops.in.sql` | 13 | 10 | 3 | 77% | 0 | 50 |
| `geo/062_tgeo_posops.in.sql` | 16 | 16 | 0 | 100% | 0 | 76 |
| `geo/062_tpoint_posops.in.sql` | 16 | 16 | 0 | 100% | 0 | 76 |
| `geo/064_tgeo_distance.in.sql` | 4 | 4 | 0 | 100% | 0 | 16 |
| `geo/064_tpoint_distance.in.sql` | 4 | 4 | 0 | 100% | 0 | 21 |
| `geo/066_tpoint_similarity.in.sql` | 5 | 5 | 0 | 100% | 0 | 0 |
| `geo/068_tgeo_aggfuncs.in.sql` | 0 | 0 | 0 | 0% | 9 | 0 |
| `geo/068_tpoint_aggfuncs.in.sql` | 0 | 0 | 0 | 0% | 12 | 0 |
| `geo/070_tgeo_spatialrels.in.sql` | 14 | 11 | 3 | 79% | 0 | 0 |
| `geo/070_tpoint_spatialrels.in.sql` | 12 | 11 | 1 | 92% | 0 | 0 |
| `geo/072_tgeo_tempspatialrels.in.sql` | 6 | 5 | 1 | 83% | 0 | 0 |
| `geo/072_tpoint_tempspatialrels.in.sql` | 5 | 5 | 0 | 100% | 0 | 0 |
| `geo/076_tgeo_analytics.in.sql` | 13 | 13 | 0 | 100% | 0 | 0 |
| `geo/076_tpoint_analytics.in.sql` | 18 | 17 | 1 | 94% | 0 | 0 |
| `geo/078_tpoint_datagen.in.sql` | 1 | 0 | 1 | 0% | 0 | 0 |
| `temporal/001_set.in.sql` | 48 | 35 | 13 | 73% | 34 | 38 |
| `temporal/002_set_ops.in.sql` | 11 | 11 | 0 | 100% | 0 | 176 |
| `temporal/003_span.in.sql` | 46 | 35 | 11 | 76% | 22 | 30 |
| `temporal/005_span_ops.in.sql` | 12 | 12 | 0 | 100% | 0 | 160 |
| `temporal/007_spanset.in.sql` | 61 | 50 | 11 | 82% | 20 | 30 |
| `temporal/009_spanset_ops.in.sql` | 14 | 13 | 1 | 93% | 0 | 280 |
| `temporal/015_span_aggfuncs.in.sql` | 0 | 0 | 0 | 0% | 10 | 0 |
| `temporal/021_tbox.in.sql` | 52 | 52 | 0 | 100% | 8 | 21 |
| `temporal/022_temporal.in.sql` | 102 | 84 | 18 | 82% | 15 | 24 |
| `temporal/023_temporal_inout.in.sql` | 16 | 4 | 12 | 25% | 0 | 0 |
| `temporal/025_temporal_tile.in.sql` | 16 | 10 | 6 | 62% | 0 | 0 |
| `temporal/026_tnumber_mathfuncs.in.sql` | 17 | 17 | 0 | 100% | 0 | 24 |
| `temporal/028_tbool_boolops.in.sql` | 4 | 4 | 0 | 100% | 0 | 7 |
| `temporal/029_ttext_textfuncs.in.sql` | 4 | 4 | 0 | 100% | 0 | 3 |
| `temporal/030_temporal_compops.in.sql` | 18 | 18 | 0 | 100% | 1 | 180 |
| `temporal/032_temporal_boxops.in.sql` | 11 | 11 | 0 | 100% | 0 | 100 |
| `temporal/034_temporal_posops.in.sql` | 8 | 8 | 0 | 100% | 0 | 112 |
| `temporal/036_tnumber_distance.in.sql` | 2 | 2 | 0 | 100% | 0 | 17 |
| `temporal/038_temporal_similarity.in.sql` | 5 | 5 | 0 | 100% | 0 | 0 |
| `temporal/040_temporal_aggfuncs.in.sql` | 0 | 0 | 0 | 0% | 40 | 0 |
| `temporal/042_temporal_waggfuncs.in.sql` | 0 | 0 | 0 | 0% | 8 | 0 |
| `temporal/046_temporal_analytics.in.sql` | 4 | 4 | 0 | 100% | 0 | 0 |
| **TOTAL (active)** | **960** | **811** | **149** | **84%** | **219** | — |

## Missing function names per active section

### `geo/050_geoset.in.sql` — 12 missing of 43 addressable (72% covered)

- `geogsetFromBinary`
- `geogsetFromEWKB`
- `geogsetFromEWKT`
- `geogsetFromHexWKB`
- `geogsetFromText`
- `geomsetFromBinary`
- `geomsetFromEWKB`
- `geomsetFromEWKT`
- `geomsetFromHexWKB`
- `geomsetFromText`
- `transformPipeline` (2 overloads)
- `unnest` (2 overloads)

### `geo/051_stbox.in.sql` — 16 missing of 75 addressable (79% covered)

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
- `stbox_hash`
- `stbox_hash_extended`

### `geo/052_tgeo.in.sql` — 6 missing of 70 addressable (91% covered)

- `temporal_hash` (2 overloads)
- `tgeographySeqSet` (3 overloads)
- `tgeographySeqSetGaps`
- `tgeometrySeqSet` (3 overloads)
- `tgeometrySeqSetGaps`
- `unnest` (2 overloads)

### `geo/052_tpoint.in.sql` — 4 missing of 70 addressable (94% covered)

- `temporal_hash` (2 overloads)
- `tgeogpointSeqSetGaps`
- `tgeompointSeqSetGaps`
- `unnest` (2 overloads)

### `geo/053_tgeo_inout.in.sql` — 12 missing of 18 addressable (33% covered)

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

### `geo/056_tgeo_spatialfuncs.in.sql` — 2 missing of 17 addressable (88% covered)

- `tCentroid`
- `transformPipeline` (2 overloads)

### `geo/056_tpoint_spatialfuncs.in.sql` — 6 missing of 30 addressable (80% covered)

- `atElevation`
- `bearing` (8 overloads)
- `minusElevation`
- `tdirection` (2 overloads)
- `transformPipeline` (3 overloads)
- `transform_gk` (2 overloads)

### `geo/058_tgeo_tile.in.sql` — 3 missing of 5 addressable (40% covered)

- `spaceSplit` (3 overloads)
- `spaceTimeSplit` (3 overloads)
- `timeBoxes`

### `geo/058_tpoint_tile.in.sql` — 3 missing of 11 addressable (73% covered)

- `spaceSplit` (3 overloads)
- `spaceTimeSplit` (3 overloads)
- `timeBoxes`

### `geo/060_tgeo_boxops.in.sql` — 3 missing of 13 addressable (77% covered)

- `splitEachNStboxes` (2 overloads)
- `splitNStboxes` (2 overloads)
- `stboxes` (2 overloads)

### `geo/060_tpoint_boxops.in.sql` — 3 missing of 13 addressable (77% covered)

- `splitEachNStboxes` (4 overloads)
- `splitNStboxes` (4 overloads)
- `stboxes` (4 overloads)

### `geo/070_tgeo_spatialrels.in.sql` — 3 missing of 14 addressable (79% covered)

- `_edisjoint` (6 overloads)
- `aCovers` (3 overloads)
- `eCovers` (3 overloads)

### `geo/070_tpoint_spatialrels.in.sql` — 1 missing of 12 addressable (92% covered)

- `_edisjoint` (6 overloads)

### `geo/072_tgeo_tempspatialrels.in.sql` — 1 missing of 6 addressable (83% covered)

- `tCovers` (3 overloads)

### `geo/076_tpoint_analytics.in.sql` — 1 missing of 18 addressable (94% covered)

- `geography` (2 overloads)

### `geo/078_tpoint_datagen.in.sql` — 1 missing of 1 addressable (0% covered)

- `create_trip`

### `temporal/001_set.in.sql` — 13 missing of 48 addressable (73% covered)

- `bigintsetFromBinary`
- `bigintsetFromHexWKB`
- `datesetFromBinary`
- `datesetFromHexWKB`
- `floatsetFromBinary`
- `floatsetFromHexWKB`
- `intsetFromBinary`
- `intsetFromHexWKB`
- `textsetFromBinary`
- `textsetFromHexWKB`
- `tstzsetFromBinary`
- `tstzsetFromHexWKB`
- `unnest` (6 overloads)

### `temporal/003_span.in.sql` — 11 missing of 46 addressable (76% covered)

- `bigintspanFromBinary`
- `bigintspanFromHexWKB`
- `datespanFromBinary`
- `datespanFromHexWKB`
- `floatspanFromBinary`
- `floatspanFromHexWKB`
- `intspanFromBinary`
- `intspanFromHexWKB`
- `range` (4 overloads)
- `tstzspanFromBinary`
- `tstzspanFromHexWKB`

### `temporal/007_spanset.in.sql` — 11 missing of 61 addressable (82% covered)

- `bigintspansetFromBinary`
- `bigintspansetFromHexWKB`
- `datespansetFromBinary`
- `datespansetFromHexWKB`
- `floatspansetFromBinary`
- `floatspansetFromHexWKB`
- `intspansetFromBinary`
- `intspansetFromHexWKB`
- `multirange` (4 overloads)
- `tstzspansetFromBinary`
- `tstzspansetFromHexWKB`

### `temporal/009_spanset_ops.in.sql` — 1 missing of 14 addressable (93% covered)

- `time_distance` (5 overloads)

### `temporal/022_temporal.in.sql` — 18 missing of 102 addressable (82% covered)

- `tboolInst`
- `tboolSeq` (2 overloads)
- `tboolSeqSet` (2 overloads)
- `tboolSeqSetGaps`
- `temporal_hash` (4 overloads)
- `tfloatInst`
- `tfloatSeq` (2 overloads)
- `tfloatSeqSet` (2 overloads)
- `tfloatSeqSetGaps`
- `tintInst`
- `tintSeq` (2 overloads)
- `tintSeqSet` (2 overloads)
- `tintSeqSetGaps`
- `ttextInst`
- `ttextSeq` (2 overloads)
- `ttextSeqSet` (2 overloads)
- `ttextSeqSetGaps`
- `unnest` (3 overloads)

### `temporal/023_temporal_inout.in.sql` — 12 missing of 16 addressable (25% covered)

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

### `temporal/025_temporal_tile.in.sql` — 6 missing of 16 addressable (62% covered)

- `timeBins` (4 overloads)
- `timeBoxes` (2 overloads)
- `valueBins` (2 overloads)
- `valueBoxes` (2 overloads)
- `valueSplit` (2 overloads)
- `valueTimeBoxes` (2 overloads)

## Appendix B — Out of scope (PG-only, no DuckDB equivalent)

These entries are PG-specific helpers — index opclasses, aggregate transition/combine/final/serialize callbacks, planner hooks (`_sel`, `_joinsel`, `_supportfn`, `_analyze`), text/binary I/O helpers (`_in`, `_out`, `_recv`, `_send`), type modifier helpers, the `999_oid_cache` PG catalog hook, and PG geometric type constructors (`019_geo_constructors`).  None of them have DuckDB equivalents and they should not be implemented; listed here only for completeness.

### Whole sections excluded

| Section | Names |
|---|---:|
| `geo/073_tgeo_gist.in.sql` | 8 |
| `geo/073_tpoint_gist.in.sql` | 3 |
| `geo/074_tgeo_spgist.in.sql` | 9 |
| `temporal/011_span_indexes.in.sql` | 19 |
| `temporal/012_spanset_indexes.in.sql` | 3 |
| `temporal/013_set_indexes.in.sql` | 10 |
| `temporal/019_geo_constructors.in.sql` | 7 |
| `temporal/043_temporal_gist.in.sql` | 14 |
| `temporal/044_temporal_spgist.in.sql` | 10 |
| `temporal/999_oid_cache.in.sql` | 1 |

### PG helpers inside active sections

| Section | PG helpers |
|---|---:|
| `geo/050_geoset.in.sql` | 13 |
| `geo/051_stbox.in.sql` | 8 |
| `geo/052_tgeo.in.sql` | 10 |
| `geo/052_tpoint.in.sql` | 8 |
| `geo/054_tgeo_compops.in.sql` | 1 |
| `geo/068_tgeo_aggfuncs.in.sql` | 9 |
| `geo/068_tpoint_aggfuncs.in.sql` | 12 |
| `temporal/001_set.in.sql` | 34 |
| `temporal/003_span.in.sql` | 22 |
| `temporal/007_spanset.in.sql` | 20 |
| `temporal/015_span_aggfuncs.in.sql` | 10 |
| `temporal/021_tbox.in.sql` | 8 |
| `temporal/022_temporal.in.sql` | 15 |
| `temporal/030_temporal_compops.in.sql` | 1 |
| `temporal/040_temporal_aggfuncs.in.sql` | 40 |
| `temporal/042_temporal_waggfuncs.in.sql` | 8 |

## Appendix C — Deferred families

These families (cbuffer, npoint, pose, rgeo) are deferred until the active temporal + geo surface stabilises. Re-include by editing `DEFERRED_FAMILIES` at the top of `scripts/parity-audit.py`. Listed here so the picture stays complete; not counted in headline coverage.

| Section | Addressable | Covered | Missing | Coverage |
|---|---:|---:|---:|---:|
| `cbuffer/150_cbuffer.in.sql` | 31 | 7 | 24 | 23% |
| `cbuffer/151_cbufferset.in.sql` | 42 | 32 | 10 | 76% |
| `cbuffer/152_tcbuffer.in.sql` | 84 | 65 | 19 | 77% |
| `cbuffer/154_tcbuffer_compops.in.sql` | 6 | 6 | 0 | 100% |
| `cbuffer/155_tcbuffer_spatialfuncs.in.sql` | 11 | 8 | 3 | 73% |
| `cbuffer/158_tcbuffer_topops.in.sql` | 7 | 7 | 0 | 100% |
| `cbuffer/159_tcbuffer_posops.in.sql` | 12 | 12 | 0 | 100% |
| `cbuffer/160_tcbuffer_distance.in.sql` | 5 | 4 | 1 | 80% |
| `cbuffer/161_tcbuffer_aggfuncs.in.sql` | 7 | 0 | 7 | 0% |
| `cbuffer/162_tcbuffer_spatialrels.in.sql` | 13 | 11 | 2 | 85% |
| `cbuffer/164_tcbuffer_tempspatialrels.in.sql` | 6 | 5 | 1 | 83% |
| `cbuffer/166_tcbuffer_indexes.in.sql` | 1 | 0 | 1 | 0% |
| `npoint/081_npoint.in.sql` | 41 | 8 | 33 | 20% |
| `npoint/082_npointset.in.sql` | 43 | 30 | 13 | 70% |
| `npoint/083_tnpoint.in.sql` | 77 | 61 | 16 | 79% |
| `npoint/085_tnpoint_compops.in.sql` | 6 | 6 | 0 | 100% |
| `npoint/087_tnpoint_spatialfuncs.in.sql` | 12 | 11 | 1 | 92% |
| `npoint/089_tnpoint_topops.in.sql` | 7 | 7 | 0 | 100% |
| `npoint/090_tnpoint_posops.in.sql` | 12 | 12 | 0 | 100% |
| `npoint/091_tnpoint_routeops.in.sql` | 4 | 0 | 4 | 0% |
| `npoint/092_tnpoint_gin.in.sql` | 3 | 0 | 3 | 0% |
| `npoint/093_tnpoint_distance.in.sql` | 4 | 4 | 0 | 100% |
| `npoint/095_tnpoint_aggfuncs.in.sql` | 8 | 0 | 8 | 0% |
| `npoint/098_tnpoint_indexes.in.sql` | 1 | 0 | 1 | 0% |
| `pose/100_pose.in.sql` | 34 | 10 | 24 | 29% |
| `pose/101_poseset.in.sql` | 46 | 33 | 13 | 72% |
| `pose/102_tpose.in.sql` | 85 | 64 | 21 | 75% |
| `pose/104_tpose_compops.in.sql` | 6 | 6 | 0 | 100% |
| `pose/105_tpose_spatialfuncs.in.sql` | 8 | 7 | 1 | 88% |
| `pose/108_tpose_topops.in.sql` | 7 | 7 | 0 | 100% |
| `pose/109_tpose_posops.in.sql` | 16 | 16 | 0 | 100% |
| `pose/111_tpose_aggfuncs.in.sql` | 7 | 0 | 7 | 0% |
| `pose/113_tpose_distance.in.sql` | 4 | 4 | 0 | 100% |
| `pose/114_tpose_indexes.in.sql` | 1 | 0 | 1 | 0% |
| `rgeo/122_trgeo.in.sql` | 95 | 75 | 20 | 79% |
| `rgeo/124_trgeo_compops.in.sql` | 6 | 6 | 0 | 100% |
| `rgeo/125_trgeo_spatialfuncs.in.sql` | 8 | 7 | 1 | 88% |
| `rgeo/126_trgeo_tile.in.sql` | 3 | 3 | 0 | 100% |
| `rgeo/127_trgeo_boxops.in.sql` | 13 | 8 | 5 | 62% |
| `rgeo/128_trgeo_topops.in.sql` | 5 | 5 | 0 | 100% |
| `rgeo/129_trgeo_posops.in.sql` | 16 | 16 | 0 | 100% |
| `rgeo/131_trgeo_aggfuncs.in.sql` | 8 | 0 | 8 | 0% |
| `rgeo/132_trgeo_similarity.in.sql` | 5 | 5 | 0 | 100% |
| `rgeo/133_trgeo_distance.in.sql` | 4 | 4 | 0 | 100% |
| `rgeo/133_trgeo_vclip.in.sql` | 6 | 0 | 6 | 0% |
| `rgeo/134_trgeo_indexes.in.sql` | 1 | 0 | 1 | 0% |
| **TOTAL (deferred)** | **827** | **572** | **255** | **69%** |

