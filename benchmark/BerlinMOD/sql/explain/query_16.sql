.output results/output/explain/query_16.txt

EXPLAIN ANALYZE
SELECT p.PeriodId, p.Period, r.RegionId, l1.Licence AS Licence1, l2.Licence AS Licence2
FROM Trips t1, Licences1 l1, Trips t2, Licences2 l2, Periods1 p, Regions1 r
WHERE
    t1.VehicleId = l1.VehicleId
    AND t2.VehicleId = l2.VehicleId
    AND l1.Licence < l2.Licence
    -- AND t1.Trip && stbox(r.Geom::WKB_BLOB, p.Period)
    -- AND t2.Trip && stbox(r.Geom::WKB_BLOB, p.Period)
    AND ST_Intersects(trajectory(atTime(t1.Trip, p.Period))::GEOMETRY, r.Geom)
    AND ST_Intersects(trajectory(atTime(t2.Trip, p.Period))::GEOMETRY, r.Geom)
    AND aDisjoint(atTime(t1.Trip, p.Period), atTime(t2.Trip, p.Period))
ORDER BY PeriodId, RegionId, Licence1, Licence2;