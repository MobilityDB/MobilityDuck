.output results/output/explain/query_14.txt

SET memory_limit = '20GB';

/* Old version
EXPLAIN ANALYZE
SELECT DISTINCT r.RegionId, i.InstantId, i.Instant, v.Licence
FROM Trips t, Vehicles v, Regions1 r, Instants1 i
WHERE
    t.VehicleId = v.VehicleId
    AND t.Trip && stbox(r.Geom::WKB_BLOB, i.Instant)
    AND ST_Contains(r.Geom, valueAtTimestamp(t.Trip, i.Instant)::GEOMETRY)
ORDER BY r.RegionId, i.InstantId, v.Licence;
*/

-- WITH Temp AS (
--     SELECT DISTINCT r.RegionId, i.InstantId, i.Instant, t.VehicleId
--     FROM Trips t, Regions1 r, Instants1 i
--     WHERE
--         t.Trip && stbox(r.Geom::WKB_BLOB, i.Instant)
--         AND ST_Contains(r.Geom, valueAtTimestamp(t.Trip, i.Instant)::GEOMETRY) )
-- SELECT DISTINCT t.RegionId, t.InstantId, t.Instant, v.Licence
-- FROM Temp t JOIN Vehicles v ON t.VehicleId = v.VehicleId 
-- ORDER BY t.RegionId, t.InstantId, v.Licence;
EXPLAIN ANALYZE
WITH Temp AS (
  SELECT DISTINCT r.regionId, i.instantId, i.instant, t.vehicleid
  FROM   Trips t, Regions r, Instants i
  WHERE  t.trip && stbox(r.geom, i.instant)
    AND  ST_Contains(r.geom, valueAtTimestamp(t.trip, i.instant))
)
SELECT DISTINCT t.regionId, t.instantId, t.instant, v.licence
FROM   Temp t
JOIN   Vehicles v ON t.vehicleid = v.vehicleid
ORDER  BY t.regionId, t.instantId, v.licence;