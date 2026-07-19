-- Query 7: What are the licence plate numbers of the passenger cars that have 
-- reached the points from Points first of all passenger cars during the
-- complete observation period?

WITH Temp AS (
    SELECT DISTINCT v.Licence, p.PointId, p.Geom,
        MIN(startTimestamp(atValues(t.Trip,p.Geom))) AS Instant
    FROM Trips t, Vehicles v, Points1 p
    WHERE
        t.VehicleId = v.VehicleId
        AND v.VehicleType = 'passenger'
        AND COALESCE(eEq(p.geom_h3cell, t.Trip_h3), TRUE)
        AND ST_Intersects(trajectory(t.Trip), p.Geom)
    GROUP BY v.Licence, p.PointId, p.Geom )
SELECT t1.Licence, t1.PointId, t1.Geom, t1.Instant
FROM Temp t1
WHERE t1.Instant <= ALL (
    SELECT t2.Instant
    FROM Temp t2
    WHERE t1.PointId = t2.PointId )
ORDER BY t1.PointId, t1.Licence;