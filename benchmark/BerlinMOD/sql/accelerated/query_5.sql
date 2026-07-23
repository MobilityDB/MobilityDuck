-- Query 5: What is the minimum distance between places, where a vehicle with a 
-- licence from Licences1 and a vehicle with a licence from Licences2 
-- have been?

SELECT l1.Licence AS Licence1, l2.Licence AS Licence2,
    minDistanceAgg(t1.Trip, t2.Trip) AS MinDist
FROM Trips t1, Licences1 l1, Trips t2, Licences2 l2
WHERE t1.VehicleId = l1.VehicleId AND t2.VehicleId = l2.VehicleId
GROUP BY l1.Licence, l2.Licence
ORDER BY l1.Licence, l2.Licence;