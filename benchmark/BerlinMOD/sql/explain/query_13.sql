.output results/output/explain/query_13.txt

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

EXPLAIN ANALYZE
WITH Temp AS (
    SELECT DISTINCT r.RegionId, p.PeriodId, p.Period, t.VehicleId
    FROM Trips t, Regions1 r, Periods1 p
    WHERE
        t.trip && stbox(r.Geom::WKB_BLOB, p.Period)
        AND ST_Intersects(trajectory(atTime(t.Trip, p.Period))::GEOMETRY, r.Geom)
    ORDER BY r.RegionId, p.PeriodId )
SELECT DISTINCT t.RegionId, t.PeriodId, t.Period, v.Licence
FROM Temp t, Vehicles v
WHERE t.VehicleId = v.VehicleId 
ORDER BY t.RegionId, t.PeriodId, v.Licence;