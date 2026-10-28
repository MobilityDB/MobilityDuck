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

.output results/output/explain/query_13.txt

SET memory_limit = '20GB';

/* Old version
EXPLAIN ANALYZE
SELECT DISTINCT r.RegionId, p.PeriodId, p.Period, v.Licence
FROM Trips t, Vehicles v, Regions1 r, Periods1 p
WHERE
    t.VehicleId = v.VehicleId
    AND t.trip && stbox(r.Geom::WKB_BLOB, p.Period)
    AND ST_Intersects(trajectory(atTime(t.Trip, p.Period))::GEOMETRY, r.Geom)
ORDER BY r.RegionId, p.PeriodId, v.Licence;
*/

-- WITH Temp AS (
--     SELECT DISTINCT r.RegionId, p.PeriodId, p.Period, t.VehicleId
--     FROM Trips t, Regions1 r, Periods1 p
--     WHERE
--         t.trip && stbox(r.Geom::WKB_BLOB, p.Period)
--         AND ST_Intersects(trajectory(atTime(t.Trip, p.Period))::GEOMETRY, r.Geom)
--     ORDER BY r.RegionId, p.PeriodId )
-- SELECT DISTINCT t.RegionId, t.PeriodId, t.Period, v.Licence
-- FROM Temp t, Vehicles v
-- WHERE t.VehicleId = v.VehicleId 
-- ORDER BY t.RegionId, t.PeriodId, v.Licence;
EXPLAIN ANALYZE
WITH Temp AS (
  SELECT DISTINCT r.regionId, p.periodId, p.period, t.vehicleid
  FROM   Trips t, Regions r, Periods p
  WHERE  r.regionId <= 10 AND p.periodId <= 10
    AND  t.trip && stbox(r.geom, p.period)
    AND  eIntersects(atTime(t.trip, p.period), r.geom)
)
SELECT DISTINCT t.regionId, t.periodId, t.period, v.licence
FROM   Temp t, Vehicles v
WHERE  t.vehicleid = v.vehicleid
ORDER  BY t.regionId, t.periodId, v.licence;