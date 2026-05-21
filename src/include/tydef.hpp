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

// MEOS naming history: `meosType` is the **pre-consolidation** spelling
// and `MeosType` is the **post-consolidation** target (the rename is
// part of the upstream consolidation sweep, not yet reached by the
// vcpkg pin).  The current pin
// (`vcpkg_ports/meos/portfile.cmake` REF f11b7443ee98…) is still
// pre-consolidation and exposes `meosType` — see
// meos/include/temporal/meos_catalog.h, where line 121 declares
// `} meosType;`.  MobilityDuck's source consistently uses
// `meosType` (verified via `grep -rn '\bmeosType\b' src/`), which
// matches the pin, so no alias is needed today.
//
// An earlier version of this file added `using meosType = MeosType;`
// as a forward-looking bridge for the eventual consolidation bump.
// That alias references `MeosType`, which the current pin does NOT
// yet expose, so it broke the build:
//   "'MeosType' does not name a type; did you mean 'meosType'?".
//
// When the MEOS pin is bumped past the consolidation point, restore
// a bridge here (`using meosType = MeosType;` becomes valid then) or
// sweep the source `meosType → MeosType` in one PR — whichever the
// project prefers at that time.
//
// The integration branch (this branch) restores the bridge so the
// existing 100+ `meosType` call sites compile against the
// post-consolidation MEOS pin without a tree-wide sed.  Drop this
// alias when the rename sweep is done.
using meosType = MeosType;

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
#define DatumGetBool(X) ((bool) (((int64) (X)) != 0))
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
