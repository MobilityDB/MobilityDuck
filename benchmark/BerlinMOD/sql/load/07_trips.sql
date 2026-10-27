/*****************************************************************************
 *
 * This MobilityDB code is provided under The PostgreSQL License.
 * Copyright (c) 2016-2026, Université libre de Bruxelles and MobilityDB
 * contributors
 *
 * MobilityDB includes portions of PostGIS version 3 source code released
 * under the GNU General Public License (GPLv2 or later).
 * Copyright (c) 2001-2026, PostGIS contributors
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written
 * agreement is hereby granted, provided that the above copyright notice and
 * this paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL UNIVERSITE LIBRE DE BRUXELLES BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
 * EVEN IF UNIVERSITE LIBRE DE BRUXELLES HAS BEEN ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * UNIVERSITE LIBRE DE BRUXELLES SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON
 * AN "AS IS" BASIS, AND UNIVERSITE LIBRE DE BRUXELLES HAS NO OBLIGATIONS TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 *****************************************************************************/

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
    Trip_h3 th3index,
    PRIMARY KEY (VehicleId, TripId));

COPY TripsInput(TripId, VehicleId, PosX, PosY, t) FROM './data/tripsinput.csv';

-- INSERT INTO TripsTmp(TripId, VehicleId, Tgeom)
-- SELECT TripId, VehicleId,
--     array_agg(
--         tgeompoint(
--         ST_Transform(
--             ST_Point(PosX, PosY),
--             'EPSG:4326',
--             'EPSG:3857',
--             always_xy := true
--         ), t
--         )
--         ORDER BY t
--     )
-- FROM TripsInput
-- GROUP BY VehicleId, TripId
-- ORDER BY VehicleId, TripId;

INSERT INTO TripsTmp(TripId, VehicleId, Tgeom)
SELECT TripId, VehicleId,
    array_agg(
        tgeompoint(
            ST_Point(PosX, PosY), t
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

UPDATE Trips
SET Trip_h3 = th3index(('SRID=4326;' || asEWKT(Trip))::tgeompoint, 7);

CREATE OR REPLACE VIEW Trips1 AS
    SELECT * FROM Trips
    ORDER BY VehicleId, TripId
    LIMIT 100;