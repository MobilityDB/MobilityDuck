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

// Single definition of the canonical DuckDB-blob <-> MEOS Temporal round-trip
// shared by every type binding.

#include "temporal/temporal_blob.hpp"

#include <cstdlib>
#include <cstring>

namespace duckdb {

string_t TemporalToBlob(Vector &result, Temporal *t) {
    size_t sz = temporal_mem_size(t);
    string_t out = StringVector::AddStringOrBlob(
        result, reinterpret_cast<const char *>(t), sz);
    free(t);
    return out;
}

Temporal *BlobToTemporal(string_t blob) {
    size_t sz = blob.GetSize();
    uint8_t *copy = static_cast<uint8_t *>(malloc(sz));
    memcpy(copy, blob.GetData(), sz);
    return reinterpret_cast<Temporal *>(copy);
}

char *TemporalAsText(const Temporal *t, int maxdd) {
    switch (t->temptype) {
    case T_TGEOMPOINT:
    case T_TGEOGPOINT:
    case T_TGEOMETRY:
    case T_TGEOGRAPHY:
    case T_TCBUFFER:
    case T_TNPOINT:
    case T_TPOSE:
    case T_TPOSECHAIN:
        return tspatial_as_text(t, maxdd);
    case T_TRGEOMETRY:
        return trgeometry_as_text(t, maxdd);
    default:
        return temporal_out(t, maxdd);
    }
}

char *SetAsText(const Set *s, int maxdd) {
    switch (s->settype) {
    case T_GEOMSET:
    case T_GEOGSET:
    case T_CBUFFERSET:
    case T_NPOINTSET:
    case T_POSESET:
    case T_POSECHAINSET:
    case T_PCPOINTSET:
    case T_PCPATCHSET:
        return spatialset_as_text(s, maxdd);
    default:
        return set_out(s, maxdd);
    }
}

} // namespace duckdb
