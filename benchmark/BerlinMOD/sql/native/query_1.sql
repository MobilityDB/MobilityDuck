.mode csv
.output results/output/query_1.csv

-- Query 1: What are the models of the vehicles with licence plate numbers from Licences?

EXPLAIN ANALYZE
SELECT DISTINCT l.Licence, v.Model AS Model
FROM Vehicles v, Licences l
WHERE v.Licence = l.Licence;