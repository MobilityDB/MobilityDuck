# MobilityDuck parity status — surface-level audit

Generated 2026-08-26. **Addressable scope** (every family, excluding PG-only helpers): 1904/3121 names covered (61.0%).

Of the 1217 not covered, **269** name only value types MobilityDuck registers and are missing a registration of their own, and **948** are blocked on a value type the binding does not register at all — a type family it does not carry yet. The blocking types, and the sections behind them, are in appendix C.

**Out of scope** (PG-only — no DuckDB equivalent exists): 542 names skipped — 96 from PG-only sections (GiST/SPGiST opclasses, set/span/spanset index files, `019_geo_constructors.in.sql` PG geometric types, `999_oid_cache.in.sql`) plus 446 PG helper functions inside active sections (`*_in/_out/_recv/_send`, `*_transfn/_combinefn/_finalfn/_serialize/_deserialize`, `*_sel/_joinsel/_supportfn/_analyze`, `*_typmod_in/_typmod_out`).  Listed in appendix B; not counted in the headline.

**Methodology**: parsed `CREATE FUNCTION` and `CREATE TYPE` from `mobilitydb/sql/**/*.in.sql`, and `RegisterFunction(ScalarFunction("name",...))` plus `RegisterType("name", …)` (with the aggregate and table-function variants) from `MobilityDuck/src/**/*.cpp`.  An entry counts as covered when the binding registers its name AND registers every value type its signature names.  `CREATE TYPE x AS (…)` composites are the row shapes a set-returning function yields, which DuckDB expresses as a STRUCT rather than a registered type, so they do not gate coverage.  Both type sets are read from the two trees, so neither is a hand-kept list that can go stale.

**Caveats**:
- Matching stays name-level within a registered type set: a registered `before` is treated as covering `before(tstzspan, temporal)` as well as `before(temporal, temporal)`.  A per-overload audit is needed for the full picture.
- DuckDB rejects multi-character operator tokens (`<<#`, `|>>`, `<#>`, `|=|`, `~=`); equivalent named functions are registered. See `docs/DuckDB-Parity-Gaps.md` for the catalogue.

Regenerate with `python3 scripts/parity-audit.py --mdb ../MobilityDB --mduck . --out docs/parity-status.md`. The OUT_OF_SCOPE_SECTIONS / OUT_OF_SCOPE_NAME_SUFFIXES sets at the top of that script control bucketing.

## Coverage summary (addressable surface)

Per-section counts: `Addressable` = MDB names minus PG-only helpers (see appendix B).  `Blocked` counts the entries naming a value type the binding does not register (appendix C).  PG-only helper count shown in `OOS` column for transparency.

