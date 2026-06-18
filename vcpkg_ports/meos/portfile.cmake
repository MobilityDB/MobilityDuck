vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO estebanzimanyi/MobilityDB
    REF 008ef1c2a590b7d6ab6ad9821ac51f13c852bb39
    SHA512 8e8b87821900107bf3dddbec93978f150779353f94e99eb9c3a6dd877053868c93cf94a33dcc85d353c57f41bd98bfe02c99c86409ff0b5807d44d6d942d3ae5
)


# json-c's FindJSON-C.cmake uses hardcoded system hints (/usr/lib, /usr/include)
# that miss vcpkg's installed layout.  Resolve the library and include paths
# explicitly so they are pre-set as cache variables before FindJSON-C runs.
set(_meos_jsonc_lib_candidates
    "${CURRENT_INSTALLED_DIR}/lib/libjson-c.so"
    "${CURRENT_INSTALLED_DIR}/lib/libjson-c.a"
    "${CURRENT_INSTALLED_DIR}/lib/libjson-c${CMAKE_SHARED_LIBRARY_SUFFIX}"
    "${CURRENT_INSTALLED_DIR}/lib/libjson-c${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(_MEOS_JSONC_LIB "")
foreach(_cand IN LISTS _meos_jsonc_lib_candidates)
    if(EXISTS "${_cand}")
        set(_MEOS_JSONC_LIB "${_cand}")
        break()
    endif()
endforeach()
if(NOT _MEOS_JSONC_LIB)
    message(FATAL_ERROR "MEOS port: cannot locate vcpkg-installed libjson-c under ${CURRENT_INSTALLED_DIR}/lib")
endif()
# json-c headers install under include/json-c/; FindJSON-C.cmake searches for
# json.h with PATH_SUFFIXES json-c, so pass the parent include directory.
set(_MEOS_JSONC_INC "${CURRENT_INSTALLED_DIR}/include/json-c")
if(NOT EXISTS "${_MEOS_JSONC_INC}/json.h")
    message(FATAL_ERROR "MEOS port: cannot locate vcpkg-installed json.h under ${CURRENT_INSTALLED_DIR}/include/json-c")
endif()

# Upstream gap: `temporal_parse` in `meos/src/temporal/type_parser.c` routes any
# input that starts with '{' to the discrete-sequence parser, consuming the '{' as
# the outer sequence delimiter.  For T_TJSONB, a bare instant like
# `{"k":1}@2000-01-01` also starts with '{' (the JSON object delimiter), so it is
# incorrectly dispatched to tdiscseq_parse which then tries to parse `"k":1}@...`
# as a temporal instant and fails with "Missing delimeter character '@'".
#
# Fix: after peeking inside the outer '{', distinguish the three cases:
#   - next char is '[' or '(' → sequence set (existing behaviour)
#   - next char is '{' → discrete sequence (first instant's value starts with '{')
#   - anything else AND basetype == T_JSONB → JSON-object instant; restore and
#     parse via tinstant_parse
# For non-T_JSONB types no observable behaviour change: their instant values never
# start with '{', so the second condition (!=T_JSONB) keeps them in tdiscseq_parse.
vcpkg_replace_string(
    "${SOURCE_PATH}/meos/src/temporal/type_parser.c"
    [=[  else if (**str == '{')
  {
    const char *bak = *str;
    p_obrace(str);
    p_whitespace(str);
    if (**str == '[' || **str == '(')
    {
      *str = bak;
      result = (Temporal *) tsequenceset_parse(str, temptype, interp);
    }
    else
    {
      *str = bak;
      result = (Temporal *) tdiscseq_parse(str, temptype);
    }
  }]=]
    [=[  else if (**str == '{')
  {
    const char *bak = *str;
    p_obrace(str);
    p_whitespace(str);
    if (**str == '[' || **str == '(')
    {
      *str = bak;
      result = (Temporal *) tsequenceset_parse(str, temptype, interp);
    }
    else if (**str == '{' || temptype_basetype(temptype) != T_JSONB)
    {
      /* Discrete sequence: either next token is another '{' (e.g. first
       * instant's JSON-object value) or the base type never starts with '{'
       * so the outer '{' is definitely the sequence delimiter. */
      *str = bak;
      result = (Temporal *) tdiscseq_parse(str, temptype);
    }
    else
    {
      /* The outer '{' belongs to the base value itself (e.g. a JSON object).
       * Restore and parse as a temporal instant. */
      *str = bak;
      TInstant *inst = tinstant_parse(str, temptype, true);
      if (! inst)
        return NULL;
      result = (Temporal *) inst;
    }
  }]=]
)

# Upstream gap: `pgtypes/libpq/pqformat.h` contains a deprecated
# static-inline helper `pq_sendint` that calls `elog()`.  In the
# standalone MEOS build the pgtypes shim does not declare `elog`, and
# GCC 14 (Ubuntu 24.04 runners) treats implicit-function-declaration
# as a hard error.  Replace the call with `meos_error` — both symbols
# are in scope via the postgres.h → meos_error.h chain that pqformat.c
# already includes before pulling in pqformat.h.
vcpkg_replace_string(
    "${SOURCE_PATH}/pgtypes/libpq/pqformat.h"
    [=[elog(ERROR, "unsupported integer size %d", b);]=]
    [=[meos_error(ERROR, MEOS_ERR_INTERNAL_ERROR, "unsupported integer size %d", b);]=]
)

# Upstream gap: the MEOS=1 include path (`postgres_int_defs.h` → `pg_timestamp.h`)
# does not reach `pgtypes/utils/timestamp.h`, so the four Timestamp/TimestampTz
# Datum accessors are absent from pure MEOS=1 TUs (e.g. `meos/src/h3/th3index_boxops.c`
# calls `TimestampTzGetDatum`).  GCC 14 treats implicit-function-declaration as a
# hard error.  Adding the functions unconditionally to `pg_timestamp.h` causes
# redefinition errors in pgtypes TUs that explicitly include `utils/timestamp.h`
# (e.g. `pgtypes/common/stringinfo.c`), because both files define the same four
# static-inline functions.
#
# Fix — cross-guard the definitions so the header processed first wins:
#   · `pg_timestamp.h` adds the four functions under `#ifndef TIMESTAMP_H`
#     (the include-guard of `utils/timestamp.h`).
#   · `utils/timestamp.h` wraps its four matching functions under
#     `#ifndef __PG_TIMESTAMP_H__` (the include-guard of `pg_timestamp.h`).
vcpkg_replace_string(
    "${SOURCE_PATH}/pgtypes/pg_timestamp.h"
    [=[typedef int64 TimestampTz;]=]
    [=[typedef int64 TimestampTz;
#ifndef TIMESTAMP_H
static inline Datum TimestampGetDatum(Timestamp X) { return Int64GetDatum(X); }
static inline Datum TimestampTzGetDatum(TimestampTz X) { return Int64GetDatum(X); }
static inline Timestamp DatumGetTimestamp(Datum X) { return (Timestamp) DatumGetInt64(X); }
static inline TimestampTz DatumGetTimestampTz(Datum X) { return (TimestampTz) DatumGetInt64(X); }
#endif]=]
)

vcpkg_replace_string(
    "${SOURCE_PATH}/pgtypes/utils/timestamp.h"
    [=[static inline Timestamp
DatumGetTimestamp(Datum X)
{
	return (Timestamp) DatumGetInt64(X);
}

static inline TimestampTz
DatumGetTimestampTz(Datum X)
{
	return (TimestampTz) DatumGetInt64(X);
}

static inline Interval *
DatumGetIntervalP(Datum X)
{
	return (Interval *) DatumGetPointer(X);
}

static inline Datum
TimestampGetDatum(Timestamp X)
{
	return Int64GetDatum(X);
}

static inline Datum
TimestampTzGetDatum(TimestampTz X)
{
	return Int64GetDatum(X);
}]=]
    [=[#ifndef __PG_TIMESTAMP_H__
static inline Timestamp
DatumGetTimestamp(Datum X)
{
	return (Timestamp) DatumGetInt64(X);
}

static inline TimestampTz
DatumGetTimestampTz(Datum X)
{
	return (TimestampTz) DatumGetInt64(X);
}
#endif

static inline Interval *
DatumGetIntervalP(Datum X)
{
	return (Interval *) DatumGetPointer(X);
}

#ifndef __PG_TIMESTAMP_H__
static inline Datum
TimestampGetDatum(Timestamp X)
{
	return Int64GetDatum(X);
}

static inline Datum
TimestampTzGetDatum(TimestampTz X)
{
	return Int64GetDatum(X);
}
#endif]=]
)

# Upstream gap: `add_timestamptz_interval` and `add_date_int` were removed
# from meos.h in this pin but are still compiled into libmeos.so (pgtypes).
# Re-export them in postgres_ext_defs.in.h so the installed meos.h continues
# to declare them for binding consumers (temporal tile functions call them).
vcpkg_replace_string(
    "${SOURCE_PATH}/meos/include/postgres_ext_defs.in.h"
    [=[extern TimestampTz minus_timestamptz_interval(TimestampTz tstz, const Interval *interv);]=]
    [=[extern TimestampTz add_timestamptz_interval(TimestampTz tstz, const Interval *interv);
extern TimestampTz minus_timestamptz_interval(TimestampTz tstz, const Interval *interv);]=]
)

vcpkg_replace_string(
    "${SOURCE_PATH}/meos/include/postgres_ext_defs.in.h"
    [=[extern DateADT minus_date_int(DateADT date, int32 days);]=]
    [=[extern DateADT add_date_int(DateADT date, int32 days);
extern DateADT minus_date_int(DateADT date, int32 days);]=]
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DMEOS=ON
        -DH3=OFF
        -DJSON=ON
        -DQUADBIN=ON
        "-DJSON-C_LIBRARIES=${_MEOS_JSONC_LIB}"
        "-DJSON-C_INCLUDE_DIRS=${_MEOS_JSONC_INC}"
        -DBUILD_SHARED_LIBS=ON
        # Build only the MEOS library, not the MEOS C test binaries: those link
        # the GEOS C++ API, which the arm64-linux vcpkg triplet does not carry.
        -DBUILD_TESTING=OFF
        -DCMAKE_C_FLAGS="-Dsession_timezone=meos_session_timezone"
        -DCMAKE_CXX_FLAGS="-Dsession_timezone=meos_session_timezone"
)

vcpkg_cmake_install()

# meos_tls.h is not listed in the upstream install() rules at this pin.
# It is included verbatim by the cmake-generated meos.h; copy it alongside
# the other installed headers.  meos_json.h is installed automatically by
# cmake when JSON=ON (as the stripped export variant).
file(COPY "${SOURCE_PATH}/meos/include/meos_tls.h"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include")

file(MAKE_DIRECTORY "${CURRENT_PACKAGES_DIR}/share/meos")
file(WRITE "${CURRENT_PACKAGES_DIR}/share/meos/MEOSConfig.cmake" [=[
# Minimal imported target for MEOS
if (NOT TARGET MEOS::meos)
  add_library(MEOS::meos UNKNOWN IMPORTED)
  # Look for the library in vcpkg's lib folders
  foreach(_cand meos libmeos)
    if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_cand}.lib")
      set(_meos_lib "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_cand}.lib")
    elseif (EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_cand}.a")
      set(_meos_lib "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_cand}.a")
    elseif (EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_cand}.so")
      set(_meos_lib "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_cand}.so")
    elseif (EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_cand}.dylib")
      set(_meos_lib "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_cand}.dylib")
    endif()
  endforeach()
  if (NOT _meos_lib)
    message(FATAL_ERROR "MEOS library not found in vcpkg package layout.")
  endif()
  set_target_properties(MEOS::meos PROPERTIES
    IMPORTED_LOCATION "${_meos_lib}"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../include"
  )
endif()
]=])

file(WRITE "${CURRENT_PACKAGES_DIR}/share/meos/usage" [=[
MEOS installed.

CMake:
  find_package(MEOS CONFIG REQUIRED)
  target_link_libraries(your_target PRIVATE MEOS::meos)
]=])
