-- Query 12: Which vehicles met at a point from Points1 at an instant 
-- from Instants1?

WITH Temp AS (
    SELECT DISTINCT p.PointId, p.Geom, i.InstantId, i.Instant, t.VehicleId
    FROM Trips t, Points1 p, Instants1 i
    WHERE t.Trip @> stbox(p.Geom, i.Instant)
    AND valueAtTimestamp(t.Trip, i.Instant) = p.Geom )
SELECT DISTINCT t1.PointId, t1.Geom, t1.InstantId, t1.Instant, 
    v1.Licence AS Licence1, v2.Licence AS Licence2
FROM Temp t1
    JOIN Vehicles v1 ON t1.VehicleId = v1.VehicleId
    JOIN Temp t2 ON t1.VehicleId < t2.VehicleId
        AND t1.PointID = t2.PointID
        AND t1.InstantId = t2.InstantId
    JOIN Vehicles v2 ON t2.VehicleId = v2.VehicleId
ORDER BY t1.PointId, t1.InstantId, v1.Licence, v2.Licence;