-- Query 1: What are the models of the vehicles with licence plate numbers from Licences?

SELECT DISTINCT l.Licence, v.Model AS Model
FROM Vehicles v, Licences l
WHERE v.Licence = l.Licence;