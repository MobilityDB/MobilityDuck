--------------------------------
-- POINTS
--------------------------------
CREATE OR REPLACE TABLE Points (
    PointId integer PRIMARY KEY,
    PosX double precision NOT NULL,
    PosY double precision NOT NULL,
    Geom Geometry
        CHECK (ST_GeometryType(Geom) = 'POINT'),
    geomWKT VARCHAR,
    geom_h3cell h3index);

COPY Points(PointId, PosX, PosY) FROM './data/points.csv';
UPDATE Points
SET Geom = ST_Point(PosX, PosY);

UPDATE Points
SET geomWKT = ST_AsText(Geom);

UPDATE Points
SET geom_h3cell = geoToH3Cell(Geom, 7);

CREATE OR REPLACE VIEW Points1(PointId, PosX, PosY, Geom) AS
    SELECT PointId, PosX, PosY, Geom
    FROM Points
    ORDER BY PointId
    LIMIT 10;