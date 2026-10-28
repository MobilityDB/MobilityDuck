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

.output results/output/explain/query_16.txt

SET memory_limit = '20GB';

-- SELECT p.PeriodId, p.Period, r.RegionId, l1.Licence AS Licence1, l2.Licence AS Licence2
-- FROM Trips t1, Licences1 l1, Trips t2, Licences2 l2, Periods1 p, Regions1 r
-- WHERE
--     t1.VehicleId = l1.VehicleId
--     AND t2.VehicleId = l2.VehicleId
--     AND l1.Licence < l2.Licence
--     -- AND t1.Trip && stbox(r.Geom::WKB_BLOB, p.Period)
--     -- AND t2.Trip && stbox(r.Geom::WKB_BLOB, p.Period)
--     AND ST_Intersects(trajectory(atTime(t1.Trip, p.Period))::GEOMETRY, r.Geom)
--     AND ST_Intersects(trajectory(atTime(t2.Trip, p.Period))::GEOMETRY, r.Geom)
--     AND aDisjoint(atTime(t1.Trip, p.Period), atTime(t2.Trip, p.Period))
-- ORDER BY PeriodId, RegionId, Licence1, Licence2;
EXPLAIN ANALYZE
WITH PR AS (
  SELECT p.periodId, p.period, r.regionId,
         array_agg(atTime(t.trip, p.period)) AS trips,
         array_agg(l.licence)                AS lic,
         array_agg(l.licenceId)              AS lid
  FROM   Licences l
  JOIN   Vehicles v ON v.licence = l.licence
  JOIN   Trips    t ON t.vehicleid   = v.vehicleid
  JOIN   Periods p ON true
  JOIN   Regions r ON true
  WHERE  l.licenceId <= 10 AND p.periodId <= 10 AND r.regionId <= 10
    AND  t.trip && stbox(r.geom, p.period)
    AND  eIntersects(atTime(t.trip, p.period), r.geom)
  GROUP  BY p.periodId, p.period, r.regionId )
SELECT g.periodId, g.period, g.regionId,
       g.lic[q.pair.i] AS licence1, g.lic[q.pair.j] AS licence2
FROM   PR g,
       UNNEST(aDisjointPairs(g.trips, g.trips)) AS q(pair)
WHERE  g.lid[q.pair.i] < g.lid[q.pair.j]
ORDER  BY g.periodId, g.regionId, licence1, licence2;