| Section | Addressable | Covered | Missing | Blocked | Coverage | OOS | MDB operators |
|---|---:|---:|---:|---:|---:|---:|---:|
| `cbuffer/200_cbuffer.in.sql` | 36 | 21 | 15 | 0 | 58% | 4 | 7 |
| `cbuffer/201_cbufferset.in.sql` | 38 | 0 | 0 | 38 | 0% | 6 | 23 |
| `cbuffer/202_tcbuffer.in.sql` | 86 | 71 | 12 | 3 | 83% | 5 | 6 |
| `cbuffer/204_tcbuffer_compops.in.sql` | 6 | 6 | 0 | 0 | 100% | 0 | 18 |
| `cbuffer/205_tcbuffer_spatialfuncs.in.sql` | 13 | 12 | 1 | 0 | 92% | 0 | 0 |
| `cbuffer/207_tcbuffer_boxops.in.sql` | 3 | 3 | 0 | 0 | 100% | 0 | 0 |
| `cbuffer/208_tcbuffer_topops.in.sql` | 10 | 10 | 0 | 0 | 100% | 0 | 25 |
| `cbuffer/209_tcbuffer_posops.in.sql` | 12 | 12 | 0 | 0 | 100% | 0 | 44 |
| `cbuffer/210_tcbuffer_distance.in.sql` | 6 | 5 | 1 | 0 | 83% | 1 | 17 |
| `cbuffer/211_tcbuffer_aggfuncs.in.sql` | 0 | 0 | 0 | 0 | 0% | 7 | 0 |
| `cbuffer/212_tcbuffer_spatialrels.in.sql` | 12 | 12 | 0 | 0 | 100% | 0 | 0 |
| `cbuffer/214_tcbuffer_tempspatialrels.in.sql` | 6 | 6 | 0 | 0 | 100% | 0 | 0 |
| `cbuffer/216_tcbuffer_indexes.in.sql` | 2 | 0 | 2 | 0 | 0% | 0 | 0 |
| `cbuffer/217_tcbuffer_analytics.in.sql` | 4 | 4 | 0 | 0 | 100% | 0 | 0 |
| `geo/049_geo_funcs.in.sql` | 5 | 2 | 3 | 0 | 40% | 0 | 0 |
| `geo/050_geoset.in.sql` | 45 | 32 | 13 | 0 | 71% | 12 | 46 |
| `geo/051_stbox.in.sql` | 75 | 61 | 14 | 0 | 81% | 8 | 29 |
| `geo/052_tgeo.in.sql` | 71 | 66 | 5 | 0 | 93% | 10 | 12 |
| `geo/052_tpoint.in.sql` | 72 | 69 | 3 | 0 | 96% | 8 | 12 |
| `geo/053_tgeo_inout.in.sql` | 19 | 19 | 0 | 0 | 100% | 0 | 0 |
| `geo/053_tpoint_inout.in.sql` | 19 | 19 | 0 | 0 | 100% | 0 | 0 |
| `geo/054_tgeo_compops.in.sql` | 6 | 6 | 0 | 0 | 100% | 1 | 36 |
| `geo/054_tpoint_compops.in.sql` | 6 | 6 | 0 | 0 | 100% | 0 | 36 |
| `geo/056_tgeo_spatialfuncs.in.sql` | 16 | 15 | 1 | 0 | 94% | 0 | 0 |
| `geo/056_tpoint_spatialfuncs.in.sql` | 33 | 25 | 8 | 0 | 76% | 0 | 0 |
| `geo/058_tgeo_tile.in.sql` | 5 | 2 | 3 | 0 | 40% | 0 | 0 |
| `geo/058_tpoint_tile.in.sql` | 11 | 8 | 3 | 0 | 73% | 0 | 0 |
| `geo/060_tgeo_boxops.in.sql` | 8 | 8 | 0 | 0 | 100% | 0 | 0 |
| `geo/060_tpoint_boxops.in.sql` | 8 | 8 | 0 | 0 | 100% | 0 | 0 |
| `geo/061_tgeo_topops.in.sql` | 5 | 5 | 0 | 0 | 100% | 0 | 50 |
| `geo/061_tpoint_topops.in.sql` | 5 | 5 | 0 | 0 | 100% | 0 | 50 |
| `geo/062_tgeo_posops.in.sql` | 16 | 16 | 0 | 0 | 100% | 0 | 76 |
| `geo/062_tpoint_posops.in.sql` | 16 | 16 | 0 | 0 | 100% | 0 | 76 |
| `geo/064_tgeo_distance.in.sql` | 4 | 4 | 0 | 0 | 100% | 0 | 16 |
| `geo/064_tpoint_distance.in.sql` | 4 | 4 | 0 | 0 | 100% | 0 | 21 |
| `geo/066_tpoint_similarity.in.sql` | 7 | 7 | 0 | 0 | 100% | 0 | 0 |
| `geo/068_tgeo_aggfuncs.in.sql` | 0 | 0 | 0 | 0 | 0% | 9 | 0 |
| `geo/068_tpoint_aggfuncs.in.sql` | 0 | 0 | 0 | 0 | 0% | 12 | 0 |
| `geo/070_tgeo_spatialrels.in.sql` | 12 | 12 | 0 | 0 | 100% | 0 | 0 |
| `geo/070_tpoint_spatialrels.in.sql` | 12 | 12 | 0 | 0 | 100% | 0 | 0 |
| `geo/071_tgeo_setset.in.sql` | 13 | 13 | 0 | 0 | 100% | 1 | 0 |
| `geo/071_tpoint_setset.in.sql` | 10 | 10 | 0 | 0 | 100% | 1 | 0 |
| `geo/072_tgeo_tempspatialrels.in.sql` | 6 | 6 | 0 | 0 | 100% | 0 | 0 |
| `geo/072_tpoint_tempspatialrels.in.sql` | 6 | 6 | 0 | 0 | 100% | 0 | 0 |
| `geo/076_tgeo_analytics.in.sql` | 12 | 12 | 0 | 0 | 100% | 0 | 0 |
| `geo/076_tpoint_analytics.in.sql` | 19 | 17 | 2 | 0 | 89% | 0 | 0 |
| `geo/078_tpoint_datagen.in.sql` | 1 | 0 | 1 | 0 | 0% | 0 | 0 |
| `h3/250_h3index.in.sql` | 15 | 6 | 9 | 0 | 40% | 4 | 6 |
| `h3/251_h3indexset.in.sql` | 27 | 24 | 3 | 0 | 89% | 6 | 20 |
| `h3/252_h3index_ops.in.sql` | 11 | 3 | 8 | 0 | 27% | 0 | 0 |
| `h3/253_th3index.in.sql` | 76 | 68 | 8 | 0 | 89% | 4 | 6 |
| `h3/254_th3index_compops.in.sql` | 6 | 6 | 0 | 0 | 100% | 0 | 18 |
| `h3/255_th3index_spatialfuncs.in.sql` | 5 | 5 | 0 | 0 | 100% | 0 | 2 |
| `h3/257_th3index_boxops.in.sql` | 3 | 3 | 0 | 0 | 100% | 0 | 0 |
| `h3/258_th3index_topops.in.sql` | 7 | 7 | 0 | 0 | 100% | 0 | 25 |
| `h3/259_th3index_posops.in.sql` | 12 | 12 | 0 | 0 | 100% | 0 | 44 |
| `h3/261_th3index_aggfuncs.in.sql` | 0 | 0 | 0 | 0 | 0% | 8 | 0 |
| `h3/262_th3index_spatialrels.in.sql` | 12 | 12 | 0 | 0 | 100% | 0 | 0 |
| `h3/264_th3index_tempspatialrels.in.sql` | 6 | 6 | 0 | 0 | 100% | 0 | 0 |
| `h3/272_th3index_gist.in.sql` | 1 | 0 | 1 | 0 | 0% | 0 | 0 |
| `h3/280_th3index_inspection.in.sql` | 5 | 5 | 0 | 0 | 100% | 0 | 0 |
| `h3/281_th3index_hierarchy.in.sql` | 4 | 4 | 0 | 0 | 100% | 0 | 0 |
| `h3/283_th3index_edges.in.sql` | 6 | 6 | 0 | 0 | 100% | 0 | 0 |
| `h3/284_th3index_vertices.in.sql` | 3 | 3 | 0 | 0 | 100% | 0 | 0 |
| `h3/285_th3index_traversal.in.sql` | 3 | 3 | 0 | 0 | 100% | 0 | 1 |
| `h3/286_th3index_metrics.in.sql` | 3 | 0 | 3 | 0 | 0% | 0 | 0 |
| `json/450_jsonbset.in.sql` | 30 | 0 | 0 | 30 | 0% | 6 | 20 |
| `json/451_jsonbset_jsonfuncs.in.sql` | 40 | 0 | 0 | 40 | 0% | 0 | 17 |
| `json/452_tjsonb.in.sql` | 76 | 70 | 3 | 3 | 92% | 4 | 6 |
| `json/454_tjsonb_jsonfuncs.in.sql` | 46 | 11 | 35 | 0 | 24% | 0 | 18 |
| `json/456_tjsonb_op.in.sql` | 5 | 2 | 3 | 0 | 40% | 0 | 9 |
| `json/458_tjsonb_compops.in.sql` | 6 | 6 | 0 | 0 | 100% | 0 | 18 |
| `json/459_tjsonb_boxops.in.sql` | 3 | 3 | 0 | 0 | 100% | 0 | 0 |
| `json/460_tjsonb_topops.in.sql` | 5 | 5 | 0 | 0 | 100% | 0 | 15 |
| `json/462_tjsonb_posops.in.sql` | 4 | 4 | 0 | 0 | 100% | 0 | 12 |
| `json/464_tjsonb_aggfuncs.in.sql` | 0 | 0 | 0 | 0 | 0% | 8 | 0 |
| `json/466_tjsonb_indexes.in.sql` | 2 | 0 | 2 | 0 | 0% | 0 | 0 |
| `npoint/300_npoint.in.sql` | 28 | 20 | 8 | 0 | 71% | 8 | 12 |
| `npoint/301_npointset.in.sql` | 38 | 0 | 0 | 38 | 0% | 6 | 23 |
| `npoint/302_tnpoint.in.sql` | 80 | 67 | 10 | 3 | 84% | 4 | 6 |
| `npoint/304_tnpoint_compops.in.sql` | 6 | 6 | 0 | 0 | 100% | 0 | 18 |
| `npoint/306_tnpoint_spatialfuncs.in.sql` | 12 | 12 | 0 | 0 | 100% | 0 | 0 |
| `npoint/307_tnpoint_boxops.in.sql` | 3 | 3 | 0 | 0 | 100% | 0 | 0 |
| `npoint/308_tnpoint_topops.in.sql` | 7 | 7 | 0 | 0 | 100% | 0 | 25 |
| `npoint/309_tnpoint_posops.in.sql` | 12 | 12 | 0 | 0 | 100% | 0 | 44 |
| `npoint/310_tnpoint_routeops.in.sql` | 4 | 0 | 4 | 0 | 0% | 0 | 20 |
| `npoint/311_tnpoint_gin.in.sql` | 3 | 0 | 3 | 0 | 0% | 0 | 0 |
| `npoint/312_tnpoint_distance.in.sql` | 3 | 3 | 0 | 0 | 100% | 0 | 12 |
| `npoint/314_tnpoint_aggfuncs.in.sql` | 0 | 0 | 0 | 0 | 0% | 9 | 0 |
| `npoint/316_tnpoint_indexes.in.sql` | 2 | 0 | 2 | 0 | 0% | 0 | 0 |
| `npoint/320_tnpoint_spatialrels.in.sql` | 12 | 12 | 0 | 0 | 100% | 0 | 0 |
| `npoint/322_tnpoint_tempspatialrels.in.sql` | 6 | 6 | 0 | 0 | 100% | 0 | 0 |
| `pointcloud/399_pointcloud_schemas.in.sql` | 3 | 0 | 3 | 0 | 0% | 0 | 0 |
| `pointcloud/400_pcset.in.sql` | 38 | 6 | 1 | 31 | 16% | 11 | 52 |
| `pointcloud/410_tpcbox.in.sql` | 53 | 0 | 0 | 53 | 0% | 4 | 29 |
| `pointcloud/420_tpcpoint.in.sql` | 80 | 71 | 4 | 5 | 89% | 6 | 6 |
| `pointcloud/430_tpcpatch.in.sql` | 84 | 71 | 6 | 7 | 85% | 4 | 6 |
| `pointcloud/433_tpcpoint_compops.in.sql` | 6 | 6 | 0 | 0 | 100% | 0 | 18 |
| `pointcloud/434_tpcpatch_compops.in.sql` | 6 | 6 | 0 | 0 | 100% | 0 | 18 |
| `pointcloud/435_tpc_boxops.in.sql` | 3 | 3 | 0 | 0 | 100% | 0 | 0 |
| `pointcloud/436_tpc_topops.in.sql` | 5 | 0 | 0 | 5 | 0% | 0 | 50 |
| `pointcloud/437_tpc_posops.in.sql` | 16 | 0 | 0 | 16 | 0% | 0 | 112 |
| `pointcloud/438_tpc_distance.in.sql` | 2 | 0 | 0 | 2 | 0% | 0 | 7 |
| `pointcloud/439_tpc_gist.in.sql` | 7 | 0 | 4 | 3 | 0% | 0 | 0 |
| `pointcloud/440_tpc_spgist.in.sql` | 2 | 0 | 2 | 0 | 0% | 0 | 0 |
| `pointcloud/441_tpc_aggfuncs.in.sql` | 0 | 0 | 0 | 0 | 0% | 12 | 0 |
| `pointcloud/442_tpcpoint_spatialfuncs.in.sql` | 3 | 3 | 0 | 0 | 100% | 0 | 0 |
| `pointcloud/443_tpcpoint_spatialrels.in.sql` | 12 | 12 | 0 | 0 | 100% | 0 | 0 |
| `pointcloud/444_tpcpoint_tempspatialrels.in.sql` | 6 | 6 | 0 | 0 | 100% | 0 | 0 |
| `pointcloud/445_tpcpoint_analytics.in.sql` | 4 | 4 | 0 | 0 | 100% | 0 | 0 |
| `pointcloud/446_tpcpoint_tile.in.sql` | 5 | 2 | 3 | 0 | 40% | 0 | 0 |
| `pointcloud/447_tpcpoint_distance.in.sql` | 4 | 4 | 0 | 0 | 100% | 0 | 4 |
| `pointcloud/448_tpcpatch_tempspatialrels.in.sql` | 6 | 6 | 0 | 0 | 100% | 0 | 0 |
| `pointcloud/449_tpcpatch_spatialrels.in.sql` | 12 | 12 | 0 | 0 | 100% | 0 | 0 |
| `pose/100_pose.in.sql` | 39 | 0 | 0 | 39 | 0% | 4 | 7 |
| `pose/101_poseset.in.sql` | 41 | 0 | 0 | 41 | 0% | 6 | 23 |
| `pose/102_tpose.in.sql` | 96 | 0 | 0 | 96 | 0% | 5 | 6 |
| `pose/104_tpose_compops.in.sql` | 6 | 0 | 0 | 6 | 0% | 0 | 18 |
| `pose/105_tpose_spatialfuncs.in.sql` | 13 | 0 | 0 | 13 | 0% | 0 | 0 |
| `pose/107_tpose_boxops.in.sql` | 3 | 0 | 0 | 3 | 0% | 0 | 0 |
| `pose/108_tpose_topops.in.sql` | 10 | 0 | 0 | 10 | 0% | 0 | 25 |
| `pose/109_tpose_posops.in.sql` | 16 | 0 | 0 | 16 | 0% | 0 | 56 |
| `pose/110_tpose_distance.in.sql` | 5 | 0 | 0 | 5 | 0% | 0 | 17 |
| `pose/111_tpose_aggfuncs.in.sql` | 0 | 0 | 0 | 0 | 0% | 8 | 0 |
| `pose/112_tpose_spatialrels.in.sql` | 12 | 0 | 0 | 12 | 0% | 0 | 0 |
| `pose/114_tpose_tempspatialrels.in.sql` | 6 | 0 | 0 | 6 | 0% | 0 | 0 |
| `pose/116_tpose_indexes.in.sql` | 2 | 0 | 0 | 2 | 0% | 0 | 0 |
| `pose/117_tpose_analytics.in.sql` | 4 | 0 | 0 | 4 | 0% | 0 | 0 |
| `pose/119_tpose_tile.in.sql` | 5 | 0 | 0 | 5 | 0% | 0 | 0 |
| `pose/120_geopose_frames.in.sql` | 4 | 0 | 4 | 0 | 0% | 0 | 0 |
| `posechain/550_posechain.in.sql` | 35 | 0 | 0 | 35 | 0% | 4 | 7 |
| `posechain/551_posechainset.in.sql` | 40 | 0 | 0 | 40 | 0% | 6 | 20 |
| `posechain/552_tposechain.in.sql` | 87 | 0 | 0 | 87 | 0% | 5 | 6 |
| `posechain/553_tposechain_spatialfuncs.in.sql` | 4 | 0 | 0 | 4 | 0% | 0 | 0 |
| `posechain/554_tposechain_compops.in.sql` | 6 | 0 | 0 | 6 | 0% | 0 | 18 |
| `posechain/558_tposechain_topops.in.sql` | 10 | 0 | 0 | 10 | 0% | 0 | 25 |
| `posechain/559_tposechain_posops.in.sql` | 16 | 0 | 0 | 16 | 0% | 0 | 56 |
| `posechain/561_tposechain_aggfuncs.in.sql` | 0 | 0 | 0 | 0 | 0% | 8 | 0 |
| `posechain/564_tposechain_tempspatialrels.in.sql` | 6 | 0 | 0 | 6 | 0% | 0 | 0 |
| `quadbin/350_quadbin.in.sql` | 11 | 10 | 1 | 0 | 91% | 4 | 6 |
| `quadbin/351_quadbinset.in.sql` | 28 | 0 | 0 | 28 | 0% | 6 | 20 |
| `quadbin/352_quadbin_ops.in.sql` | 12 | 2 | 8 | 2 | 17% | 0 | 0 |
| `quadbin/353_tquadbin.in.sql` | 76 | 65 | 8 | 3 | 86% | 4 | 6 |
| `quadbin/354_tquadbin_compops.in.sql` | 6 | 6 | 0 | 0 | 100% | 0 | 18 |
| `quadbin/355_tquadbin_spatialfuncs.in.sql` | 7 | 2 | 5 | 0 | 29% | 0 | 0 |
| `quadbin/357_tquadbin_boxops.in.sql` | 3 | 3 | 0 | 0 | 100% | 0 | 0 |
| `quadbin/358_tquadbin_topops.in.sql` | 7 | 7 | 0 | 0 | 100% | 0 | 25 |
| `quadbin/359_tquadbin_posops.in.sql` | 12 | 12 | 0 | 0 | 100% | 0 | 44 |
| `quadbin/361_tquadbin_aggfuncs.in.sql` | 0 | 0 | 0 | 0 | 0% | 8 | 0 |
| `quadbin/362_tquadbin_spatialrels.in.sql` | 12 | 12 | 0 | 0 | 100% | 0 | 0 |
| `quadbin/364_tquadbin_tempspatialrels.in.sql` | 6 | 6 | 0 | 0 | 100% | 0 | 0 |
| `quadbin/372_tquadbin_gist.in.sql` | 1 | 0 | 1 | 0 | 0% | 0 | 0 |
| `raster/500_raster.in.sql` | 31 | 29 | 2 | 0 | 94% | 4 | 6 |
| `rgeo/150_trgeo.in.sql` | 88 | 0 | 0 | 88 | 0% | 5 | 6 |
| `rgeo/154_trgeo_compops.in.sql` | 6 | 0 | 0 | 6 | 0% | 0 | 18 |
| `rgeo/156_trgeo_spatialfuncs.in.sql` | 19 | 0 | 0 | 19 | 0% | 0 | 0 |
| `rgeo/158_trgeo_tile.in.sql` | 3 | 0 | 0 | 3 | 0% | 0 | 0 |
| `rgeo/160_trgeo_boxops.in.sql` | 6 | 0 | 0 | 6 | 0% | 0 | 0 |
| `rgeo/161_trgeo_topops.in.sql` | 5 | 0 | 0 | 5 | 0% | 0 | 25 |
| `rgeo/162_trgeo_posops.in.sql` | 16 | 0 | 0 | 16 | 0% | 0 | 56 |
| `rgeo/164_trgeo_distance.in.sql` | 4 | 0 | 0 | 4 | 0% | 0 | 12 |
| `rgeo/166_trgeo_similarity.in.sql` | 5 | 0 | 0 | 5 | 0% | 0 | 0 |
| `rgeo/168_trgeo_aggfuncs.in.sql` | 0 | 0 | 0 | 0 | 0% | 8 | 0 |
| `rgeo/170_trgeo_spatialrels.in.sql` | 12 | 0 | 0 | 12 | 0% | 0 | 0 |
| `rgeo/172_trgeo_tempspatialrels.in.sql` | 6 | 0 | 0 | 6 | 0% | 0 | 0 |
| `rgeo/173_trgeo_indexes.in.sql` | 2 | 0 | 0 | 2 | 0% | 0 | 0 |
| `rgeo/175_trgeo_geom_clip.in.sql` | 2 | 0 | 1 | 1 | 0% | 0 | 0 |
| `rgeo/176_trgeo_vclip.in.sql` | 5 | 0 | 2 | 3 | 0% | 0 | 0 |
| `temporal/001_set.in.sql` | 48 | 47 | 1 | 0 | 98% | 34 | 38 |
| `temporal/002_set_ops.in.sql` | 15 | 14 | 1 | 0 | 93% | 1 | 171 |
| `temporal/003_span.in.sql` | 46 | 45 | 1 | 0 | 98% | 22 | 30 |
| `temporal/005_span_ops.in.sql` | 16 | 13 | 3 | 0 | 81% | 0 | 160 |
| `temporal/007_spanset.in.sql` | 61 | 60 | 1 | 0 | 98% | 20 | 30 |
| `temporal/009_spanset_ops.in.sql` | 20 | 17 | 3 | 0 | 85% | 0 | 280 |
| `temporal/015_span_aggfuncs.in.sql` | 0 | 0 | 0 | 0 | 0% | 10 | 0 |
| `temporal/021_tbox.in.sql` | 53 | 52 | 1 | 0 | 98% | 8 | 21 |
| `temporal/022_temporal.in.sql` | 115 | 112 | 3 | 0 | 97% | 17 | 30 |
| `temporal/023_temporal_inout.in.sql` | 19 | 18 | 1 | 0 | 95% | 0 | 0 |
| `temporal/025_temporal_tile.in.sql` | 16 | 10 | 6 | 0 | 62% | 0 | 0 |
| `temporal/026_tnumber_mathfuncs.in.sql` | 20 | 20 | 0 | 0 | 100% | 0 | 36 |
| `temporal/028_tbool_boolops.in.sql` | 4 | 1 | 3 | 0 | 25% | 0 | 7 |
| `temporal/029_ttext_textfuncs.in.sql` | 4 | 4 | 0 | 0 | 100% | 0 | 3 |
| `temporal/030_temporal_compops.in.sql` | 18 | 18 | 0 | 0 | 100% | 1 | 234 |
| `temporal/032_temporal_boxops.in.sql` | 6 | 6 | 0 | 0 | 100% | 0 | 0 |
| `temporal/033_temporal_topops.in.sql` | 5 | 5 | 0 | 0 | 100% | 1 | 135 |
| `temporal/034_temporal_posops.in.sql` | 8 | 8 | 0 | 0 | 100% | 0 | 168 |
| `temporal/036_tnumber_distance.in.sql` | 2 | 2 | 0 | 0 | 100% | 0 | 25 |
| `temporal/038_temporal_similarity.in.sql` | 7 | 7 | 0 | 0 | 100% | 0 | 0 |
| `temporal/040_temporal_aggfuncs.in.sql` | 0 | 0 | 0 | 0 | 0% | 47 | 0 |
| `temporal/042_temporal_waggfuncs.in.sql` | 0 | 0 | 0 | 0 | 0% | 11 | 0 |
| `temporal/046_temporal_analytics.in.sql` | 5 | 4 | 1 | 0 | 80% | 0 | 0 |
| **TOTAL** | **3121** | **1904** | **269** | **948** | **61%** | **446** | — |

