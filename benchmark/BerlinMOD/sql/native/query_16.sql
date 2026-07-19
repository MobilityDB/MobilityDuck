.mode csv
.output results/output/query_16.csv

-- Query 16: List the pairs of licences for vehicles, the first from 
-- Licences1, the second from Licences2, where the corresponding 
-- vehicles are both present within a region from Regions1 during a 
-- period from QueryPeriod1, but do not meet each other there and then.

EXPLAIN ANALYZE
SELECT p.PeriodId, p.Period, r.RegionId, l1.Licence AS Licence1, l2.Licence AS Licence2
FROM Trips t1, Licences1 l1, Trips t2, Licences2 l2, Periods1 p, Regions1 r
WHERE
    t1.VehicleId = l1.VehicleId
    AND t2.VehicleId = l2.VehicleId
    AND l1.Licence < l2.Licence
    -- AND t1.Trip && stbox(r.Geom::WKB_BLOB, p.Period)
    -- AND t2.Trip && stbox(r.Geom::WKB_BLOB, p.Period)
    AND ST_Intersects(trajectory(atTime(t1.Trip, p.Period)), r.Geom)
    AND ST_Intersects(trajectory(atTime(t2.Trip, p.Period)), r.Geom)
    AND aDisjoint(atTime(t1.Trip, p.Period), atTime(t2.Trip, p.Period))
ORDER BY PeriodId, RegionId, Licence1, Licence2;