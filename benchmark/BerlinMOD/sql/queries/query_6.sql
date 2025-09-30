.mode csv
.output results/output/query_6.csv

/* SLOWER VERSION

SELECT DISTINCT v1.Licence AS Licence1, v2.Licence AS Licence2
FROM Trips t1, Vehicles v1, Trips t2, Vehicles v2
WHERE
    t1.VehicleId = v1.VehicleId
    AND t2.VehicleId = v2.VehicleId
    AND t1.VehicleId < t2.VehicleId
    AND v1.VehicleType = 'truck'
    AND v2.VehicleType = 'truck'
    AND t1.Trip && expandSpace(t2.Trip::STBOX, 10)
    AND eDwithin(t1.Trip, t2.Trip, 10.0)
ORDER BY v1.Licence, v2.Licence;

*/

WITH Temp(Licence, VehicleId, Trip) AS (
    SELECT v.Licence, t.VehicleId, t.Trip
    FROM Trips t, Vehicles v
    WHERE t.VehicleId = v.VehicleId 
    AND v.VehicleType = 'truck' )
SELECT t1.Licence, t2.Licence
FROM Temp t1, Temp t2
WHERE t1.VehicleId < t2.VehicleId 
    AND t1.Trip && expandSpace(t2.Trip::STBOX, 10) 
    AND eDwithin(t1.Trip, t2.Trip, 10.0)
ORDER BY t1.Licence, t2.Licence;