## Missing function names per section

Every value type these name is one MobilityDuck already registers, so each is a registration it can add as it stands.

### `cbuffer/200_cbuffer.in.sql` — 15 missing of 36 addressable (58% covered)

- `cbuffer` (2 overloads)
- `cbufferFromBinary`
- `cbufferFromEWKB`
- `cbufferFromEWKT`
- `cbufferFromHexEWKB`
- `cbufferFromText`
- `cbuffer_contains`
- `cbuffer_covers`
- `cbuffer_disjoint`
- `cbuffer_dwithin`
- `cbuffer_intersects`
- `cbuffer_same`
- `cbuffer_touches`
- `point`
- `transformPipeline`

### `cbuffer/202_tcbuffer.in.sql` — 12 missing of 86 addressable (83% covered)

- `points`
- `tcbufferFromBinary`
- `tcbufferFromEWKB`
- `tcbufferFromEWKT`
- `tcbufferFromHexEWKB`
- `tcbufferFromMFJSON`
- `tcbufferFromText`
- `tcbufferInst`
- `tcbufferSeq` (2 overloads)
- `tcbufferSeqSet` (2 overloads)
- `tcbufferSeqSetGaps`
- `unnest`

### `cbuffer/205_tcbuffer_spatialfuncs.in.sql` — 1 missing of 13 addressable (92% covered)

