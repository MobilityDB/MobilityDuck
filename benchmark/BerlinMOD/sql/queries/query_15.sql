.mode csv
.output results/output/query_15.csv

/* Old version
SELECT DISTINCT pt.PointId, pt.Geom, pr.PeriodId, pr.Period, v.Licence
FROM Trips t, Vehicles v, Points1 pt, Periods1 pr
WHERE
    t.VehicleId = v.VehicleId
    AND t.Trip && stbox(pt.Geom::WKB_BLOB, pr.Period)
    AND ST_Intersects(trajectory(atTime(t.Trip, pr.Period))::GEOMETRY, pt.Geom)
ORDER BY pt.PointId, pr.PeriodId, v.Licence;
*/

WITH Temp AS (
    SELECT DISTINCT pt.PointId, pt.Geom, pr.PeriodId, pr.Period, t.VehicleId
    FROM Trips t, Points1 pt, Periods1 pr
    WHERE t.Trip && stbox(pt.Geom::WKB_BLOB, pr.Period)
    AND ST_Intersects(trajectory(atTime(t.Trip, pr.Period))::GEOMETRY, pt.Geom) )
SELECT DISTINCT t.PointId, t.Geom, t.PeriodId, t.Period, v.Licence  
FROM Temp t, Vehicles v
WHERE t.VehicleId = v.VehicleId 
ORDER BY t.PointId, t.PeriodId, v.Licence;