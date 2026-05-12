# MobilityDuck parity status — surface-level audit

Generated 2026-05-11. **Active addressable scope** (temporal + geo, excluding PG-only helpers): 929/943 names covered (98.5%).

**Out of scope** (PG-only — no DuckDB equivalent exists): 315 names skipped — 84 from PG-only sections (GiST/SPGiST opclasses, set/span/spanset index files, `019_geo_constructors.in.sql` PG geometric types, `999_oid_cache.in.sql`) plus 231 PG helper functions inside active sections (`*_in/_out/_recv/_send`, `*_transfn/_combinefn/_finalfn/_serialize/_deserialize`, `*_sel/_joinsel/_supportfn/_analyze`, `*_typmod_in/_typmod_out`).  Listed in appendix B; not counted in the headline.

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
| `geo/050_geoset.in.sql` | 42 | 41 | 1 | 98% | 13 | 46 |
| `geo/051_stbox.in.sql` | 73 | 70 | 3 | 96% | 10 | 29 |
| `geo/052_tgeo.in.sql` | 68 | 68 | 0 | 100% | 11 | 12 |
| `geo/052_tpoint.in.sql` | 69 | 69 | 0 | 100% | 9 | 12 |
| `geo/053_tgeo_inout.in.sql` | 18 | 18 | 0 | 100% | 0 | 0 |
| `geo/053_tpoint_inout.in.sql` | 18 | 18 | 0 | 100% | 0 | 0 |
| `geo/054_tgeo_compops.in.sql` | 6 | 6 | 0 | 100% | 1 | 36 |
| `geo/054_tpoint_compops.in.sql` | 6 | 6 | 0 | 100% | 0 | 36 |
| `geo/056_tgeo_spatialfuncs.in.sql` | 16 | 15 | 1 | 94% | 0 | 0 |
| `geo/056_tpoint_spatialfuncs.in.sql` | 28 | 27 | 1 | 96% | 1 | 0 |
| `geo/058_tgeo_tile.in.sql` | 5 | 4 | 1 | 80% | 0 | 0 |
| `geo/058_tpoint_tile.in.sql` | 11 | 10 | 1 | 91% | 0 | 0 |
| `geo/060_tgeo_boxops.in.sql` | 13 | 13 | 0 | 100% | 0 | 50 |
| `geo/060_tpoint_boxops.in.sql` | 13 | 13 | 0 | 100% | 0 | 50 |
| `geo/062_tgeo_posops.in.sql` | 16 | 16 | 0 | 100% | 0 | 76 |
| `geo/062_tpoint_posops.in.sql` | 16 | 16 | 0 | 100% | 0 | 76 |
| `geo/064_tgeo_distance.in.sql` | 4 | 4 | 0 | 100% | 0 | 16 |
| `geo/064_tpoint_distance.in.sql` | 4 | 4 | 0 | 100% | 0 | 21 |
| `geo/066_tpoint_similarity.in.sql` | 5 | 5 | 0 | 100% | 0 | 0 |
| `geo/068_tgeo_aggfuncs.in.sql` | 0 | 0 | 0 | 0% | 9 | 0 |
| `geo/068_tpoint_aggfuncs.in.sql` | 0 | 0 | 0 | 0% | 12 | 0 |
| `geo/070_tgeo_spatialrels.in.sql` | 13 | 13 | 0 | 100% | 1 | 0 |
| `geo/070_tpoint_spatialrels.in.sql` | 11 | 11 | 0 | 100% | 1 | 0 |
| `geo/072_tgeo_tempspatialrels.in.sql` | 6 | 6 | 0 | 100% | 0 | 0 |
| `geo/072_tpoint_tempspatialrels.in.sql` | 5 | 5 | 0 | 100% | 0 | 0 |
| `geo/076_tgeo_analytics.in.sql` | 12 | 12 | 0 | 100% | 0 | 0 |
| `geo/076_tpoint_analytics.in.sql` | 18 | 17 | 1 | 94% | 0 | 0 |
| `geo/078_tpoint_datagen.in.sql` | 0 | 0 | 0 | 0% | 1 | 0 |
| `temporal/001_set.in.sql` | 47 | 47 | 0 | 100% | 35 | 38 |
| `temporal/002_set_ops.in.sql` | 11 | 11 | 0 | 100% | 0 | 176 |
| `temporal/003_span.in.sql` | 45 | 45 | 0 | 100% | 23 | 30 |
| `temporal/005_span_ops.in.sql` | 12 | 12 | 0 | 100% | 0 | 160 |
| `temporal/007_spanset.in.sql` | 60 | 60 | 0 | 100% | 21 | 30 |
| `temporal/009_spanset_ops.in.sql` | 14 | 14 | 0 | 100% | 0 | 280 |
| `temporal/015_span_aggfuncs.in.sql` | 0 | 0 | 0 | 0% | 10 | 0 |
| `temporal/021_tbox.in.sql` | 52 | 52 | 0 | 100% | 8 | 21 |
| `temporal/022_temporal.in.sql` | 101 | 101 | 0 | 100% | 16 | 24 |
| `temporal/023_temporal_inout.in.sql` | 16 | 16 | 0 | 100% | 0 | 0 |
| `temporal/025_temporal_tile.in.sql` | 16 | 11 | 5 | 69% | 0 | 0 |
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
| **TOTAL (active)** | **943** | **929** | **14** | **99%** | **231** | — |

