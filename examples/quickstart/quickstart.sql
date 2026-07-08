-- quickstart.sql — Edge-to-Cloud Temporal Data Lake demo (no external data needed)
--
-- Demonstrates the full TemporalParquet pipeline with synthetic GPS trajectories:
--   1. Generate 5 vessels × 12 pings from inline VALUES (no CSV required)
--   2. Build tgeogpointSeq trajectories — geodetic WGS-84, length in metres
--   3. Write TemporalParquet shard: asBinary() + temporalFooter() metadata
--   4. Query the shard: length, speed, region intersection, trip duration
--
-- Companion file: quickstart_mobilitydb.sql — same queries on PostgreSQL/MobilityDB.
--
-- ─────────────────────────────────────────────────────────────────────────────
-- PREREQUISITES (build from source required — community extension coming soon)
-- ─────────────────────────────────────────────────────────────────────────────
--
--   git clone --recurse-submodules https://github.com/MobilityDB/MobilityDuck.git
--   cd MobilityDuck
--   make                          # installs vcpkg + MEOS, builds the extension
--                                 # first build ~5-10 min; subsequent builds ~30 s
--
-- Then run this file from the MobilityDuck root:
--   TZ=UTC ./build/release/duckdb -c ".read examples/quickstart/quickstart.sql"
--
-- Or from the examples/quickstart/ directory:
--   TZ=UTC ../../build/release/duckdb :memory: -f quickstart.sql
--
-- The two LOAD lines below assume a local build at ../../build/release/.
-- Adjust the paths if your build directory differs.
-- ─────────────────────────────────────────────────────────────────────────────

LOAD '../../build/release/extension/mobilityduck/mobilityduck.duckdb_extension';
LOAD '../../build/release/extension/parquet/parquet.duckdb_extension';

-- ─────────────────────────────────────────────────────────────────────────────
-- Step 1: generate synthetic pings (no external data needed)
--
-- Five vessels depart from different positions in the North Sea / Kattegat area
-- and move linearly for 12 pings at 10-minute intervals (1h50m total).
-- Coordinates are in WGS-84 decimal degrees.
--
-- Vessel coverage of the Copenhagen bounding box (lon 11.5–13.5, lat 55.0–56.5):
--   1 → approaches from west, enters box around ping 7
--   2 → approaches from east, enters box around ping 3
--   3 → stays in Skagerrak, never enters box        ← useful negative case
--   4 → starts inside box, stays inside throughout
--   5 → approaches from southwest, enters box around ping 10
-- ─────────────────────────────────────────────────────────────────────────────

CREATE OR REPLACE TABLE raw_pings AS
SELECT
    entity_id,
    round(start_lon + delta_lon * step, 6) AS lon,
    round(start_lat + delta_lat * step, 6) AS lat,
    -- to_timestamp(unix_epoch) avoids TIMESTAMPTZ+INTERVAL operator ambiguity
    -- 1768464000 = 2026-01-15 08:00:00 UTC
    to_timestamp(1768464000 + step * 600)  AS ts
FROM (VALUES
    -- entity_id  start_lon  start_lat  delta_lon  delta_lat
    (1,  10.00,   55.50,   0.23,   0.05),
    (2,  14.00,   56.00,  -0.18,  -0.08),
    (3,   8.50,   57.50,   0.06,  -0.06),
    (4,  12.10,   55.20,   0.04,   0.02),
    (5,   9.50,   54.50,   0.22,   0.06)
) t(entity_id, start_lon, start_lat, delta_lon, delta_lat),
generate_series(0, 11) g(step);

SELECT entity_id, count(*) AS pings FROM raw_pings GROUP BY entity_id ORDER BY entity_id;

-- ─────────────────────────────────────────────────────────────────────────────
-- Step 2: build tgeogpointSeq trajectories
--
-- TGEOGPOINT(geometry, timestamptz) creates a geodetic instant.
-- tgeogpointSeq(list(... ORDER BY ts)) assembles them into a linear sequence.
-- The geodetic flag means length() and speed() return metres and m/s.
-- ─────────────────────────────────────────────────────────────────────────────

