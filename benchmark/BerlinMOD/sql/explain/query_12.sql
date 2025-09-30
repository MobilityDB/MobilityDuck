.output results/output/explain/query_12.txt

/* Old version
EXPLAIN ANALYZE
SELECT DISTINCT p.PointId, p.Geom, i.InstantId, i.Instant,
    v1.Licence AS Licence1, v2.Licence AS Licence2
FROM Trips t1, Vehicles v1, Trips t2, Vehicles v2, Points1 p, Instants1 i
WHERE
    t1.VehicleId = v1.VehicleId
    AND t2.VehicleId = v2.VehicleId
    AND t1.VehicleId < t2.VehicleId
    AND t1.Trip @> stbox(p.Geom::WKB_BLOB, i.Instant)
    AND t2.Trip @> stbox(p.Geom::WKB_BLOB, i.Instant)
    AND valueAtTimestamp(t1.Trip, i.Instant) = p.Geom
    AND valueAtTimestamp(t2.Trip, i.Instant) = p.Geom
ORDER BY p.PointId, i.InstantId, v1.Licence, v2.Licence;
*/

EXPLAIN ANALYZE
WITH Temp AS (
    SELECT DISTINCT p.PointId, p.Geom, i.InstantId, i.Instant, t.VehicleId
    FROM Trips t, Points1 p, Instants1 i
    WHERE t.Trip @> stbox(p.Geom::WKB_BLOB, i.Instant)
    AND valueAtTimestamp(t.Trip, i.Instant) = p.Geom )
SELECT DISTINCT t1.PointId, t1.Geom, t1.InstantId, t1.Instant, 
    v1.Licence AS Licence1, v2.Licence AS Licence2
FROM Temp t1
    JOIN Vehicles v1 ON t1.VehicleId = v1.VehicleId
    JOIN Temp t2 ON t1.VehicleId < t2.VehicleId
        AND t1.PointID = t2.PointID
        AND t1.InstantId = t2.InstantId
    JOIN Vehicles v2 ON t2.VehicleId = v2.VehicleId
ORDER BY t1.PointId, t1.InstantId, v1.Licence, v2.Licence;