--------------------------------
-- TRIPS
--------------------------------
CREATE OR REPLACE TABLE TripsInput (
    TripId integer NOT NULL,
    VehicleId integer NOT NULL REFERENCES Vehicles(VehicleId),
    PosX float NOT NULL,
    PosY float NOT NULL,
    t timestamptz NOT NULL,
    PRIMARY KEY (TripId, t) );
CREATE OR REPLACE TABLE TripsTmp (
    TripId integer NOT NULL,
    VehicleId integer NOT NULL REFERENCES Vehicles(VehicleId),
    Tgeom tgeompoint[] NOT NULL);
CREATE OR REPLACE TABLE Trips (
    TripId integer,
    VehicleId integer NOT NULL REFERENCES Vehicles(VehicleId),
    Trip tgeompoint NOT NULL,
    Traj geometry,
    PRIMARY KEY (VehicleId, TripId));

COPY TripsInput(TripId, VehicleId, PosX, PosY, t) FROM './data/tripsinput.csv';

INSERT INTO TripsTmp(TripId, VehicleId, Tgeom)
SELECT TripId, VehicleId,
    array_agg(
        tgeompoint(
        ST_Transform(
            ST_Point(PosX, PosY),
            'EPSG:4326',
            'EPSG:3857',
            always_xy := true
        )::WKB_BLOB, t
        )
        ORDER BY t
    )
FROM TripsInput
GROUP BY VehicleId, TripId
ORDER BY VehicleId, TripId;

INSERT INTO Trips(TripId, VehicleId, Trip)
SELECT TripId, VehicleId, tgeompointSeq(Tgeom) FROM TripsTmp
ORDER BY VehicleId, TripId;

UPDATE Trips
SET Traj = trajectory(Trip);

CREATE OR REPLACE VIEW Trips1 AS
    SELECT * FROM Trips
    ORDER BY VehicleId, TripId
    LIMIT 100;