## Missing function names per active section

### `geo/050_geoset.in.sql` — 1 missing of 42 addressable (98% covered)

- `transformPipeline` (2 overloads)

### `geo/051_stbox.in.sql` — 3 missing of 73 addressable (96% covered)

- `geography`
- `perimeter`
- `quadSplit`

### `geo/056_tgeo_spatialfuncs.in.sql` — 1 missing of 16 addressable (94% covered)

- `transformPipeline` (2 overloads)

### `geo/056_tpoint_spatialfuncs.in.sql` — 1 missing of 28 addressable (96% covered)

- `transformPipeline` (3 overloads)

### `geo/058_tgeo_tile.in.sql` — 1 missing of 5 addressable (80% covered)

- `timeBoxes`

### `geo/058_tpoint_tile.in.sql` — 1 missing of 11 addressable (91% covered)

- `timeBoxes`

### `geo/076_tpoint_analytics.in.sql` — 1 missing of 18 addressable (94% covered)

- `geography` (2 overloads)

### `temporal/025_temporal_tile.in.sql` — 5 missing of 16 addressable (69% covered)

- `timeBins` (4 overloads)
- `timeBoxes` (2 overloads)
- `valueBins` (2 overloads)
- `valueBoxes` (2 overloads)
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
| `geo/051_stbox.in.sql` | 10 |
| `geo/052_tgeo.in.sql` | 11 |
| `geo/052_tpoint.in.sql` | 9 |
| `geo/054_tgeo_compops.in.sql` | 1 |
| `geo/056_tpoint_spatialfuncs.in.sql` | 1 |
| `geo/068_tgeo_aggfuncs.in.sql` | 9 |
| `geo/068_tpoint_aggfuncs.in.sql` | 12 |
| `geo/070_tgeo_spatialrels.in.sql` | 1 |
| `geo/070_tpoint_spatialrels.in.sql` | 1 |
| `geo/078_tpoint_datagen.in.sql` | 1 |
| `temporal/001_set.in.sql` | 35 |
| `temporal/003_span.in.sql` | 23 |
| `temporal/007_spanset.in.sql` | 21 |
| `temporal/015_span_aggfuncs.in.sql` | 10 |
| `temporal/021_tbox.in.sql` | 8 |
| `temporal/022_temporal.in.sql` | 16 |
| `temporal/030_temporal_compops.in.sql` | 1 |
| `temporal/040_temporal_aggfuncs.in.sql` | 40 |
| `temporal/042_temporal_waggfuncs.in.sql` | 8 |

