.mode csv
.output results/output/query_5.csv

SELECT l1.Licence AS Licence1, l2.Licence AS Licence2,
    MIN(ST_Distance(trajectory(t1.Trip)::GEOMETRY,
    trajectory(t2.Trip)::GEOMETRY)) AS MinDist
FROM Trips t1, Licences1 l1, Trips t2, Licences2 l2
WHERE
    t1.VehicleId = l1.VehicleId
    AND t2.VehicleId = l2.VehicleId
    AND t1.VehicleId < t2.VehicleId
GROUP BY l1.Licence, l2.Licence
ORDER BY l1.Licence, l2.Licence;