- `transformPipeline`

### `cbuffer/210_tcbuffer_distance.in.sql` — 1 missing of 6 addressable (83% covered)

- `distance` (5 overloads)

### `cbuffer/216_tcbuffer_indexes.in.sql` — 2 missing of 2 addressable (0% covered)

- `tcbuffer_gist_consistent`
- `tcbuffer_gist_distance`

### `geo/049_geo_funcs.in.sql` — 3 missing of 5 addressable (40% covered)

- `buffer`
- `orientedEnvelope`
- `relate`

### `geo/050_geoset.in.sql` — 13 missing of 45 addressable (71% covered)

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
- `setDistance` (6 overloads)
- `transformPipeline` (2 overloads)
- `unnest` (2 overloads)

### `geo/051_stbox.in.sql` — 14 missing of 75 addressable (81% covered)

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

### `geo/052_tgeo.in.sql` — 5 missing of 71 addressable (93% covered)

- `tgeographySeqSet` (2 overloads)
- `tgeographySeqSetGaps`
- `tgeometrySeqSet` (2 overloads)
- `tgeometrySeqSetGaps`
- `unnest` (2 overloads)

### `geo/052_tpoint.in.sql` — 3 missing of 72 addressable (96% covered)

- `tgeogpointSeqSetGaps`
- `tgeompointSeqSetGaps`
- `unnest` (2 overloads)

