-- quickstart_mobilitydb.sql — Edge-to-Cloud demo on PostgreSQL / MobilityDB
--
-- Companion to quickstart.sql.  Builds the same five synthetic trajectories and
-- runs the same three analytics queries — proving the portable named-function
-- SQL dialect produces identical results on both DuckDB and PostgreSQL.
--
-- Requirements: PostgreSQL with MobilityDB and PostGIS installed.
-- Run as: psql -d <your_db> -f quickstart_mobilitydb.sql
--
-- Reading a TemporalParquet shard written by MobilityDuck:
--   Install pg_parquet (https://github.com/CrunchyData/pg_parquet), then:
--     CREATE EXTENSION pg_parquet;
--     SELECT tgeogpointFromBinary(traj), ping_count
--     FROM parquet.read('edge_to_cloud_demo.parquet');
--   Replace the WITH trajs CTE below with that table expression.

-- ─────────────────────────────────────────────────────────────────────────────
-- Construction (MobilityDB syntax)
--
-- Key difference from DuckDB:
--   DuckDB:    tgeogpointSeq(list(TGEOGPOINT(ST_Point(lon,lat), ts) ORDER BY ts))
--   MobilityDB: tgeogpointseq(array_agg(
--                   format('SRID=4326;POINT(%s %s)@%s',lon,lat,ts)::tgeogpoint
--                   ORDER BY ts))
--
-- The analytics queries (A, B, C) below are identical on both platforms.
-- ─────────────────────────────────────────────────────────────────────────────

WITH raw AS (
    SELECT
        entity_id,
        round((start_lon + delta_lon * s)::numeric, 6)::float8 AS lon,
        round((start_lat + delta_lat * s)::numeric, 6)::float8 AS lat,
        TIMESTAMPTZ '2026-01-15 08:00:00+00' + (s * INTERVAL '10 minutes') AS ts
    FROM (VALUES
        -- entity_id  start_lon  start_lat  delta_lon  delta_lat
        (1,  10.00,   55.50,   0.23,   0.05),
        (2,  14.00,   56.00,  -0.18,  -0.08),
        (3,   8.50,   57.50,   0.06,  -0.06),
        (4,  12.10,   55.20,   0.04,   0.02),
        (5,   9.50,   54.50,   0.22,   0.06)
    ) t(entity_id, start_lon, start_lat, delta_lon, delta_lat),
    generate_series(0, 11) g(s)
),
trajs AS (
    SELECT
        entity_id,
        tgeogpointseq(
            array_agg(
                format('SRID=4326;POINT(%s %s)@%s', lon, lat, ts)::tgeogpoint
                ORDER BY ts
            )
        ) AS traj
    FROM raw
    GROUP BY entity_id
)
-- ─────────────────────────────────────────────────────────────────────────────
-- Query A: total distance and maximum speed per vessel
-- Identical to DuckDB — length() returns geodetic metres, speed() returns m/s
-- ─────────────────────────────────────────────────────────────────────────────
SELECT
    entity_id,
    round(length(traj))                        AS length_m,
    round(maxValue(speed(traj))::numeric, 2)   AS max_speed_ms
FROM trajs
ORDER BY length_m DESC;

-- Expected (same as DuckDB):
--  entity_id | length_m | max_speed_ms
-- -----------+----------+--------------
--          5 |   172001 |        26.22
--          1 |   170169 |        25.93
--          2 |   158771 |        24.21
--          3 |    83644 |        12.70
--          4 |    37155 |         5.64


-- ─────────────────────────────────────────────────────────────────────────────
-- Query B: vessels that entered the Copenhagen bounding box
-- MobilityDB uses ST_GeomFromText with SRID=4326 prefix (EWKT).
-- DuckDB requires GEODSTBOX workaround (DuckDB geometry type carries no SRID).
-- Both return the same four vessels.
-- ─────────────────────────────────────────────────────────────────────────────
WITH raw AS (
    SELECT
        entity_id,
        round((start_lon + delta_lon * s)::numeric, 6)::float8 AS lon,
        round((start_lat + delta_lat * s)::numeric, 6)::float8 AS lat,
        TIMESTAMPTZ '2026-01-15 08:00:00+00' + (s * INTERVAL '10 minutes') AS ts
    FROM (VALUES
        (1,  10.00,   55.50,   0.23,   0.05),
        (2,  14.00,   56.00,  -0.18,  -0.08),
        (3,   8.50,   57.50,   0.06,  -0.06),
        (4,  12.10,   55.20,   0.04,   0.02),
        (5,   9.50,   54.50,   0.22,   0.06)
    ) t(entity_id, start_lon, start_lat, delta_lon, delta_lat),
    generate_series(0, 11) g(s)
),
trajs AS (
    SELECT entity_id,
           tgeogpointseq(array_agg(
               format('SRID=4326;POINT(%s %s)@%s', lon, lat, ts)::tgeogpoint
               ORDER BY ts)) AS traj
    FROM raw GROUP BY entity_id
)
SELECT entity_id
FROM trajs
WHERE eIntersects(
    ST_GeomFromText('SRID=4326;POLYGON((11.5 55.0,13.5 55.0,13.5 56.5,11.5 56.5,11.5 55.0))'),
    traj
)
ORDER BY entity_id;

-- Expected (same as DuckDB):
--  entity_id
-- -----------
--          1
--          2
--          4
--          5


-- ─────────────────────────────────────────────────────────────────────────────
-- Query C: trip duration (timezone-independent interval)
-- ─────────────────────────────────────────────────────────────────────────────
WITH raw AS (
    SELECT
        entity_id,
        round((start_lon + delta_lon * s)::numeric, 6)::float8 AS lon,
        round((start_lat + delta_lat * s)::numeric, 6)::float8 AS lat,
        TIMESTAMPTZ '2026-01-15 08:00:00+00' + (s * INTERVAL '10 minutes') AS ts
    FROM (VALUES
        (1,  10.00,   55.50,   0.23,   0.05),
        (2,  14.00,   56.00,  -0.18,  -0.08),
        (3,   8.50,   57.50,   0.06,  -0.06),
        (4,  12.10,   55.20,   0.04,   0.02),
        (5,   9.50,   54.50,   0.22,   0.06)
    ) t(entity_id, start_lon, start_lat, delta_lon, delta_lat),
    generate_series(0, 11) g(s)
),
trajs AS (
    SELECT entity_id,
           tgeogpointseq(array_agg(
               format('SRID=4326;POINT(%s %s)@%s', lon, lat, ts)::tgeogpoint
               ORDER BY ts)) AS traj
    FROM raw GROUP BY entity_id
)
SELECT entity_id, duration(traj) AS trip_duration
FROM trajs
ORDER BY entity_id;

-- Expected (same as DuckDB):
--  entity_id | trip_duration
-- -----------+---------------
--          1 | 01:50:00
--          2 | 01:50:00
--          3 | 01:50:00
--          4 | 01:50:00
--          5 | 01:50:00
