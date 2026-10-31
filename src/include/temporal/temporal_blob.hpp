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

// Canonical round-trip between a DuckDB blob (string_t) and a MEOS Temporal
// value. Every type binding shares this single definition, so the
// function-pointer registrations across the bindings all resolve to one body.

#include "meos_wrapper_simple.hpp"
#include "common.hpp"

namespace duckdb {

// Copy t into result's string heap and free t; returns the stored blob.
string_t TemporalToBlob(Vector &result, Temporal *t);

// Copy the blob bytes into a freshly malloc'd Temporal* owned by the caller.
Temporal *BlobToTemporal(string_t blob);

// The text asText writes for t, owned by the caller: the WKT of a spatial
// value (tspatial_as_text, trgeometry_as_text) and temporal_out for the other
// types, as MobilityDB's asText of each temporal type is.
char *TemporalAsText(const Temporal *t, int maxdd);

// The text asText writes for s, owned by the caller: the WKT of a set of
// spatial values (spatialset_as_text) and set_out for the other sets, the
// cell sets among them, as MobilityDB's asText of each set type is.
char *SetAsText(const Set *s, int maxdd);

} // namespace duckdb
