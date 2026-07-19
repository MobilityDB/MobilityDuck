.mode csv
.output results/output/query_3.csv

-- Query 3: Where have the vehicles with licences from Licences1 been 
-- at each of the instants from Instants1?

EXPLAIN ANALYZE
SELECT DISTINCT l.Licence, i.InstantId, i.Instant AS Instant,
    valueAtTimestamp(t.Trip, i.Instant) AS Location
FROM Trips t, Licences1 l, Instants1 i
WHERE t.VehicleId = l.VehicleId AND t.Trip::tstzspan @> i.Instant
ORDER BY l.Licence, i.InstantId;