## Appendix C — Deferred families

These families (cbuffer, npoint, pose, rgeo) are deferred until the active temporal + geo surface stabilises. Re-include by editing `DEFERRED_FAMILIES` at the top of `scripts/parity-audit.py`. Listed here so the picture stays complete; not counted in headline coverage.

| Section | Addressable | Covered | Missing | Coverage |
|---|---:|---:|---:|---:|
| `cbuffer/150_cbuffer.in.sql` | 31 | 7 | 24 | 23% |
| `cbuffer/151_cbufferset.in.sql` | 42 | 32 | 10 | 76% |
| `cbuffer/152_tcbuffer.in.sql` | 84 | 66 | 18 | 79% |
| `cbuffer/154_tcbuffer_compops.in.sql` | 6 | 6 | 0 | 100% |
| `cbuffer/155_tcbuffer_spatialfuncs.in.sql` | 9 | 6 | 3 | 67% |
| `cbuffer/158_tcbuffer_topops.in.sql` | 7 | 7 | 0 | 100% |
| `cbuffer/159_tcbuffer_posops.in.sql` | 12 | 12 | 0 | 100% |
| `cbuffer/160_tcbuffer_distance.in.sql` | 5 | 4 | 1 | 80% |
| `cbuffer/161_tcbuffer_aggfuncs.in.sql` | 7 | 0 | 7 | 0% |
| `cbuffer/162_tcbuffer_spatialrels.in.sql` | 13 | 13 | 0 | 100% |
| `cbuffer/164_tcbuffer_tempspatialrels.in.sql` | 6 | 6 | 0 | 100% |
| `cbuffer/166_tcbuffer_indexes.in.sql` | 1 | 0 | 1 | 0% |
| `npoint/081_npoint.in.sql` | 41 | 8 | 33 | 20% |
| `npoint/082_npointset.in.sql` | 43 | 30 | 13 | 70% |
| `npoint/083_tnpoint.in.sql` | 77 | 62 | 15 | 81% |
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
| `pose/102_tpose.in.sql` | 84 | 65 | 19 | 77% |
| `pose/104_tpose_compops.in.sql` | 6 | 6 | 0 | 100% |
| `pose/105_tpose_spatialfuncs.in.sql` | 8 | 7 | 1 | 88% |
| `pose/108_tpose_topops.in.sql` | 7 | 7 | 0 | 100% |
| `pose/109_tpose_posops.in.sql` | 16 | 16 | 0 | 100% |
| `pose/111_tpose_aggfuncs.in.sql` | 7 | 0 | 7 | 0% |
| `pose/113_tpose_distance.in.sql` | 4 | 4 | 0 | 100% |
| `pose/114_tpose_indexes.in.sql` | 1 | 0 | 1 | 0% |
| `rgeo/122_trgeo.in.sql` | 83 | 65 | 18 | 78% |
| `rgeo/124_trgeo_compops.in.sql` | 6 | 6 | 0 | 100% |
| `rgeo/125_trgeo_spatialfuncs.in.sql` | 4 | 3 | 1 | 75% |
| `rgeo/128_trgeo_topops.in.sql` | 5 | 5 | 0 | 100% |
| `rgeo/129_trgeo_posops.in.sql` | 12 | 12 | 0 | 100% |
| `rgeo/131_trgeo_aggfuncs.in.sql` | 7 | 0 | 7 | 0% |
| `rgeo/133_trgeo_distance.in.sql` | 4 | 4 | 0 | 100% |
| `rgeo/133_trgeo_vclip.in.sql` | 6 | 0 | 6 | 0% |
| `rgeo/134_trgeo_indexes.in.sql` | 1 | 0 | 1 | 0% |
| **TOTAL (deferred)** | **782** | **542** | **240** | **69%** |

