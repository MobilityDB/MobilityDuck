--------------------------------
-- POINTS
--------------------------------
CREATE OR REPLACE TABLE Points (
    PointId integer PRIMARY KEY,
    PosX double precision NOT NULL,
    PosY double precision NOT NULL,
    Geom Geometry
        CHECK (ST_GeometryType(Geom) = 'POINT'),
    geomWKT VARCHAR);

COPY Points(PointId, PosX, PosY) FROM './data/points.csv';
UPDATE Points
SET Geom = ST_Point(PosX, PosY);

UPDATE Points
SET geomWKT = ST_AsText(Geom);

CREATE OR REPLACE VIEW Points1(PointId, PosX, PosY, Geom) AS
    SELECT PointId, PosX, PosY, Geom
    FROM Points
    ORDER BY PointId
    LIMIT 10;