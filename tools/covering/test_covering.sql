-- Loads the generated covering macros and checks they materialise the covering
-- columns. Run with a MobilityDuck-enabled DuckDB:
--   duckdb < tools/covering/test_covering.sql
.read tools/covering/covering.sql

-- spatial: a tgeogpoint trajectory -> xmin..zmax, tmin/tmax, srid
WITH t AS (
  SELECT 7 AS mmsi,
         tgeogpointSeq(list(TGEOGPOINT(ST_Point(lon, lat), ts) ORDER BY ts)) AS traj
  FROM (VALUES (TIMESTAMPTZ '2026-02-26 08:00:00+00', 4.40, 51.20),
               (TIMESTAMPTZ '2026-02-26 10:00:00+00', 4.95, 51.48)) v(ts, lon, lat)
)
SELECT mmsi, c.xmin, c.xmax, c.ymin, c.ymax, c.zmin, c.tmin, c.tmax, c.srid
FROM (SELECT mmsi, covering_spatial(traj) AS c FROM t);

-- number: a tfloat -> vmin/vmax, tmin/tmax
WITH t AS (SELECT tfloat '[1.5@2026-02-26 08:00:00+00, 9.2@2026-02-26 10:00:00+00]' AS v)
SELECT c.vmin, c.vmax, c.tmin, c.tmax
FROM (SELECT covering_number(v) AS c FROM t);

-- time-only: a tbool -> tmin/tmax (no spatial box)
WITH t AS (SELECT tbool '[true@2026-02-26 08:00:00+00, false@2026-02-26 10:00:00+00]' AS v)
SELECT c.tmin, c.tmax
FROM (SELECT covering_timeOnly(v) AS c FROM t);
