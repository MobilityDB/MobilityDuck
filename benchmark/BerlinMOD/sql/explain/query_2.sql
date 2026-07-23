.output results/output/explain/query_2.txt

SET memory_limit = '20GB';

-- SELECT COUNT (DISTINCT Licence)
-- FROM Vehicles v
-- WHERE VehicleType = 'passenger';
-- EXPLAIN ANALYZE
-- SELECT DISTINCT v.Licence
-- FROM   Vehicles v
-- JOIN   Trips t    ON  t.VehicleId = v.VehicleId
-- JOIN   Regions r ON
--    eEq(geoToH3IndexSet(r.Geom, 7), t.Trip_h3)
--    AND eIntersects(t.Trip, r.Geom)
-- ORDER  BY v.Licence;
EXPLAIN ANALYZE
SELECT DISTINCT v.Licence
FROM   Vehicles v
JOIN   Trips t    ON  t.VehicleId = v.VehicleId
JOIN   Regions r ON
   eEq(r.Geom_h3set, t.Trip_h3)
   AND eIntersects(t.Trip, r.Geom)
ORDER  BY v.Licence;