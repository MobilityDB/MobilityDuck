.output results/output/explain/query_3.txt

SET memory_limit = '20GB';

-- SELECT DISTINCT l.Licence, i.InstantId, i.Instant AS Instant,
--     valueAtTimestamp(t.Trip, i.Instant)::GEOMETRY AS Pos
-- FROM Trips t, Licences1 l, Instants1 i
-- WHERE t.VehicleId = l.VehicleId AND t.Trip::tstzspan @> i.Instant
-- ORDER BY l.Licence, i.InstantId;

EXPLAIN ANALYZE
SELECT v.vehicleid     AS vehid,
       v.licence,
       i.instantId AS instantid,
       asHexWKB(atTime(t.trip, i.instant)) AS pos
FROM   Licences l
JOIN   Vehicles v  ON  v.licence = l.licence
JOIN   Trips    t  ON  t.vehicleid   = v.vehicleid
JOIN   Instants i ON true
WHERE  atTime(t.trip, i.instant) IS NOT NULL
ORDER  BY v.vehicleid, i.instantId;