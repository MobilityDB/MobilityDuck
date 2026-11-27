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

/* MobilityDuck binding for the MEOS network-point types (npoint + nsegment + tnpoint).
 *
 * npoint is a network point (a route identifier + a fractional position), a
 * FIXED-size struct {int64 rid, double pos} surfaced as a BLOB (sizeof(Npoint)).
 * nsegment is a network segment {int64 rid, double pos1, double pos2}, likewise.
 * tnpoint is the temporal network point, stored as a Temporal* blob (BLOB).
 *
 * npoint is the 2D-planar twin of cbuffer: both are Spatial<T> base values,
 * non-geodetic. npoint carries NO SRID of its own — npoint_srid() returns
 * get_srid_ways(), so the `ways` network CSV must be loaded (meos_set_ways_csv,
 * done at extension init) for construction and geometry ops to resolve.
 *
 * Only the type registration + text I/O boundary is hand-written here; the
 * operation surface is generated from the MEOS-API catalog into src/generated/.
 */

#include "npoint/tnpoint.hpp"
#include "temporal/temporal.hpp"
#include "tydef.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/common/extension_type_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "mobilityduck/meos_registration.hpp"
#include "time_util.hpp"
#include "temporal/temporal_blob.hpp"

extern "C" {
    #include <meos.h>
    #include <meos_npoint.h>
    #include <meos_internal.h>
}

#include <cstdlib>
#include <cstring>
#include <string>

namespace duckdb {

namespace {

/* Npoint / Nsegment are FIXED-size structs (palloc(sizeof(...))), so the stored
 * BLOB holds exactly sizeof bytes — copy-in/out by sizeof (like Span/STBox).
 * The size is CHECKED rather than assumed: the type is BLOB-backed, so what the
 * copy reads is a length the value carries rather than one the type guarantees,
 * and a shorter one would read past the end of a string_t whose 12 bytes sit
 * inline. Reading the length also gives the compiler the bound it otherwise
 * cannot prove, which is what keeps the copy free of a -Wstringop-overread. */
template <class T>
inline T *BlobToFixed(string_t blob, const char *type_name) {
    if (blob.GetSize() != sizeof(T)) {
        throw InvalidInputException("A %s value is %llu bytes, and this one holds %llu",
                                    type_name, (uint64_t) sizeof(T), (uint64_t) blob.GetSize());
    }
    T *copy = (T *) malloc(sizeof(T));
    memcpy(copy, blob.GetData(), sizeof(T));
    return copy;
}

inline Npoint *BlobToNpoint(string_t blob) {
    return BlobToFixed<Npoint>(blob, "npoint");
}

inline string_t NpointToBlob(Vector &result, Npoint *np) {
    if (!np) return string_t();
    string_t out = StringVector::AddStringOrBlob(
        result, string_t(reinterpret_cast<const char *>(np), sizeof(Npoint)));
    free(np);
    return out;
}

inline Nsegment *BlobToNsegment(string_t blob) {
    return BlobToFixed<Nsegment>(blob, "nsegment");
}

inline string_t NsegmentToBlob(Vector &result, Nsegment *ns) {
    if (!ns) return string_t();
    string_t out = StringVector::AddStringOrBlob(
        result, string_t(reinterpret_cast<const char *>(ns), sizeof(Nsegment)));
    free(ns);
    return out;
}

inline Temporal *BlobToTemp(string_t blob) {
    size_t sz = blob.GetSize();
    uint8_t *copy = (uint8_t *) malloc(sz);
    memcpy(copy, blob.GetData(), sz);
    return reinterpret_cast<Temporal *>(copy);
}

inline string_t TempToBlob(Vector &result, Temporal *t) {
    if (!t) return string_t();
    size_t sz = temporal_mem_size(t);
    string_t out = StringVector::AddStringOrBlob(
        result, string_t(reinterpret_cast<const char *>(t), sz));
    free(t);
    return out;
}

} // namespace

/* =====================================================================
 * Registration
 * ===================================================================== */

void NpointTypes::RegisterCastFunctions(ExtensionLoader &loader) {
}

} // namespace duckdb
