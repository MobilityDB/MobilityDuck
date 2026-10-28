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

-- Query 7: What are the licence plate numbers of the passenger cars that have 
-- reached the points from Points first of all passenger cars during the
-- complete observation period?

WITH Temp AS (
    SELECT DISTINCT v.Licence, p.PointId, p.Geom,
        MIN(startTimestamp(atValues(t.Trip,p.Geom))) AS Instant
    FROM Trips t, Vehicles v, Points1 p
    WHERE
        t.VehicleId = v.VehicleId
        AND v.VehicleType = 'passenger'
        AND ST_Intersects(trajectory(t.Trip), p.Geom)
    GROUP BY v.Licence, p.PointId, p.Geom )
SELECT t1.Licence, t1.PointId, t1.Geom, t1.Instant
FROM Temp t1
WHERE t1.Instant <= ALL (
    SELECT t2.Instant
    FROM Temp t2
    WHERE t1.PointId = t2.PointId )
ORDER BY t1.PointId, t1.Licence;