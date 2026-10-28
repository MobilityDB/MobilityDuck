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

.output results/output/explain/query_15.txt

SET memory_limit = '20GB';

/* Old version
EXPLAIN ANALYZE
SELECT DISTINCT pt.PointId, pt.Geom, pr.PeriodId, pr.Period, v.Licence
FROM Trips t, Vehicles v, Points1 pt, Periods1 pr
WHERE
    t.VehicleId = v.VehicleId
    AND t.Trip && stbox(pt.Geom::WKB_BLOB, pr.Period)
    AND ST_Intersects(trajectory(atTime(t.Trip, pr.Period))::GEOMETRY, pt.Geom)
ORDER BY pt.PointId, pr.PeriodId, v.Licence;
*/

-- WITH Temp AS (
--     SELECT DISTINCT pt.PointId, pt.Geom, pr.PeriodId, pr.Period, t.VehicleId
--     FROM Trips t, Points1 pt, Periods1 pr
--     WHERE t.Trip && stbox(pt.Geom::WKB_BLOB, pr.Period)
--     AND ST_Intersects(trajectory(atTime(t.Trip, pr.Period))::GEOMETRY, pt.Geom) )
-- SELECT DISTINCT t.PointId, t.Geom, t.PeriodId, t.Period, v.Licence  
-- FROM Temp t, Vehicles v
-- WHERE t.VehicleId = v.VehicleId 
-- ORDER BY t.PointId, t.PeriodId, v.Licence;
EXPLAIN ANALYZE
WITH Temp AS (
  SELECT DISTINCT pt.pointId, pt.geom, pt.geomWKT, pr.periodId, pr.period, t.vehicleid
  FROM   Trips t, Points pt, Periods pr
  WHERE  pt.pointId  <= 10 AND pr.periodId <= 10
    AND  t.trip && stbox(pt.geom, pr.period)
    AND  eIntersects(atTime(t.trip, pr.period), pt.geom)
)
SELECT DISTINCT t.pointId, t.geomWKT AS geom, t.periodId, t.period, v.licence
FROM   Temp t, Vehicles v
WHERE  t.vehicleid = v.vehicleid
ORDER  BY t.pointId, t.periodId, v.licence;