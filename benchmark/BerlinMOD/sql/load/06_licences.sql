--------------------------------
-- LICENCES
--------------------------------
CREATE OR REPLACE TABLE Licences (
    LicenceId integer PRIMARY KEY,
    Licence text NOT NULL,
    VehicleId integer NOT NULL REFERENCES Vehicles(VehicleId) );

COPY Licences(LicenceId, Licence, VehicleId) FROM './data/licences.csv';


CREATE OR REPLACE VIEW Licences1(LicenceId, Licence, VehicleId) AS
    SELECT LicenceId, Licence, VehicleId
    FROM Licences
    ORDER BY LicenceId
    LIMIT 10;

CREATE OR REPLACE VIEW Licences2(LicenceId, Licence, VehicleId) AS
    SELECT LicenceId, Licence, VehicleId
    FROM Licences
    ORDER BY LicenceId
    LIMIT 10 OFFSET 10;