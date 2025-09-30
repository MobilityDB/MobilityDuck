.mode csv
.output results/output/query_4.csv

SELECT DISTINCT p.PointId, p.Geom, v.Licence
FROM Trips t, Vehicles v, Points p
WHERE
    t.VehicleId = v.VehicleId
    AND ST_Intersects(trajectory(t.Trip)::GEOMETRY, p.Geom)
ORDER BY p.PointId, v.Licence;