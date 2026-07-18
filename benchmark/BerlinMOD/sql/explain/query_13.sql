.output results/output/explain/query_13.txt

SET memory_limit = '20GB';

/* Old version
EXPLAIN ANALYZE
SELECT DISTINCT r.RegionId, p.PeriodId, p.Period, v.Licence
FROM Trips t, Vehicles v, Regions1 r, Periods1 p
WHERE
    t.VehicleId = v.VehicleId
    AND t.trip && stbox(r.Geom::WKB_BLOB, p.Period)
    AND ST_Intersects(trajectory(atTime(t.Trip, p.Period))::GEOMETRY, r.Geom)
ORDER BY r.RegionId, p.PeriodId, v.Licence;
*/

-- WITH Temp AS (
--     SELECT DISTINCT r.RegionId, p.PeriodId, p.Period, t.VehicleId
--     FROM Trips t, Regions1 r, Periods1 p
--     WHERE
--         t.trip && stbox(r.Geom::WKB_BLOB, p.Period)
--         AND ST_Intersects(trajectory(atTime(t.Trip, p.Period))::GEOMETRY, r.Geom)
--     ORDER BY r.RegionId, p.PeriodId )
-- SELECT DISTINCT t.RegionId, t.PeriodId, t.Period, v.Licence
-- FROM Temp t, Vehicles v
-- WHERE t.VehicleId = v.VehicleId 
-- ORDER BY t.RegionId, t.PeriodId, v.Licence;
EXPLAIN ANALYZE
WITH Temp AS (
  SELECT DISTINCT r.regionId, p.periodId, p.period, t.vehicleid
  FROM   Trips t, Regions r, Periods p
  WHERE  r.regionId <= 10 AND p.periodId <= 10
    AND  t.trip && stbox(r.geom, p.period)
    AND  eIntersects(atTime(t.trip, p.period), r.geom)
)
SELECT DISTINCT t.regionId, t.periodId, t.period, v.licence
FROM   Temp t, Vehicles v
WHERE  t.vehicleid = v.vehicleid
ORDER  BY t.regionId, t.periodId, v.licence;