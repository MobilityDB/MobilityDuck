-- Query 15: Which vehicles passed a point from Points1 during a period 
-- from Periods1?

WITH Temp AS (
    SELECT DISTINCT pt.PointId, pt.Geom, pr.PeriodId, pr.Period, t.VehicleId
    FROM Trips t, Points1 pt, Periods1 pr
    WHERE t.Trip && stbox(pt.Geom, pr.Period)
    AND ST_Intersects(trajectory(atTime(t.Trip, pr.Period)), pt.Geom) )
SELECT DISTINCT t.PointId, t.Geom, t.PeriodId, t.Period, v.Licence  
FROM Temp t, Vehicles v
WHERE t.VehicleId = v.VehicleId 
ORDER BY t.PointId, t.PeriodId, v.Licence;