### `geo/056_tgeo_spatialfuncs.in.sql` — 1 missing of 16 addressable (94% covered)

- `transformPipeline` (2 overloads)

### `geo/056_tpoint_spatialfuncs.in.sql` — 8 missing of 33 addressable (76% covered)

- `atElevation`
- `bearing` (7 overloads)
- `difference`
- `geoUnion`
- `minusElevation`
- `symDifference`
- `transformGK` (2 overloads)
- `transformPipeline` (3 overloads)

### `geo/058_tgeo_tile.in.sql` — 3 missing of 5 addressable (40% covered)

- `spaceSplit` (3 overloads)
- `spaceTimeSplit` (3 overloads)
- `timeBoxes`

### `geo/058_tpoint_tile.in.sql` — 3 missing of 11 addressable (73% covered)

- `spaceSplit` (3 overloads)
- `spaceTimeSplit` (3 overloads)
- `timeBoxes`

### `geo/076_tpoint_analytics.in.sql` — 2 missing of 19 addressable (89% covered)

- `extendedKalmanFilter`
- `geography` (2 overloads)

### `geo/078_tpoint_datagen.in.sql` — 1 missing of 1 addressable (0% covered)

- `createTrip`

### `h3/250_h3index.in.sql` — 9 missing of 15 addressable (40% covered)

- `h3indexFromBinary`
- `h3indexFromHexWKB`
- `h3index_cmp`
- `h3index_eq`
- `h3index_ge`
- `h3index_gt`
- `h3index_le`
- `h3index_lt`
- `h3index_ne`

### `h3/251_h3indexset.in.sql` — 3 missing of 27 addressable (89% covered)

- `h3indexsetFromBinary`
- `h3indexsetFromHexWKB`
- `unnest`

### `h3/252_h3index_ops.in.sql` — 8 missing of 11 addressable (27% covered)

- `h3CellToChildren`
- `h3CellToVertexes`
- `h3CompactCells`
- `h3GetIcosahedronFaces`
- `h3GridDisk`
- `h3GridPathCells`
- `h3GridRing`
- `h3OriginToDirectedEdges`

