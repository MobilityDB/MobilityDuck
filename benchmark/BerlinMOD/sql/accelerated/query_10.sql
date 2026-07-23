-- Query 10: When and where did the vehicles with licence plate numbers from 
-- Licences1 meet other vehicles (distance < 3m) and what are the latter
-- licences?

.mode csv
.output results/output/benchmark/accelerated/query_10_modified.csv
SET memory_limit = '20GB';
PRAGMA enable_profiling = 'json';

-- WITH Temp AS (
--     SELECT l1.Licence AS Licence1, t2.VehicleId AS Car2Id,
--         whenTrue(tDwithin(t1.Trip, t2.Trip, 3.0)) AS Periods
--     FROM Trips t1, Licences1 l1, Trips t2, Vehicles v
--     WHERE t1.VehicleId = l1.VehicleId AND t2.VehicleId = v.VehicleId AND
--         t1.VehicleId <> t2.VehicleId AND t2.Trip && expandSpace(t1.trip::STBOX, 3.0) )
-- SELECT Licence1, Car2Id, Periods
-- FROM Temp
-- WHERE Periods IS NOT NULL;

PRAGMA profiling_output = 'results/output/benchmark/accelerated/query_10_modified.create.profile.json';
CREATE OR REPLACE TEMP TABLE Temp10 AS
SELECT l1.Licence AS Licence1, t2.VehicleId AS Car2Id,
    whenTrue(tDwithin(t1.Trip, t2.Trip, 3.0)) AS Periods
FROM Trips t1, Licences1 l1, Trips t2, Vehicles v
WHERE t1.VehicleId = l1.VehicleId AND t2.VehicleId = v.VehicleId AND
    t1.VehicleId <> t2.VehicleId AND t2.Trip && expandSpace(t1.trip::STBOX, 3.0);

PRAGMA profiling_output = 'results/output/benchmark/accelerated/query_10_modified.select.profile.json';
SELECT Licence1, Car2Id, Periods FROM Temp10 WHERE Periods IS NOT NULL;