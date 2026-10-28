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

.output results/output/explain/query_11.txt

SET memory_limit = '20GB';

/* Old version
EXPLAIN ANALYZE
SELECT p.PointId, p.Geom, i.InstantId, i.Instant, v.Licence
FROM Trips t, Vehicles v, Points1 p, Instants1 i
WHERE
    t.VehicleId = v.VehicleId
    AND t.Trip @> stbox(p.Geom::WKB_BLOB, i.Instant)
    AND valueAtTimestamp(t.Trip, i.Instant) = p.Geom
ORDER BY p.PointId, i.InstantId, v.Licence;
*/

-- WITH Temp AS (
--     SELECT p.PointId, p.Geom, i.InstantId, i.Instant, t.VehicleId
--     FROM Trips t, Points1 p, Instants1 i
--     WHERE
--         t.Trip @> stbox(p.Geom::WKB_BLOB, i.Instant)
--         AND valueAtTimestamp(t.Trip, i.Instant) = p.Geom )
-- SELECT t.PointId, t.Geom, t.InstantId, t.Instant, v.Licence
-- FROM Temp t JOIN Vehicles v ON t.VehicleId = v.VehicleId
-- ORDER BY t.PointId, t.InstantId, v.Licence;
EXPLAIN ANALYZE
WITH Temp AS (
  SELECT p.pointId, p.geom, p.geomWKT, i.instantId, i.instant, t.vehicleid
  FROM   Trips t, Points p, Instants i
  WHERE  t.trip && stbox(p.geom, i.instant)
    AND  valueAtTimestamp(t.trip, i.instant) = p.geom
)
SELECT t.pointId, t.geomWKT AS geom, t.instantId, t.instant, v.licence
FROM   Temp t
JOIN   Vehicles v ON t.vehicleid = v.vehicleid
ORDER  BY t.pointId, t.instantId, v.licence;