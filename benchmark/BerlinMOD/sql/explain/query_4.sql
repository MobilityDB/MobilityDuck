.output results/output/explain/query_4.txt

SET memory_limit = '20GB';

-- SELECT DISTINCT p.PointId, p.Geom, v.Licence
-- FROM Trips t, Vehicles v, Points p
-- WHERE
--     t.VehicleId = v.VehicleId
--     AND ST_Intersects(trajectory(t.Trip)::GEOMETRY, p.Geom)
-- ORDER BY p.PointId, v.Licence;

EXPLAIN ANALYZE
SELECT DISTINCT v.licence
FROM   Vehicles v
JOIN   Trips t      ON t.vehicleid  = v.vehicleid
JOIN   Points p ON
   COALESCE(eEq(geoToH3Cell(p.geom, 7), t.trip_h3), TRUE)
   AND eIntersects(t.trip, p.geom)
ORDER  BY v.licence;