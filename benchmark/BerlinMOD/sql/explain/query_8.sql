.output results/output/explain/query_8.txt

EXPLAIN ANALYZE
SELECT l.Licence, p.PeriodId, p.Period,
    SUM(length(atTime(t.Trip, p.Period))) AS Dist
FROM Trips t, Licences1 l, Periods1 p
WHERE t.VehicleId = l.VehicleId AND t.Trip && p.Period
GROUP BY l.Licence, p.PeriodId, p.Period
ORDER BY l.Licence, p.PeriodId;