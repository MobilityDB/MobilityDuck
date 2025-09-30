.output results/output/explain/query_2.txt

EXPLAIN ANALYZE
SELECT COUNT (DISTINCT Licence)
FROM Vehicles v
WHERE VehicleType = 'passenger';