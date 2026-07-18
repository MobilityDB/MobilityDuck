.output results/output/explain/query_7.txt

SET memory_limit = '20GB';

-- WITH Timestamps AS (
--     SELECT DISTINCT v.Licence, p.PointId, p.Geom,
--         MIN(startTimestamp(atValues(t.Trip,p.Geom::WKB_BLOB))) AS Instant
--     FROM Trips t, Vehicles v, Points1 p
--     WHERE
--         t.VehicleId = v.VehicleId
--         AND v.VehicleType = 'passenger'
--         AND t.Trip && stbox(p.Geom::WKB_BLOB)
--         AND ST_Intersects(trajectory(t.Trip)::GEOMETRY, p.Geom)
--     GROUP BY v.Licence, p.PointId, p.Geom )
-- SELECT t1.Licence, t1.PointId, t1.Geom, t1.Instant
-- FROM Timestamps t1
-- WHERE t1.Instant <= ALL (
--     SELECT t2.Instant
--     FROM Timestamps t2
--     WHERE t1.PointId = t2.PointId )
-- ORDER BY t1.PointId, t1.Licence;
EXPLAIN ANALYZE
SELECT v.vehicleid AS vehid,
       v.licence,
       p.periodId  AS periodid,
       asHexWKB(atTime(t.trip, p.period)) AS pos
FROM   Licences l
JOIN   Vehicles v  ON  v.licence = l.licence
JOIN   Trips    t  ON  t.vehicleid   = v.vehicleid
JOIN   Periods p ON true
WHERE  atTime(t.trip, p.period) IS NOT NULL
ORDER  BY v.vehicleid, p.periodId, t.tripId;