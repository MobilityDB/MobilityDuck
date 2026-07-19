.mode csv
.output results/output/query_9.csv

-- Query 9: What is the longest distance that was travelled by a vehicle during 
-- each of the periods from Periods?

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