### `h3/253_th3index.in.sql` — 8 missing of 76 addressable (89% covered)

- `th3indexFromBinary`
- `th3indexFromHexWKB`
- `th3indexFromMFJSON`
- `th3indexInst`
- `th3indexSeq` (2 overloads)
- `th3indexSeqSet` (2 overloads)
- `th3indexSeqSetGaps`
- `unnest`

### `h3/272_th3index_gist.in.sql` — 1 missing of 1 addressable (0% covered)

- `th3index_gist_consistent`

### `h3/286_th3index_metrics.in.sql` — 3 missing of 3 addressable (0% covered)

- `greatCircleDistance`
- `th3CellArea`
- `th3EdgeLength`

### `json/452_tjsonb.in.sql` — 3 missing of 76 addressable (92% covered)

- `tjsonbSeqSet` (2 overloads)
- `tjsonbSeqSetGaps`
- `unnest`

### `json/454_tjsonb_jsonfuncs.in.sql` — 35 missing of 46 addressable (24% covered)

- `tjsonArrayElement`
- `tjsonArrayElementOpr`
- `tjsonArrayLength`
- `tjsonExtractPath`
- `tjsonExtractPathOpr`
- `tjsonObjectField`
- `tjsonObjectFieldOpr`
- `tjsonStripNulls`
- `tjsonbArrayElement`
- `tjsonbArrayElementOpr`
- `tjsonbArrayElementText`
- `tjsonbArrayElementTextOpr`
- `tjsonbDeleteArray`
- `tjsonbDeletePath`
- `tjsonbExtractPath`
- `tjsonbExtractPathOpr`
- `tjsonbExtractPathText`
- `tjsonbExtractPathTextOpr`
- `tjsonbInsert`
- `tjsonbObjectField`
- `tjsonbObjectFieldOpr`
- `tjsonbObjectFieldText`
- `tjsonbObjectFieldTextOpr`
- `tjsonbPathExists`
- `tjsonbPathExistsOpr`
- `tjsonbPathExistsTz`
- `tjsonbPathMatch`
- `tjsonbPathMatchOpr`
- `tjsonbPathMatchTz`
- `tjsonbPathQueryArray`
- `tjsonbPathQueryArrayTz`
- `tjsonbPathQueryFirst`
- `tjsonbPathQueryFirstTz`
- `tjsonbSet`
- `tjsonbSetLax`

### `json/456_tjsonb_op.in.sql` — 3 missing of 5 addressable (40% covered)

- `tjsonbContained` (3 overloads)
- `tjsonbExistsAll`
- `tjsonbExistsAny`

### `json/466_tjsonb_indexes.in.sql` — 2 missing of 2 addressable (0% covered)

- `tjsonb_gist_compress`
- `tjsonb_gist_consistent`

### `npoint/300_npoint.in.sql` — 8 missing of 28 addressable (71% covered)

- `getPosition`
- `npoint` (2 overloads)
- `npointFromBinary`
- `npointFromEWKB`
- `npointFromEWKT`
- `npointFromHexEWKB`
- `npointFromText`
- `nsegment` (3 overloads)

### `npoint/302_tnpoint.in.sql` — 10 missing of 80 addressable (84% covered)

- `positions`
- `routes`
- `tnpointFromBinary`
- `tnpointFromHexWKB`
- `tnpointFromMFJSON`
- `tnpointInst`
- `tnpointSeq` (2 overloads)
- `tnpointSeqSet` (2 overloads)
- `tnpointSeqSetGaps`
- `unnest`

### `npoint/310_tnpoint_routeops.in.sql` — 4 missing of 4 addressable (0% covered)

- `contained_rid` (5 overloads)
- `contains_rid` (5 overloads)
- `overlaps_rid` (3 overloads)
- `same_rid` (7 overloads)

### `npoint/311_tnpoint_gin.in.sql` — 3 missing of 3 addressable (0% covered)

- `tnpoint_gin_extract_query`
- `tnpoint_gin_extract_value`
- `tnpoint_gin_triconsistent`

### `npoint/316_tnpoint_indexes.in.sql` — 2 missing of 2 addressable (0% covered)

- `tnpoint_gist_consistent`
- `tnpoint_gist_distance`

### `pointcloud/399_pointcloud_schemas.in.sql` — 3 missing of 3 addressable (0% covered)

- `pointCloudSchemaCompression`
- `pointCloudSchemaNDims`
- `pointCloudSchemaSRID`

### `pointcloud/400_pcset.in.sql` — 1 missing of 38 addressable (16% covered)

- `getDim`

### `pointcloud/420_tpcpoint.in.sql` — 4 missing of 80 addressable (89% covered)

- `getDim`
- `pcpoint` (2 overloads)
- `tpcpointSeqSet` (2 overloads)
- `unnest`

### `pointcloud/430_tpcpatch.in.sql` — 6 missing of 84 addressable (85% covered)

- `endNumPoints`
- `pcpatch`
- `points`
- `startNumPoints`
- `tpcpatchSeqSet` (2 overloads)
- `unnest`

### `pointcloud/439_tpc_gist.in.sql` — 4 missing of 7 addressable (0% covered)

- `tpc_gist_compress`
- `tpcbox_gist_penalty`
- `tpcbox_gist_picksplit`
- `tpcbox_gist_sortsupport`

### `pointcloud/440_tpc_spgist.in.sql` — 2 missing of 2 addressable (0% covered)

- `tpc_spgist_compress`
- `tpcbox_spgist_compress`

### `pointcloud/446_tpcpoint_tile.in.sql` — 3 missing of 5 addressable (40% covered)

- `spaceSplit` (3 overloads)
- `spaceTimeSplit` (3 overloads)
- `timeBoxes`

### `pose/120_geopose_frames.in.sql` — 4 missing of 4 addressable (0% covered)

- `geoPoseFrameIsGeographic`
- `geoPoseFrameName`
- `geoPoseFrameSRID`
- `geoPoseFrames`

### `quadbin/350_quadbin.in.sql` — 1 missing of 11 addressable (91% covered)

- `isValidIndex`

### `quadbin/352_quadbin_ops.in.sql` — 8 missing of 12 addressable (17% covered)

- `geoToQuadbinCell`
- `quadbinCellArea`
- `quadbinCellSibling`
- `quadbinCellToBoundary`
- `quadbinCellToParent`
- `quadbinCellToPoint`
- `quadbinCellToQuadkey`
- `quadbinCellToTile`

