--------------------------------
-- VEHICLES
--------------------------------
CREATE OR REPLACE TABLE Vehicles (
    VehicleId integer PRIMARY KEY,
    Licence text NOT NULL,
    VehicleType text NOT NULL,
    Model text NOT NULL );

COPY Vehicles(VehicleId, Licence, VehicleType, Model) FROM './data/vehicles.csv';