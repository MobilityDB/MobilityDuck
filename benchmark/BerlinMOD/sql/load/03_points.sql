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

CREATE OR REPLACE VIEW Points1(PointId, PosX, PosY, Geom, geomWKT, geom_h3cell) AS
    SELECT PointId, PosX, PosY, Geom, geomWKT, geom_h3cell
    FROM Points
    ORDER BY PointId
    LIMIT 10;