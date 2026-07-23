-- Query 17: Which point(s) from Points have been visited by a 
-- maximum number of different vehicles?

WITH PointCount AS (
    SELECT p.PointId, COUNT(DISTINCT t.VehicleId) AS Hits
    FROM Trips t, Points p
    WHERE COALESCE(eEq(p.geom_h3cell, t.Trip_h3), TRUE)
    AND ST_Intersects(trajectory(t.Trip), p.Geom)
    GROUP BY p.PointId )
SELECT PointId, Hits
FROM PointCount AS p
WHERE p.Hits = ( SELECT MAX(Hits) FROM PointCount );