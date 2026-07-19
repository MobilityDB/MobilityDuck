.mode csv
.output results/output/query_8.csv

-- Query 8: What are the overall travelled distances of the vehicles with licence
-- plate numbers from Licences1 during the periods from Periods1?

EXPLAIN ANALYZE
SELECT l.Licence, p.PeriodId, p.Period,
    SUM(length(atTime(t.Trip, p.Period))) AS Dist
FROM Trips t, Licences1 l, Periods1 p
WHERE t.VehicleId = l.VehicleId AND t.Trip && p.Period
GROUP BY l.Licence, p.PeriodId, p.Period
ORDER BY l.Licence, p.PeriodId;