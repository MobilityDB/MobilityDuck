.output results/output/explain/query_1.txt

SET memory_limit = '20GB';

-- SELECT DISTINCT l.Licence, v.Model AS Model
-- FROM Vehicles v, Licences l
-- WHERE v.Licence = l.Licence;
EXPLAIN ANALYZE
SELECT l.Licence, v.Model
FROM   Licences l
JOIN   Vehicles v ON v.Licence = l.Licence
ORDER  BY l.Licence;