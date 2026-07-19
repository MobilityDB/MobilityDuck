-- Query 13: Which vehicles travelled within one of the regions from 
-- Regions1 during the periods from Periods1?    

WITH Temp AS (
    SELECT DISTINCT r.RegionId, p.PeriodId, p.Period, t.VehicleId
    FROM Trips t, Regions1 r, Periods1 p
    WHERE
        COALESCE(eEq(r.Geom_h3set, t.Trip_h3), TRUE)
        AND t.trip && stbox(r.Geom, p.Period)
        AND ST_Intersects(trajectory(atTime(t.Trip, p.Period)), r.Geom)
    ORDER BY r.RegionId, p.PeriodId )
SELECT DISTINCT t.RegionId, t.PeriodId, t.Period, v.Licence
FROM Temp t, Vehicles v
WHERE t.VehicleId = v.VehicleId
ORDER BY t.RegionId, t.PeriodId, v.Licence;