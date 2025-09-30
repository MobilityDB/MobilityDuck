.mode csv
.output results/output/query_2.csv

SELECT COUNT (DISTINCT Licence)
FROM Vehicles v
WHERE VehicleType = 'passenger';