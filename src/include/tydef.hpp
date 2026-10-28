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

#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>

extern "C" {
    #include <meos.h>
    #include <meos_internal.h>
}

namespace duckdb {

static inline Datum
Float8GetDatum(double X)
{
  union
  {
    double    value;
    int64    retval;
  }      myunion;

  myunion.value = X;
  return Datum(myunion.retval);
}

static inline double
DatumGetFloat8(Datum X)
{
  union
  {
    int64    value;
    double    retval;
  }      myunion;

  myunion.value = int64(X);
  return myunion.retval;
}

#define DatumGetInt32(X) ((int32) (X))
#define DatumGetInt64(X) ((int64) (X))
#define DatumGetCString(X) ((char *) DatumGetPointer(X))
#define CStringGetDatum(X) PointerGetDatum(X)
#define DatumGetPointer(X) ((Pointer) (X))
#define PointerGetDatum(X) ((Datum) (X))    
#define DatumGetTextP(X)      ((text *) DatumGetPointer(X)) // ((text *) PG_DETOAST_DATUM(X))
#define SET_VARSIZE(PTR, len)        SET_VARSIZE_4B(PTR, len)
#define SET_VARSIZE_4B(PTR,len) \
  (((varattrib_4b *) (PTR))->va_4byte.va_header = (((uint32) (len)) << 2))
#define VARSIZE(PTR)            VARSIZE_4B(PTR)
#define VARSIZE_4B(PTR) \
  ((((varattrib_4b *) (PTR))->va_4byte.va_header >> 2) & 0x3FFFFFFF)
#define VARDATA(PTR)            VARDATA_4B(PTR)
#define VARDATA_4B(PTR)    (((varattrib_4b *) (PTR))->va_4byte.va_data) 
#define FLEXIBLE_ARRAY_MEMBER	/* empty */

#define VARSIZE_ANY(PTR) \
  (VARATT_IS_1B_E(PTR) ? VARSIZE_EXTERNAL(PTR) : \
   (VARATT_IS_1B(PTR) ? VARSIZE_1B(PTR) : \
    VARSIZE_4B(PTR)))
typedef union
{
  struct            /* Normal varlena (4-byte length) */
  {
    uint32    va_header;
    char    va_data[FLEXIBLE_ARRAY_MEMBER];
  }      va_4byte;
  struct            /* Compressed-in-line format */
  {
    uint32    va_header;
    uint32    va_tcinfo;  /* Original data size (excludes header) and
                 * compression method; see va_extinfo */
    char    va_data[FLEXIBLE_ARRAY_MEMBER]; /* Compressed data */
  }      va_compressed;
} varattrib_4b;

#define VARHDRSZ		((int32) sizeof(int32))
}

#define OUT_DEFAULT_DECIMAL_DIGITS 15
// #define Float8GetDatum(X) ((Datum) *(uint64_t *) &(X))
#define Int32GetDatum(X) ((Datum) (X))
