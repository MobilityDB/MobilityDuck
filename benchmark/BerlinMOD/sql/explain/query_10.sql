.output results/output/explain/query_10.txt

SET memory_limit = '20GB';

/* Old version
EXPLAIN ANALYZE
WITH Values AS (
    SELECT DISTINCT l1.Licence AS QueryLicence, l2.Licence AS OtherLicence,
        atTime(t1.Trip, getTime(atValues(tdwithin(t1.Trip, t2.Trip, 3.0), TRUE))) AS Pos
    FROM Trips t1, Licences1 l1, Trips t2, Licences2 l2
    WHERE t1.VehicleId = l1.VehicleId AND t2.VehicleId = l2.VehicleId AND
        t1.VehicleId < t2.VehicleId AND
        expandSpace(t1.Trip::STBOX, 3) && expandSpace(t2.Trip::STBOX, 3) AND
        eDwithin(t1.Trip, t2.Trip, 3.0) )
SELECT QueryLicence, OtherLicence, array_agg(Pos ORDER BY startTimestamp(Pos)) AS Pos
FROM Values
GROUP BY QueryLicence, OtherLicence
ORDER BY QueryLicence, OtherLicence;
*/

-- WITH Temp AS (
--     SELECT l1.Licence AS Licence1, t2.VehicleId AS Car2Id,
--     whenTrue(tDwithin(t1.Trip, t2.Trip, 3.0)) AS Periods
--     FROM Trips t1, Licences1 l1, Trips t2, Vehicles v
--     WHERE t1.VehicleId = l1.VehicleId AND t2.VehicleId = v.VehicleId AND
--     t1.VehicleId <> t2.VehicleId AND t2.Trip && expandSpace(t1.trip::STBOX, 3.0) )
-- SELECT Licence1, Car2Id, Periods
-- FROM Temp
-- WHERE Periods IS NOT NULL;
EXPLAIN ANALYZE
WITH LicTrips AS (
  SELECT array_agg(t1.trip)   AS trips,
         array_agg(l.licence) AS lic,
         array_agg(t1.vehicleid)  AS veh
  FROM   Licences l
  JOIN   Vehicles v1 ON v1.licence = l.licence
  JOIN   Trips    t1 ON t1.vehicleid   = v1.vehicleid ),
AllTrips AS (
  SELECT array_agg(t2.trip)  AS trips,
         array_agg(t2.vehicleid) AS veh
  FROM   Trips t2 )
SELECT a.lic[p.pair.i] AS licence1, b.veh[p.pair.j] AS car2Id, p.pair.periods AS periods
FROM   LicTrips a, AllTrips b,
       UNNEST(tDwithinPairs(a.trips, b.trips, 3.0)) AS p(pair)
WHERE  a.veh[p.pair.i] <> b.veh[p.pair.j]
ORDER  BY licence1, car2Id;