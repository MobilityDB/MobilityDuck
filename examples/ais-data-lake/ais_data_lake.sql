-- ais_data_lake.sql — AIS trajectory data lake demo
--
-- Demonstrates the TemporalParquet data-lake pattern using Danish AIS data:
--   1. Ingest raw AIS pings from CSV into MobilityDuck
--   2. Build tgeompoint trajectories per vessel (MMSI)
--   3. Write to a Parquet "shard" using asBinary() for portable MEOS-WKB encoding
--   4. Read back from Parquet and run analytics using temporal operators
--
-- After running, annotate the output Parquet with TemporalParquet metadata:
--   python3 ../../tools/temporal_parquet.py annotate ais_trajectories.parquet \
--     --column "name=traj,base_type=tgeompoint,srid=4326,subtype=SequenceSet,interp=linear"
--
-- Requirements: MobilityDuck extension loaded, AIS CSV available.

LOAD '../../build/release/extension/mobilityduck/mobilityduck.duckdb_extension';
LOAD '../../build/release/extension/parquet/parquet.duckdb_extension';

-- ─────────────────────────────────────────────────────────────────────────────
-- Step 1: ingest raw AIS pings
-- Limit to Class A vessels and a 1-hour window to keep the demo fast.
-- Remove rows with invalid coordinates.
-- ─────────────────────────────────────────────────────────────────────────────

CREATE OR REPLACE TABLE ais_raw AS
SELECT
    CAST(strptime("# Timestamp", '%d/%m/%Y %H:%M:%S') AS TIMESTAMPTZ)  AS ts,
    CAST(MMSI AS BIGINT)                                                 AS mmsi,
    CAST(Latitude  AS DOUBLE)                                            AS lat,
    CAST(Longitude AS DOUBLE)                                            AS lon,
    SOG,
    COG,
    "Type of mobile"                                                     AS mobile_type
FROM read_csv_auto(
    '../../meos/examples/data/aisdk-2026-02-26.csv',
    header    = true,
    nullstr   = '',
    delim     = ','
)
WHERE "Type of mobile" = 'Class A'
  AND TRY_CAST(Latitude  AS DOUBLE) BETWEEN -90  AND  90
  AND TRY_CAST(Longitude AS DOUBLE) BETWEEN -180 AND 180
  AND strptime("# Timestamp", '%d/%m/%Y %H:%M:%S') < '2026-02-26 01:00:00'
;

SELECT count(*) AS raw_pings, count(DISTINCT mmsi) AS vessels FROM ais_raw;

-- ─────────────────────────────────────────────────────────────────────────────
-- Step 2: build tgeompoint trajectories
-- One tgeompoint SequenceSet per vessel, built from all its AIS pings
-- ordered by timestamp.
-- ─────────────────────────────────────────────────────────────────────────────

CREATE OR REPLACE TABLE trajectories AS
SELECT
    mmsi,
    tgeompoint(
        list(ST_Point(lon, lat) ORDER BY ts),
        list(ts              ORDER BY ts)
    ) AS traj
FROM ais_raw
GROUP BY mmsi
HAVING count(*) >= 3          -- discard single-ping "vessels"
;

SELECT count(*) AS trajectories FROM trajectories;

-- ─────────────────────────────────────────────────────────────────────────────
-- Step 3: write to Parquet using portable MEOS-WKB encoding
-- asBinary() converts the internal MEOS Temporal* struct to standard WKB bytes.
-- ─────────────────────────────────────────────────────────────────────────────

COPY (
    SELECT
        mmsi,
        asBinary(traj)   AS traj,       -- MEOS-WKB BLOB
        numInstants(traj) AS ping_count
    FROM trajectories
)
TO 'ais_trajectories.parquet' (FORMAT PARQUET, ROW_GROUP_SIZE 1000);

-- Inspect what DuckDB wrote
SELECT *
FROM parquet_schema('ais_trajectories.parquet');

-- ─────────────────────────────────────────────────────────────────────────────
-- Step 4: analytics directly on the Parquet shard
-- After tgeompointFromBinary() reconstruction, all temporal operators work.
-- ─────────────────────────────────────────────────────────────────────────────

-- Top 10 vessels by total trajectory length (km)
SELECT
    mmsi,
    ping_count,
    round(length(traj),   3)  AS length_km,
    round(maxSpeed(traj), 3)  AS max_speed_mps,
    numInstants(traj)         AS instants
FROM (
    SELECT
        mmsi,
        ping_count,
        tgeompointFromBinary(traj) AS traj
    FROM read_parquet('ais_trajectories.parquet')
)
ORDER BY length_km DESC
LIMIT 10;

-- Vessels that were ever within a 10 km bounding box around Copenhagen
-- (lon 12.57, lat 55.68):
SELECT mmsi, ping_count, round(length(traj), 3) AS length_km
FROM (
    SELECT
        mmsi,
        ping_count,
        tgeompointFromBinary(traj) AS traj
    FROM read_parquet('ais_trajectories.parquet')
)
WHERE eContains(
    stbox 'STBOX XT(((11.5,55.0),(13.5,56.5)),[2026-02-26 00:00:00+00, 2026-02-26 01:00:00+00])',
    traj
)
ORDER BY mmsi;

-- Time-series: average speed per 5-minute slot over the fleet
SELECT
    time_bin,
    round(avg(inst_value), 3) AS avg_speed_mps
FROM (
    SELECT
        date_trunc('minute', t.ts) -
            INTERVAL (extract(minute FROM t.ts)::INT % 5) MINUTE AS time_bin,
        t.value AS inst_value
    FROM (
        SELECT tgeompointFromBinary(traj) AS traj
        FROM read_parquet('ais_trajectories.parquet')
    ) v,
    LATERAL (
        SELECT ts, value
        FROM unnest(
            getTimestamps(v.traj),
            list_transform(instants(v.traj),
                           i -> speed(v.traj, getTimestamp(i)))
        ) t(ts, value)
        WHERE value IS NOT NULL
    ) t
)
GROUP BY time_bin
ORDER BY time_bin;
