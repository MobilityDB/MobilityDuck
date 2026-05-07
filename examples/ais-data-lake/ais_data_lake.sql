-- ais_data_lake.sql — AIS trajectory data lake demo
--
-- Demonstrates the TemporalParquet data-lake pattern using Danish AIS data:
--   1. Ingest raw AIS pings from CSV into MobilityDuck
--   2. Build tgeogpoint trajectories per vessel (MMSI) — geodetic WGS-84
--   3. Write to a Parquet "shard" using asBinary() for portable MEOS-WKB encoding
--   4. Read back from Parquet and run analytics using temporal operators
--
-- After running, annotate the output Parquet with TemporalParquet metadata:
--   python3 ../../tools/temporal_parquet.py annotate ais_trajectories.parquet \
--     --column "name=traj,base_type=tgeogpoint,subtype=Sequence,interp=linear"
--
-- Requirements: MobilityDuck extension loaded, AIS CSV available at the path below.
-- Run from the examples/ais-data-lake/ directory.

LOAD '../../build/release/extension/mobilityduck/mobilityduck.duckdb_extension';
LOAD '../../build/release/extension/parquet/parquet.duckdb_extension';

-- ─────────────────────────────────────────────────────────────────────────────
-- Step 1: ingest raw AIS pings
-- Limit to Class A vessels and a 1-hour window to keep the demo fast.
-- Remove rows with invalid coordinates and deduplicate (mmsi, ts) pairs —
-- the raw feed occasionally emits duplicate messages at the same timestamp.
-- ─────────────────────────────────────────────────────────────────────────────

CREATE OR REPLACE TABLE ais_raw AS
SELECT
    CAST("# Timestamp" AS TIMESTAMPTZ)  AS ts,   -- DuckDB auto-parses the timestamp
    CAST(MMSI         AS BIGINT)        AS mmsi,
    CAST(Latitude     AS DOUBLE)        AS lat,
    CAST(Longitude    AS DOUBLE)        AS lon
FROM read_csv_auto(
    '../../meos/examples/data/aisdk-2026-02-26.csv',
    header  = true,
    nullstr = '',
    delim   = ','
)
WHERE "Type of mobile" = 'Class A'
  AND TRY_CAST(Latitude  AS DOUBLE) BETWEEN  -90 AND  90
  AND TRY_CAST(Longitude AS DOUBLE) BETWEEN -180 AND 180
  AND "# Timestamp" < '2026-02-26 01:00:00'
QUALIFY ROW_NUMBER() OVER (PARTITION BY CAST(MMSI AS BIGINT), "# Timestamp"
                           ORDER BY     "# Timestamp") = 1
;

SELECT count(*) AS raw_pings, count(DISTINCT mmsi) AS vessels FROM ais_raw;

-- ─────────────────────────────────────────────────────────────────────────────
-- Step 2: build tgeogpoint trajectories
-- One tgeogpoint Sequence per vessel, built from all its AIS pings ordered by ts.
-- Constructor: tgeogpointSeq(list(TGEOGPOINT(geom, ts) ORDER BY ts))
-- TGEOGPOINT uses geodetic (WGS-84) math, so length() returns metres.
-- ─────────────────────────────────────────────────────────────────────────────

CREATE OR REPLACE TABLE trajectories AS
SELECT
    mmsi,
    tgeogpointSeq(
        list(TGEOGPOINT(ST_Point(lon, lat), ts) ORDER BY ts)
    ) AS traj
FROM ais_raw
GROUP BY mmsi
HAVING count(*) >= 3          -- discard vessels with fewer than 3 unique pings
;

SELECT count(*) AS trajectories FROM trajectories;

-- ─────────────────────────────────────────────────────────────────────────────
-- Step 3: write to Parquet using portable MEOS-WKB encoding
-- asBinary() converts the MEOS Temporal* value to standard WKB bytes.
-- The resulting column is BYTE_ARRAY in Parquet — use tgeogpointFromBinary()
-- to reconstruct the typed value on read.
-- ─────────────────────────────────────────────────────────────────────────────

COPY (
    SELECT
        mmsi,
        asBinary(traj)    AS traj,        -- MEOS-WKB BLOB
        numInstants(traj) AS ping_count
    FROM trajectories
)
TO 'ais_trajectories.parquet' (FORMAT PARQUET, ROW_GROUP_SIZE 1000);

-- Inspect what DuckDB wrote — traj column must appear as BYTE_ARRAY
SELECT name, type FROM parquet_schema('ais_trajectories.parquet')
WHERE name NOT IN ('duckdb_schema');

-- ─────────────────────────────────────────────────────────────────────────────
-- Step 4: analytics directly on the Parquet shard
-- After tgeompointFromBinary() reconstruction, all temporal operators work.
-- Note: length() returns Euclidean distance in the coordinate space (degrees
-- for SRID 0 / WGS-84 lon-lat input).  Use tgeogpoint for geodetic km.
-- ─────────────────────────────────────────────────────────────────────────────

-- Top 10 vessels by total trajectory length (metres, geodetic)
SELECT
    mmsi,
    ping_count,
    round(length(traj))                  AS length_m,      -- metres (geodetic WGS-84)
    round(maxValue(speed(traj)), 6)      AS max_speed_ms,  -- m/s
    numInstants(traj)                    AS instants
FROM (
    SELECT
        mmsi,
        ping_count,
        tgeogpointFromBinary(traj) AS traj
    FROM read_parquet('ais_trajectories.parquet')
)
ORDER BY length_m DESC
LIMIT 10;

-- Vessels that were ever within a bounding box around Copenhagen
-- (approx lon 11.5..13.5, lat 55.0..56.5):
-- eContains(geometry, tgeogpoint) — "ever" variant (at least one instant inside)
SELECT mmsi, ping_count
FROM (
    SELECT
        mmsi,
        ping_count,
        tgeogpointFromBinary(traj) AS traj
    FROM read_parquet('ais_trajectories.parquet')
)
WHERE eContains(
    ST_GeomFromText('POLYGON((11.5 55.0, 13.5 55.0, 13.5 56.5, 11.5 56.5, 11.5 55.0))'),
    traj
)
ORDER BY mmsi;
