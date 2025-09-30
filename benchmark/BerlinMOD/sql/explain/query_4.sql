.output results/output/explain/query_4.txt

EXPLAIN ANALYZE
SELECT DISTINCT p.PointId, p.Geom, v.Licence
FROM Trips t, Vehicles v, Points p
WHERE
    t.VehicleId = v.VehicleId
    AND ST_Intersects(trajectory(t.Trip)::GEOMETRY, p.Geom)
ORDER BY p.PointId, v.Licence;