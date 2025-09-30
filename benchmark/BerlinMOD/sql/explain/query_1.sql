.output results/output/explain/query_1.txt

EXPLAIN ANALYZE
SELECT DISTINCT l.Licence, v.Model AS Model
FROM Vehicles v, Licences l
WHERE v.Licence = l.Licence;