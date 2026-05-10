-- generic_ingest.sql — TemporalParquet ingest template (bring your own data)
--
-- Converts any lon/lat/timestamp CSV into a TemporalParquet shard.
-- Edit the CONFIGURE macros below, then run from the MobilityDuck root:
--   TZ=UTC ./build/release/duckdb -c ".read examples/generic-ingest/generic_ingest.sql"
--
-- Output: a self-describing Parquet file with MEOS-WKB trajectory column and
-- TemporalParquet footer metadata, readable by MobilityDB, MobilitySpark, PyMEOS.
--
-- ─────────────────────────────────────────────────────────────────────────────
-- PREREQUISITES (build from source required — community extension coming soon)
-- ─────────────────────────────────────────────────────────────────────────────
--
--   git clone --recurse-submodules https://github.com/MobilityDB/MobilityDuck.git
--   cd MobilityDuck
--   make                          # installs vcpkg + MEOS, builds the extension
--
-- The two LOAD lines below assume a local build at ../../build/release/.
-- ─────────────────────────────────────────────────────────────────────────────

LOAD '../../build/release/extension/mobilityduck/mobilityduck.duckdb_extension';
LOAD '../../build/release/extension/parquet/parquet.duckdb_extension';

-- ─────────────────────────────────────────────────────────────────────────────
-- CONFIGURE: data source
-- ─────────────────────────────────────────────────────────────────────────────

-- Path to your CSV file (wildcards accepted: 'data/*.csv')
-- The CSV must have a header row.
CREATE OR REPLACE MACRO csv_path() AS 'your_data.csv';

-- Output Parquet shard path
CREATE OR REPLACE MACRO output_path() AS 'trajectories.parquet';

-- ─────────────────────────────────────────────────────────────────────────────
-- CONFIGURE: column mapping
-- Replace these macro bodies with your actual column names.
-- ─────────────────────────────────────────────────────────────────────────────

-- Column in your CSV that uniquely identifies each moving object
-- (vessel MMSI, vehicle ID, user ID, sensor tag, …)
CREATE OR REPLACE MACRO col_entity_id() AS 'entity_id';

-- Column containing longitude in WGS-84 decimal degrees (−180 … 180)
CREATE OR REPLACE MACRO col_lon() AS 'longitude';

-- Column containing latitude in WGS-84 decimal degrees (−90 … 90)
CREATE OR REPLACE MACRO col_lat() AS 'latitude';

-- Column containing the observation timestamp
-- DuckDB parses ISO-8601, Unix epoch (integer), and most common formats.
CREATE OR REPLACE MACRO col_ts() AS 'timestamp';

-- Minimum number of pings per entity to include in output (filters sparse tracks)
CREATE OR REPLACE MACRO min_pings() AS 3;

-- ─────────────────────────────────────────────────────────────────────────────
-- Step 1: load and validate raw pings
--
-- Drops rows with out-of-range coordinates and deduplicates (entity, ts) pairs
-- (common in AIS/GPS feeds that emit duplicate messages at the same timestamp).
-- ─────────────────────────────────────────────────────────────────────────────

CREATE OR REPLACE TABLE raw_pings AS
SELECT
    CAST(columns(col_entity_id()) AS BIGINT)       AS entity_id,
    CAST(columns(col_lon())       AS DOUBLE)        AS lon,
    CAST(columns(col_lat())       AS DOUBLE)        AS lat,
    CAST(columns(col_ts())        AS TIMESTAMPTZ)   AS ts
FROM read_csv_auto(csv_path(), header = true, nullstr = '')
WHERE TRY_CAST(columns(col_lon()) AS DOUBLE) BETWEEN -180 AND  180
  AND TRY_CAST(columns(col_lat()) AS DOUBLE) BETWEEN  -90 AND   90
QUALIFY ROW_NUMBER() OVER (
    PARTITION BY CAST(columns(col_entity_id()) AS BIGINT),
                 CAST(columns(col_ts())        AS TIMESTAMPTZ)
    ORDER BY     CAST(columns(col_ts())        AS TIMESTAMPTZ)
) = 1;

SELECT count(*) AS raw_pings, count(DISTINCT entity_id) AS entities FROM raw_pings;

-- ─────────────────────────────────────────────────────────────────────────────
-- Step 2: build tgeogpointSeq trajectories
--
-- One geodetic sequence per entity, ordered by timestamp.
-- Entities with fewer than min_pings() observations are excluded.
-- ─────────────────────────────────────────────────────────────────────────────

CREATE OR REPLACE TABLE trajectories AS
SELECT
    entity_id,
    tgeogpointSeq(
        list(TGEOGPOINT(ST_Point(lon, lat), ts) ORDER BY ts)
    ) AS traj
FROM raw_pings
GROUP BY entity_id
HAVING count(*) >= min_pings();

SELECT count(*) AS trajectories FROM trajectories;

-- ─────────────────────────────────────────────────────────────────────────────
-- Step 3: write TemporalParquet shard
--
-- The TemporalParquet footer (KV_METADATA 'temporal') declares traj as a
-- tgeogpoint column encoded with MEOS-WKB.  Any MEOS-aware reader can
-- reconstruct the typed value from the BYTE_ARRAY column without a schema file.
-- ─────────────────────────────────────────────────────────────────────────────

COPY (
    SELECT
        entity_id,
        asBinary(traj)    AS traj,
        numInstants(traj) AS ping_count
    FROM trajectories
)
TO output_path() (
    FORMAT    PARQUET,
    ROW_GROUP_SIZE 1000,
    KV_METADATA {'temporal': temporalFooter(MAP {'traj': 'tgeogpoint'})}
);

-- Verify schema: traj must appear as BYTE_ARRAY
SELECT name, type FROM parquet_schema(output_path())
WHERE name NOT IN ('duckdb_schema');

-- Verify footer
SELECT value = temporalFooter(MAP {'traj': 'tgeogpoint'})::BLOB AS footer_ok
FROM parquet_kv_metadata(output_path())
WHERE key = 'temporal'::BLOB;

-- ─────────────────────────────────────────────────────────────────────────────
-- Step 4: quick sanity analytics on the written shard
-- These same queries run unchanged on MobilityDB and MobilitySpark.
-- ─────────────────────────────────────────────────────────────────────────────

-- Top 10 entities by geodetic trajectory length (metres)
SELECT
    entity_id,
    ping_count,
    round(length(tgeogpointFromBinary(traj)))             AS length_m,
    round(maxValue(speed(tgeogpointFromBinary(traj))), 2) AS max_speed_ms
FROM read_parquet(output_path())
ORDER BY length_m DESC
LIMIT 10;

-- Distribution of ping counts
SELECT ping_count, count(*) AS entities
FROM read_parquet(output_path())
GROUP BY ping_count
ORDER BY ping_count;
