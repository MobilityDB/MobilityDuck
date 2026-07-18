.output results/output/explain/query_17.txt

SET memory_limit = '20GB';

-- WITH PointCount AS (
--     SELECT p.PointId, COUNT(DISTINCT t.VehicleId) AS Hits
--     FROM Trips t, Points p
--     WHERE ST_Intersects(trajectory(t.Trip)::GEOMETRY, p.Geom)
--     GROUP BY p.PointId )
-- SELECT PointId, Hits
-- FROM PointCount AS p
-- WHERE p.Hits = ( SELECT MAX(Hits) FROM PointCount );
EXPLAIN ANALYZE
WITH PointCount AS (
  SELECT p.pointId, COUNT(DISTINCT t.vehicleid) AS hits
  FROM   Trips t, Points p
  WHERE  eIntersects(t.trip, p.geom)
  GROUP  BY p.pointId
)
SELECT pointId, hits
FROM   PointCount
WHERE  hits = (SELECT MAX(hits) FROM PointCount)
ORDER  BY pointId;