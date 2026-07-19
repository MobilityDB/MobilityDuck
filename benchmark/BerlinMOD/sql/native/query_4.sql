.mode csv
.output results/output/query_4.csv

-- Query 4: Which vehicles have passed the points from Points?

EXPLAIN ANALYZE
SELECT DISTINCT p.PointId, p.Geom, v.Licence
FROM Trips t, Vehicles v, Points p
WHERE
    t.VehicleId = v.VehicleId
    AND ST_Intersects(trajectory(t.Trip), p.Geom)
ORDER BY p.PointId, v.Licence;