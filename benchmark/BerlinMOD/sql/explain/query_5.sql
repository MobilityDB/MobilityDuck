.output results/output/explain/query_5.txt

SET memory_limit = '20GB';

/* Slow version
EXPLAIN ANALYZE
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
*/

EXPLAIN ANALYZE
WITH Temp1(Licence1, Trajs) AS (
    SELECT l1.Licence, collect_gs(list(trajectory_gs(t1.Trip)))
    FROM Trips t1, Licences1 l1
    WHERE t1.VehicleId = l1.VehicleId
    GROUP BY l1.Licence ),
Temp2(Licence2, Trajs) AS (
    SELECT l2.Licence, collect_gs(list(trajectory_gs(t2.Trip)))
    FROM Trips t2, Licences2 l2
    WHERE t2.VehicleId = l2.VehicleId
    GROUP BY l2.Licence )
SELECT Licence1, Licence2, distance_gs(t1.Trajs, t2.Trajs) AS MinDist
FROM Temp1 t1, Temp2 t2  
ORDER BY Licence1, Licence2;