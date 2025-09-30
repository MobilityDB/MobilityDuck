.output results/output/explain/query_11.txt

/* Old version
EXPLAIN ANALYZE
SELECT p.PointId, p.Geom, i.InstantId, i.Instant, v.Licence
FROM Trips t, Vehicles v, Points1 p, Instants1 i
WHERE
    t.VehicleId = v.VehicleId
    AND t.Trip @> stbox(p.Geom::WKB_BLOB, i.Instant)
    AND valueAtTimestamp(t.Trip, i.Instant) = p.Geom
ORDER BY p.PointId, i.InstantId, v.Licence;
*/

EXPLAIN ANALYZE
WITH Temp AS (
    SELECT p.PointId, p.Geom, i.InstantId, i.Instant, t.VehicleId
    FROM Trips t, Points1 p, Instants1 i
    WHERE
        t.Trip @> stbox(p.Geom::WKB_BLOB, i.Instant)
        AND valueAtTimestamp(t.Trip, i.Instant) = p.Geom )
SELECT t.PointId, t.Geom, t.InstantId, t.Instant, v.Licence
FROM Temp t JOIN Vehicles v ON t.VehicleId = v.VehicleId
ORDER BY t.PointId, t.InstantId, v.Licence;