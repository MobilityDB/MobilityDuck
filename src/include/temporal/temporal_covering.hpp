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

#include <string>

namespace duckdb {

/* A covering column TemporalParquet declares for a temporal column: the struct
 * column <column><column_suffix> carries the bounds named in `bounds` */
struct TemporalCovering {
    const char *key;               /* bbox, tspan or vspan; nullptr ends a list */
    const char *column_suffix;     /* the suffix naming the struct column */
    const char *const *bounds;     /* the bound names, ended by nullptr */
};

/* The TemporalParquet description a temporal base type fixes */
struct TemporalCoveringType {
    const char *base_type;
    const char *edges;                 /* GeoParquet edges, nullptr when the base fixes none */
    const TemporalCovering *coverings; /* ended by a covering whose key is nullptr */
};

/* Return the description of @p base_type, or nullptr for a type TemporalParquet
 * gives no covering. The table is generated from the MEOS-API catalog by
 * tools/codegen_duck_udfs.py into src/generated/generated_temporal_covering.cpp */
const TemporalCoveringType *TemporalCoveringOf(const std::string &base_type);

} // namespace duckdb
