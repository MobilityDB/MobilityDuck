-- Query 6: What are the pairs of licence plate numbers of “trucks”
-- that have ever been as close as 10m or less to each other?

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