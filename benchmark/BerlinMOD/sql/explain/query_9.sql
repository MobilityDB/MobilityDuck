.output results/output/explain/query_9.txt

EXPLAIN ANALYZE
WITH Distances AS (
    SELECT p.PeriodId, p.Period, t.VehicleId,
        SUM(length(atTime(t.Trip, p.Period))) AS Dist
    FROM Trips t, Periods p
    WHERE t.Trip && p.Period
    GROUP BY p.PeriodId, p.Period, t.VehicleId )
SELECT PeriodId, Period, MAX(Dist) AS MaxDist
FROM Distances
GROUP BY PeriodId, Period
ORDER BY PeriodId;