-- Query 11: Which vehicles passed a point from Points1 at one of the 
-- instants from Instants1?

WITH Temp AS (
    SELECT p.PointId, p.Geom, i.InstantId, i.Instant, t.VehicleId
    FROM Trips t, Points1 p, Instants1 i
    WHERE
        t.Trip @> stbox(p.Geom, i.Instant)
        AND valueAtTimestamp(t.Trip, i.Instant) = p.Geom )
SELECT t.PointId, t.Geom, t.InstantId, t.Instant, v.Licence
FROM Temp t JOIN Vehicles v ON t.VehicleId = v.VehicleId
ORDER BY t.PointId, t.InstantId, v.Licence;