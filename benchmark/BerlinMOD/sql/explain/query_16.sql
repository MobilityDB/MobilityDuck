.output results/output/explain/query_16.txt

SET memory_limit = '20GB';

-- SELECT p.PeriodId, p.Period, r.RegionId, l1.Licence AS Licence1, l2.Licence AS Licence2
-- FROM Trips t1, Licences1 l1, Trips t2, Licences2 l2, Periods1 p, Regions1 r
-- WHERE
--     t1.VehicleId = l1.VehicleId
--     AND t2.VehicleId = l2.VehicleId
--     AND l1.Licence < l2.Licence
--     -- AND t1.Trip && stbox(r.Geom::WKB_BLOB, p.Period)
--     -- AND t2.Trip && stbox(r.Geom::WKB_BLOB, p.Period)
--     AND ST_Intersects(trajectory(atTime(t1.Trip, p.Period))::GEOMETRY, r.Geom)
--     AND ST_Intersects(trajectory(atTime(t2.Trip, p.Period))::GEOMETRY, r.Geom)
--     AND aDisjoint(atTime(t1.Trip, p.Period), atTime(t2.Trip, p.Period))
-- ORDER BY PeriodId, RegionId, Licence1, Licence2;
EXPLAIN ANALYZE
WITH PR AS (
  SELECT p.periodId, p.period, r.regionId,
         array_agg(atTime(t.trip, p.period)) AS trips,
         array_agg(l.licence)                AS lic,
         array_agg(l.licenceId)              AS lid
  FROM   Licences l
  JOIN   Vehicles v ON v.licence = l.licence
  JOIN   Trips    t ON t.vehicleid   = v.vehicleid
  JOIN   Periods p ON true
  JOIN   Regions r ON true
  WHERE  l.licenceId <= 10 AND p.periodId <= 10 AND r.regionId <= 10
    AND  t.trip && stbox(r.geom, p.period)
    AND  eIntersects(atTime(t.trip, p.period), r.geom)
  GROUP  BY p.periodId, p.period, r.regionId )
SELECT g.periodId, g.period, g.regionId,
       g.lic[q.pair.i] AS licence1, g.lic[q.pair.j] AS licence2
FROM   PR g,
       UNNEST(aDisjointPairs(g.trips, g.trips)) AS q(pair)
WHERE  g.lid[q.pair.i] < g.lid[q.pair.j]
ORDER  BY g.periodId, g.regionId, licence1, licence2;