### `quadbin/353_tquadbin.in.sql` — 8 missing of 76 addressable (86% covered)

- `tquadbinFromBinary`
- `tquadbinFromHexWKB`
- `tquadbinFromMFJSON`
- `tquadbinInst`
- `tquadbinSeq` (2 overloads)
- `tquadbinSeqSet` (2 overloads)
- `tquadbinSeqSetGaps`
- `unnest`

### `quadbin/355_tquadbin_spatialfuncs.in.sql` — 5 missing of 7 addressable (29% covered)

- `tquadbinCellArea`
- `tquadbinCellToBoundary`
- `tquadbinCellToParent`
- `tquadbinCellToPoint`
- `tquadbinGetResolution`

### `quadbin/372_tquadbin_gist.in.sql` — 1 missing of 1 addressable (0% covered)

- `tquadbin_gist_consistent`

### `raster/500_raster.in.sql` — 2 missing of 31 addressable (94% covered)

- `numBands`
- `quadbins`

### `rgeo/175_trgeo_geom_clip.in.sql` — 1 missing of 2 addressable (0% covered)

- `_trgeometry_geom_clip_polygon`

### `rgeo/176_trgeo_vclip.in.sql` — 2 missing of 5 addressable (0% covered)

- `vClipPolyPoint`
- `vClipPolyPoly`

### `temporal/001_set.in.sql` — 1 missing of 48 addressable (98% covered)

- `unnest` (6 overloads)

### `temporal/002_set_ops.in.sql` — 1 missing of 15 addressable (93% covered)

- `setDistance` (15 overloads)

### `temporal/003_span.in.sql` — 1 missing of 46 addressable (98% covered)

- `range` (4 overloads)

### `temporal/005_span_ops.in.sql` — 3 missing of 16 addressable (81% covered)

- `distance` (15 overloads)
- `spanMinus` (15 overloads)
- `spanUnion` (15 overloads)

### `temporal/007_spanset.in.sql` — 1 missing of 61 addressable (98% covered)

- `multirange` (4 overloads)

### `temporal/009_spanset_ops.in.sql` — 3 missing of 20 addressable (85% covered)

- `distance` (25 overloads)
- `spanMinus` (5 overloads)
- `spanUnion` (5 overloads)

### `temporal/021_tbox.in.sql` — 1 missing of 53 addressable (98% covered)

- `bigintspan`

### `temporal/022_temporal.in.sql` — 3 missing of 115 addressable (97% covered)

- `mobilitydbFullVersion`
- `mobilitydbVersion`
- `unnest` (4 overloads)

### `temporal/023_temporal_inout.in.sql` — 1 missing of 19 addressable (95% covered)

- `tbigintFromBinary`

### `temporal/025_temporal_tile.in.sql` — 6 missing of 16 addressable (62% covered)

- `timeBins` (5 overloads)
- `timeBoxes` (3 overloads)
- `valueBins` (3 overloads)
- `valueBoxes` (3 overloads)
- `valueSplit` (3 overloads)
- `valueTimeBoxes` (3 overloads)

### `temporal/028_tbool_boolops.in.sql` — 3 missing of 4 addressable (25% covered)

- `tAnd` (3 overloads)
- `tNot`
- `tOr` (3 overloads)

### `temporal/046_temporal_analytics.in.sql` — 1 missing of 5 addressable (80% covered)

- `extendedKalmanFilter`

## Appendix B — Out of scope (PG-only, no DuckDB equivalent)

These entries are PG-specific helpers — index opclasses, aggregate transition/combine/final/serialize callbacks, planner hooks (`_sel`, `_joinsel`, `_supportfn`, `_analyze`), text/binary I/O helpers (`_in`, `_out`, `_recv`, `_send`), type modifier helpers, the `999_oid_cache` PG catalog hook, and PG geometric type constructors (`019_geo_constructors`).  None of them have DuckDB equivalents and they should not be implemented; listed here only for completeness.

### Whole sections excluded

| Section | Names |
|---|---:|
| `geo/073_tgeo_gist.in.sql` | 11 |
| `geo/073_tpoint_gist.in.sql` | 5 |
| `geo/074_tgeo_spgist.in.sql` | 9 |
| `temporal/011_span_indexes.in.sql` | 20 |
| `temporal/012_spanset_indexes.in.sql` | 3 |
| `temporal/013_set_indexes.in.sql` | 10 |
| `temporal/019_geo_constructors.in.sql` | 7 |
| `temporal/043_temporal_gist.in.sql` | 20 |
| `temporal/044_temporal_spgist.in.sql` | 10 |
| `temporal/999_oid_cache.in.sql` | 1 |

### PG helpers inside active sections

