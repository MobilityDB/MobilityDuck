.mode csv
.output results/output/query_2.csv

-- Query 2: How many vehicles exist that are passenger cars?

EXPLAIN ANALYZE
SELECT COUNT (DISTINCT Licence)
FROM Vehicles v
WHERE VehicleType = 'passenger';