CREATE OR REPLACE TABLE trajectories AS
SELECT
    entity_id,
    tgeogpointSeq(
        list(TGEOGPOINT(ST_Point(lon, lat), ts) ORDER BY ts)
    ) AS traj
FROM raw_pings
GROUP BY entity_id;

SELECT count(*) AS trajectories FROM trajectories;

-- ─────────────────────────────────────────────────────────────────────────────
-- Step 3: write TemporalParquet shard
--
-- asBinary()         → portable MEOS-WKB BLOB (BYTE_ARRAY in Parquet)
-- temporalFooter()   → TemporalParquet JSON metadata injected via KV_METADATA
--
-- Any MEOS-WKB-aware reader (MobilityDB, MobilitySpark, PyMEOS) can decode
-- the traj column using the base_type declared in the footer.
-- ─────────────────────────────────────────────────────────────────────────────

COPY (
    SELECT
        entity_id,
        asBinary(traj)    AS traj,
        numInstants(traj) AS ping_count
    FROM trajectories
)
TO 'edge_to_cloud_demo.parquet' (
    FORMAT    PARQUET,
    ROW_GROUP_SIZE 1000,
    KV_METADATA {'temporal': temporalFooter(MAP {'traj': 'tgeogpoint'})}
);

-- Verify the Parquet schema: traj must land as BYTE_ARRAY
SELECT name, type
FROM parquet_schema('edge_to_cloud_demo.parquet')
WHERE name NOT IN ('duckdb_schema');

-- Verify the TemporalParquet footer is embedded correctly
SELECT value = temporalFooter(MAP {'traj': 'tgeogpoint'})::BLOB AS footer_ok
FROM parquet_kv_metadata('edge_to_cloud_demo.parquet')
WHERE key = 'temporal'::BLOB;

-- ─────────────────────────────────────────────────────────────────────────────
-- Step 4: analytics on the Parquet shard
--
-- All queries use tgeogpointFromBinary() to reconstruct the typed value from
-- the BLOB column.  The same named-function queries run unchanged on
-- MobilityDB and MobilitySpark — see quickstart_mobilitydb.sql.
-- ─────────────────────────────────────────────────────────────────────────────

-- Query A: total distance and maximum speed per vessel
-- length() returns geodetic metres (spheroidal WGS-84 via Vincenty/Haversine)
-- speed() returns a tfloat of instantaneous speed in m/s; maxValue() extracts the peak
SELECT
    entity_id,
    ping_count,
    round(length(tgeogpointFromBinary(traj)))             AS length_m,
    round(maxValue(speed(tgeogpointFromBinary(traj))), 2) AS max_speed_ms
FROM read_parquet('edge_to_cloud_demo.parquet')
ORDER BY length_m DESC;

-- Query B: vessels that entered the Copenhagen bounding box
-- lon 11.5–13.5, lat 55.0–56.5  (approx. Øresund / Danish straits region)
-- eIntersects returns true if the trajectory ever enters the polygon.
-- DuckDB GEOMETRY is automatically promoted to geodetic when matched against
-- a tgeogpoint, so no SRID annotation on the polygon is required.
SELECT entity_id
FROM (
    SELECT entity_id, tgeogpointFromBinary(traj) AS traj
    FROM read_parquet('edge_to_cloud_demo.parquet')
)
WHERE eIntersects(
    ST_GeomFromText('POLYGON((11.5 55.0,13.5 55.0,13.5 56.5,11.5 56.5,11.5 55.0))'),
    traj
)
ORDER BY entity_id;

-- Query C: trip duration (timezone-independent interval)
SELECT
    entity_id,
    duration(tgeogpointFromBinary(traj))::VARCHAR AS trip_duration
FROM read_parquet('edge_to_cloud_demo.parquet')
ORDER BY entity_id;
