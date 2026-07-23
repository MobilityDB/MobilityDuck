-- Query 2: How many vehicles exist that are passenger cars?

SELECT COUNT (DISTINCT Licence)
FROM Vehicles v
WHERE VehicleType = 'passenger';