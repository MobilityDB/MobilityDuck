.output results/output/explain/query_13.txt

EXPLAIN ANALYZE
SELECT DISTINCT r.RegionId, p.PeriodId, p.Period, v.Licence
FROM Trips t, Vehicles v, Regions1 r, Periods1 p
WHERE
    t.VehicleId = v.VehicleId
    AND t.trip && stbox(r.Geom::WKB_BLOB, p.Period)
    AND ST_Intersects(trajectory(atTime(t.Trip, p.Period))::GEOMETRY, r.Geom)
ORDER BY r.RegionId, p.PeriodId, v.Licence;