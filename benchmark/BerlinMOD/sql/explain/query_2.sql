.output results/output/explain/query_2.txt

SET memory_limit = '20GB';

EXPLAIN ANALYZE
SELECT COUNT (DISTINCT Licence)
FROM Vehicles v
WHERE VehicleType = 'passenger';