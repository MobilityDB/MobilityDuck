--------------------------------
-- REGIONS
--------------------------------
CREATE OR REPLACE TABLE RegionsInput (
    RegionId integer,
    PointNo integer,
    XPos double precision NOT NULL,
    YPos double precision NOT NULL,
    PRIMARY KEY (RegionId, PointNo) );
CREATE OR REPLACE TABLE Regions (
    RegionId integer PRIMARY KEY,
    Geom Geometry NOT NULL
        CHECK (ST_GeometryType(Geom) = 'POLYGON'),
    Geom_h3set h3indexset);

COPY RegionsInput(RegionId, PointNo, XPos, YPos) FROM './data/regions.csv';

-- INSERT INTO Regions(RegionId, Geom)
-- SELECT RegionId, ST_MakePolygon(
--     ST_MakeLine(
--         array_agg(
--         ST_Transform(
--             ST_Point(XPos, YPos),
--             'EPSG:4326',
--             'EPSG:3857',
--             always_xy := true
--         )
--         ORDER BY PointNo
--         )
--     )
-- ) FROM RegionsInput GROUP BY RegionId;

INSERT INTO Regions(RegionId, Geom)
SELECT RegionId, ST_MakePolygon(
    ST_MakeLine(
        array_agg(
            ST_Point(XPos, YPos)
        ORDER BY PointNo
        )
    )
) FROM RegionsInput GROUP BY RegionId;

UPDATE Regions
SET Geom_h3set = geoToH3IndexSet(Geom, 7);

CREATE OR REPLACE VIEW Regions1(RegionId, Geom) AS
    SELECT RegionId, Geom
    FROM Regions
    ORDER BY RegionId
    LIMIT 10;