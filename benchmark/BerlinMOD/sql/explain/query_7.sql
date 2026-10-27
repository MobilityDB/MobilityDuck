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

.output results/output/explain/query_7.txt

SET memory_limit = '20GB';

-- WITH Timestamps AS (
--     SELECT DISTINCT v.Licence, p.PointId, p.Geom,
--         MIN(startTimestamp(atValues(t.Trip,p.Geom::WKB_BLOB))) AS Instant
--     FROM Trips t, Vehicles v, Points1 p
--     WHERE
--         t.VehicleId = v.VehicleId
--         AND v.VehicleType = 'passenger'
--         AND t.Trip && stbox(p.Geom::WKB_BLOB)
--         AND ST_Intersects(trajectory(t.Trip)::GEOMETRY, p.Geom)
--     GROUP BY v.Licence, p.PointId, p.Geom )
-- SELECT t1.Licence, t1.PointId, t1.Geom, t1.Instant
-- FROM Timestamps t1
-- WHERE t1.Instant <= ALL (
--     SELECT t2.Instant
--     FROM Timestamps t2
--     WHERE t1.PointId = t2.PointId )
-- ORDER BY t1.PointId, t1.Licence;
EXPLAIN ANALYZE
SELECT v.vehicleid AS vehid,
       v.licence,
       p.periodId  AS periodid,
       asHexWKB(atTime(t.trip, p.period)) AS pos
FROM   Licences l
JOIN   Vehicles v  ON  v.licence = l.licence
JOIN   Trips    t  ON  t.vehicleid   = v.vehicleid
JOIN   Periods p ON true
WHERE  atTime(t.trip, p.period) IS NOT NULL
ORDER  BY v.vehicleid, p.periodId, t.tripId;