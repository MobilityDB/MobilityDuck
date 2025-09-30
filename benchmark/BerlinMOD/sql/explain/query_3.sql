.output results/output/explain/query_3.txt

EXPLAIN ANALYZE
SELECT DISTINCT l.Licence, i.InstantId, i.Instant AS Instant,
    valueAtTimestamp(t.Trip, i.Instant)::GEOMETRY AS Pos
FROM Trips t, Licences1 l, Instants1 i
WHERE t.VehicleId = l.VehicleId AND t.Trip::tstzspan @> i.Instant
ORDER BY l.Licence, i.InstantId;