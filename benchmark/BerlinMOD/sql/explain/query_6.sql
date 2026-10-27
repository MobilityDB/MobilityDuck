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

.output results/output/explain/query_6.txt

SET memory_limit = '20GB';

/* SLOWER VERSION

EXPLAIN ANALYZE
SELECT DISTINCT v1.Licence AS Licence1, v2.Licence AS Licence2
FROM Trips t1, Vehicles v1, Trips t2, Vehicles v2
WHERE
    t1.VehicleId = v1.VehicleId
    AND t2.VehicleId = v2.VehicleId
    AND t1.VehicleId < t2.VehicleId
    AND v1.VehicleType = 'truck'
    AND v2.VehicleType = 'truck'
    AND t1.Trip && expandSpace(t2.Trip::STBOX, 10)
    AND eDwithin(t1.Trip, t2.Trip, 10.0)
ORDER BY v1.Licence, v2.Licence;

*/

-- WITH Temp(Licence, VehicleId, Trip) AS (
--     SELECT v.Licence, t.VehicleId, t.Trip
--     FROM Trips t, Vehicles v
--     WHERE t.VehicleId = v.VehicleId 
--     AND v.VehicleType = 'truck' )
-- SELECT t1.Licence, t2.Licence
-- FROM Temp t1, Temp t2
-- WHERE t1.VehicleId < t2.VehicleId 
--     AND t1.Trip && expandSpace(t2.Trip::STBOX, 10) 
--     AND eDwithin(t1.Trip, t2.Trip, 10.0)
-- ORDER BY t1.Licence, t2.Licence;
EXPLAIN ANALYZE
WITH TruckTrips AS (
  SELECT array_agg(t.trip)    AS trips,
         array_agg(v.licence) AS lic
  FROM   Vehicles v
  JOIN   Trips    t ON t.vehicleid = v.vehicleid
  WHERE  v.vehicletype = 'truck' )
SELECT DISTINCT g.lic[p.pair.i] AS licence1, g.lic[p.pair.j] AS licence2
FROM   TruckTrips g,
       UNNEST(eDwithinPairs(g.trips, g.trips, 10.0)) AS p(pair)
WHERE  p.pair.i < p.pair.j
ORDER  BY licence1, licence2;