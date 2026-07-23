-- Query 14: Which vehicles travelled within one of the regions from 
-- Regions1 at one of the instants from Instants1?

WITH Temp AS (
    SELECT DISTINCT r.RegionId, i.InstantId, i.Instant, t.VehicleId
    FROM Trips t, Regions1 r, Instants1 i
    WHERE
        t.Trip && stbox(r.Geom, i.Instant)
        AND ST_Contains(r.Geom, valueAtTimestamp(t.Trip, i.Instant)) )
SELECT DISTINCT t.RegionId, t.InstantId, t.Instant, v.Licence
FROM Temp t JOIN Vehicles v ON t.VehicleId = v.VehicleId 
ORDER BY t.RegionId, t.InstantId, v.Licence;