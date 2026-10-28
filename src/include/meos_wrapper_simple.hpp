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

#pragma once

#include <cstddef>
#include <cstdint>
// Include MEOS headers
extern "C" {
    #include <meos.h>
    #include <meos_geo.h>
    #include <meos_internal.h>
    #include <meos_internal_geo.h>
    // Per-family public headers (alphabetical, per the family-block convention):
    // family type declarations and cross-family conversions (e.g.
    // tcbuffer_to_tfloat, ttext_to_tjsonb, tbigint_to_tquadbin) live here, not in
    // the meos.h umbrella. The port builds every family the generated UDFs call
    // into. meos_pointcloud.h must be wrapped here (C linkage) now that the
    // pointcloud value surface (tpcpoint/tpcpatch startValue/atValue/…) generates
    // and links against libmeos. (Alphabetical also keeps meos_pose.h before
    // meos_rgeo.h, which depends on it.)
    #include <meos_cbuffer.h>
    #include <meos_cellindex.h>
    #include <meos_h3.h>
    #include <meos_json.h>
    #include <meos_npoint.h>
    #include <meos_pointcloud.h>
    #include <meos_pose.h>
    #include <meos_quadbin.h>
    #include <meos_raster.h>
    #include <meos_s2cell.h>
    #include <meos_rgeo.h>
    // PG-compat date/timestamp arithmetic (add_date_int, add_timestamptz_interval)
    // lives in the pgtypes public leaf headers, not the meos.h umbrella.
    #include <pg_date.h>
    #include <pg_timestamp.h>
}

// Create explicit aliases for MEOS types
// Use the original MEOS type names but with explicit qualification
using MeosInterval = ::Interval;  // Explicitly use global MEOS Interval
using MeosTimestamp = ::Timestamp;  // Explicitly use global MEOS Timestamp 
