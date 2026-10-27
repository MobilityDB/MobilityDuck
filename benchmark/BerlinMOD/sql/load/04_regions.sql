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

CREATE OR REPLACE VIEW Regions1(RegionId, Geom, Geom_h3set) AS
    SELECT RegionId, Geom, Geom_h3set
    FROM Regions
    ORDER BY RegionId
    LIMIT 10;