| Section | PG helpers |
|---|---:|
| `cbuffer/200_cbuffer.in.sql` | 4 |
| `cbuffer/201_cbufferset.in.sql` | 6 |
| `cbuffer/202_tcbuffer.in.sql` | 5 |
| `cbuffer/210_tcbuffer_distance.in.sql` | 1 |
| `cbuffer/211_tcbuffer_aggfuncs.in.sql` | 7 |
| `geo/050_geoset.in.sql` | 12 |
| `geo/051_stbox.in.sql` | 8 |
| `geo/052_tgeo.in.sql` | 10 |
| `geo/052_tpoint.in.sql` | 8 |
| `geo/054_tgeo_compops.in.sql` | 1 |
| `geo/068_tgeo_aggfuncs.in.sql` | 9 |
| `geo/068_tpoint_aggfuncs.in.sql` | 12 |
| `geo/071_tgeo_setset.in.sql` | 1 |
| `geo/071_tpoint_setset.in.sql` | 1 |
| `h3/250_h3index.in.sql` | 4 |
| `h3/251_h3indexset.in.sql` | 6 |
| `h3/253_th3index.in.sql` | 4 |
| `h3/261_th3index_aggfuncs.in.sql` | 8 |
| `json/450_jsonbset.in.sql` | 6 |
| `json/452_tjsonb.in.sql` | 4 |
| `json/464_tjsonb_aggfuncs.in.sql` | 8 |
| `npoint/300_npoint.in.sql` | 8 |
| `npoint/301_npointset.in.sql` | 6 |
| `npoint/302_tnpoint.in.sql` | 4 |
| `npoint/314_tnpoint_aggfuncs.in.sql` | 9 |
| `pointcloud/400_pcset.in.sql` | 11 |
| `pointcloud/410_tpcbox.in.sql` | 4 |
| `pointcloud/420_tpcpoint.in.sql` | 6 |
| `pointcloud/430_tpcpatch.in.sql` | 4 |
| `pointcloud/441_tpc_aggfuncs.in.sql` | 12 |
| `pose/100_pose.in.sql` | 4 |
| `pose/101_poseset.in.sql` | 6 |
| `pose/102_tpose.in.sql` | 5 |
| `pose/111_tpose_aggfuncs.in.sql` | 8 |
| `posechain/550_posechain.in.sql` | 4 |
| `posechain/551_posechainset.in.sql` | 6 |
| `posechain/552_tposechain.in.sql` | 5 |
| `posechain/561_tposechain_aggfuncs.in.sql` | 8 |
| `quadbin/350_quadbin.in.sql` | 4 |
| `quadbin/351_quadbinset.in.sql` | 6 |
| `quadbin/353_tquadbin.in.sql` | 4 |
| `quadbin/361_tquadbin_aggfuncs.in.sql` | 8 |
| `raster/500_raster.in.sql` | 4 |
| `rgeo/150_trgeo.in.sql` | 5 |
| `rgeo/168_trgeo_aggfuncs.in.sql` | 8 |
| `temporal/001_set.in.sql` | 34 |
| `temporal/002_set_ops.in.sql` | 1 |
| `temporal/003_span.in.sql` | 22 |
| `temporal/007_spanset.in.sql` | 20 |
| `temporal/015_span_aggfuncs.in.sql` | 10 |
| `temporal/021_tbox.in.sql` | 8 |
| `temporal/022_temporal.in.sql` | 17 |
| `temporal/030_temporal_compops.in.sql` | 1 |
| `temporal/033_temporal_topops.in.sql` | 1 |
| `temporal/040_temporal_aggfuncs.in.sql` | 47 |
| `temporal/042_temporal_waggfuncs.in.sql` | 11 |

## Appendix C — Value types MobilityDuck does not register

MobilityDB declares 57 value types and MobilityDuck registers 46.  A signature naming one of the types below cannot be spelled in DuckDB at all, so its entries are counted as blocked rather than missing.  A type whose whole family appears here is a family the binding does not carry.

| Type | Entries blocked | Sections |
|---|---:|---|
| `tpose` | 180 | `pose/102_tpose.in.sql`, `pose/104_tpose_compops.in.sql`, `pose/105_tpose_spatialfuncs.in.sql`, `pose/107_tpose_boxops.in.sql`, `pose/108_tpose_topops.in.sql`, `pose/109_tpose_posops.in.sql`, `pose/110_tpose_distance.in.sql`, `pose/112_tpose_spatialrels.in.sql`, `pose/114_tpose_tempspatialrels.in.sql`, `pose/116_tpose_indexes.in.sql`, `pose/117_tpose_analytics.in.sql`, `pose/119_tpose_tile.in.sql`, `posechain/552_tposechain.in.sql`, `rgeo/150_trgeo.in.sql` |
| `trgeometry` | 175 | `rgeo/150_trgeo.in.sql`, `rgeo/154_trgeo_compops.in.sql`, `rgeo/156_trgeo_spatialfuncs.in.sql`, `rgeo/158_trgeo_tile.in.sql`, `rgeo/160_trgeo_boxops.in.sql`, `rgeo/161_trgeo_topops.in.sql`, `rgeo/162_trgeo_posops.in.sql`, `rgeo/164_trgeo_distance.in.sql`, `rgeo/166_trgeo_similarity.in.sql`, `rgeo/170_trgeo_spatialrels.in.sql`, `rgeo/172_trgeo_tempspatialrels.in.sql`, `rgeo/173_trgeo_indexes.in.sql`, `rgeo/176_trgeo_vclip.in.sql` |
| `tposechain` | 129 | `posechain/552_tposechain.in.sql`, `posechain/553_tposechain_spatialfuncs.in.sql`, `posechain/554_tposechain_compops.in.sql`, `posechain/558_tposechain_topops.in.sql`, `posechain/559_tposechain_posops.in.sql`, `posechain/564_tposechain_tempspatialrels.in.sql` |
| `tpcbox` | 85 | `pointcloud/410_tpcbox.in.sql`, `pointcloud/420_tpcpoint.in.sql`, `pointcloud/430_tpcpatch.in.sql`, `pointcloud/436_tpc_topops.in.sql`, `pointcloud/437_tpc_posops.in.sql`, `pointcloud/438_tpc_distance.in.sql`, `pointcloud/439_tpc_gist.in.sql` |
| `pose` | 82 | `pose/100_pose.in.sql`, `pose/101_poseset.in.sql`, `pose/102_tpose.in.sql`, `pose/104_tpose_compops.in.sql`, `pose/108_tpose_topops.in.sql`, `pose/110_tpose_distance.in.sql`, `posechain/550_posechain.in.sql`, `rgeo/150_trgeo.in.sql`, `rgeo/175_trgeo_geom_clip.in.sql` |
| `jsonbset` | 73 | `json/450_jsonbset.in.sql`, `json/451_jsonbset_jsonfuncs.in.sql`, `json/452_tjsonb.in.sql` |
| `posechain` | 61 | `posechain/550_posechain.in.sql`, `posechain/551_posechainset.in.sql`, `posechain/552_tposechain.in.sql`, `posechain/554_tposechain_compops.in.sql`, `posechain/558_tposechain_topops.in.sql` |
| `poseset` | 45 | `pose/101_poseset.in.sql`, `pose/102_tpose.in.sql`, `rgeo/150_trgeo.in.sql` |
| `posechainset` | 43 | `posechain/551_posechainset.in.sql`, `posechain/552_tposechain.in.sql` |
| `cbufferset` | 41 | `cbuffer/201_cbufferset.in.sql`, `cbuffer/202_tcbuffer.in.sql` |
| `npointset` | 41 | `npoint/301_npointset.in.sql`, `npoint/302_tnpoint.in.sql` |
| `quadbinset` | 33 | `quadbin/351_quadbinset.in.sql`, `quadbin/352_quadbin_ops.in.sql`, `quadbin/353_tquadbin.in.sql` |
| `pcpatchset` | 32 | `pointcloud/400_pcset.in.sql`, `pointcloud/430_tpcpatch.in.sql` |
| `pcpointset` | 32 | `pointcloud/400_pcset.in.sql`, `pointcloud/420_tpcpoint.in.sql` |

