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

#include "duckdb/common/types.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

namespace duckdb {

/* The boundary between MEOS's raster and DuckDB's raster extension
 * (duckdb-raster), whose names all carry the prefix RT_. The raster is MEOS's,
 * its algebra closed; the boundary converts it to and from the extension's
 * values, which are built here by value as the extension builds them, so the
 * conversions register whichever extension loads first:
 *
 *   raster(datacube, tile_x, tile_y, metadata)  a tile of RT_Read  -> raster
 *   CAST(rast AS RT_DATACUBE)                   raster -> its bands as a datacube
 *   rasterMetadata(rast)                        raster -> the metadata of its tile
 *   CAST(stats AS RT_STATS)                     summarystats -> RT_STATS
 *   CAST(stats AS summarystats)                 RT_STATS -> summarystats
 *
 * The casts are explicit: none keeps every property of its source. */
struct RasterBoundary {
	//! duckdb-raster's RT_DATACUBE: a BLOB aliased RT_DATACUBE
	static LogicalType RtDatacube();
	//! duckdb-raster's RT_STATS: the struct of its statistics, aliased RT_STATS
	static LogicalType RtStats();
	//! MobilityDB's summarystats: the row of the fields of a BandStats
	static LogicalType SummaryStats();

	static void Register(ExtensionLoader &loader);
};

} // namespace duckdb
