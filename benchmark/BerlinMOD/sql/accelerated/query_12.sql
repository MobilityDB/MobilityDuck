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

-- Query 12: Which vehicles met at a point from Points1 at an instant 
-- from Instants1?

WITH Temp AS (
    SELECT DISTINCT p.PointId, p.Geom, i.InstantId, i.Instant, t.VehicleId
    FROM Trips t, Points1 p, Instants1 i
    WHERE COALESCE(eEq(p.geom_h3cell, t.Trip_h3), TRUE)
    AND t.Trip @> stbox(p.Geom, i.Instant)
    AND valueAtTimestamp(t.Trip, i.Instant) = p.Geom )
SELECT DISTINCT t1.PointId, t1.Geom, t1.InstantId, t1.Instant,
    v1.Licence AS Licence1, v2.Licence AS Licence2
FROM Temp t1
    JOIN Vehicles v1 ON t1.VehicleId = v1.VehicleId
    JOIN Temp t2 ON t1.VehicleId < t2.VehicleId
        AND t1.PointID = t2.PointID
        AND t1.InstantId = t2.InstantId
    JOIN Vehicles v2 ON t2.VehicleId = v2.VehicleId
ORDER BY t1.PointId, t1.InstantId, v1.Licence, v2.Licence;