# MobilityDuck tracks MobilityDB/MobilityDB master, one recorded commit at a time.
# _MEOS_REF is the commit libmeos is built from, and the SAME commit the committed
# generated UDF surface is derived from, so the library and the surface that links
# against it always match one upstream commit.
#
# The refresh workflow (.github/workflows/meos-surface-refresh.yml) advances this
# SHA daily: it derives the catalog from master's tip, regenerates src/generated,
# rewrites the SHA below to the commit it derived from, and opens one pull request
# carrying both. Tracking happens in atomic, reviewable steps rather than
# continuously.
#
# Resolving master's tip here at configure time instead would change the libmeos
# source on every push, which defeats the extension pipeline's ccache (a full
# libmeos recompile per build) and makes two runs of the same commit build different
# sources. A recorded SHA keeps builds reproducible and the cache warm between
# advances.
set(_MEOS_REF "5de12af90137a1955e3c2fa04ae42f44eb69dd8c")
message(STATUS "MEOS port: building MobilityDB at recorded ${_MEOS_REF}")

# FETCH_REF names the branch (always advertised) so the fetch works even when the
# server does not allow fetching an arbitrary commit SHA directly; REF then checks
# out master's resolved tip from that fetched history.
vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/MobilityDB/MobilityDB.git
    REF "${_MEOS_REF}"
    FETCH_REF refs/heads/master
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

# USER-APPROVED-PIN-WRITE (2026-06-23): removed the binding's own stale
# vcpkg_replace_string patches that INJECTED Datum accessors into pg_timestamp.h /
# utils/timestamp.h and re-exported add_date_int/add_timestamptz_interval into
# postgres_ext_defs.in.h. PROVEN against a clean `cmake -DMEOS=ON` install: clean
# pg_timestamp.h does not reference Int64GetDatum, the clean libmeos builds without
# these, and consumers reach add_date_int/add_timestamptz_interval by including the
# public leaf headers pg_date.h / pg_timestamp.h (see meos_wrapper_simple.hpp).
# These patches were the contamination that broke the binding's own include path.

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DMEOS=ON
        # Activate EVERY optional family so the standalone libmeos matches the full
        # catalog surface the binding generates against (catalog subseteq libmeos).
        # -DALL=ON FORCE-sets every family (ARROW/CBUFFER/H3/JSON/NPOINT/POINTCLOUD/
        # POSE/QUADBIN/RASTER; RGEO follows POSE via CMAKE_DEPENDENT_OPTION). Their
        # external dependencies come from this port's vcpkg.json — H3 from vcpkg h3,
        # RASTER from vcpkg gdal, POINTCLOUD from vcpkg libxml2 + zlib — so they build
        # on every triplet (x64/arm64/osx/wasm) with no system -dev packages, the
        # cross-arch equivalent of provision-meos's apt install set. POINTCLOUD builds
        # its pgPointCloud core (libpc) directly from lib/*.c as a CMake static library
        # with no pg_config (MobilityDB #1370), so -DALL now builds in the vcpkg model.
        -DALL=ON
        "-DJSON-C_LIBRARIES=${_MEOS_JSONC_LIB}"
        "-DJSON-C_INCLUDE_DIRS=${_MEOS_JSONC_INC}"
        # BUILD_SHARED_LIBS is deliberately NOT set here: vcpkg_cmake_configure
        # derives it from the triplet's VCPKG_LIBRARY_LINKAGE, which is static on
        # every triplet this extension is built for. A DuckDB extension ships as
        # one .duckdb_extension file with nowhere to put a sidecar libmeos, so
        # forcing a shared build here produced a binary that referenced
        # @rpath/libmeos.dylib and failed to load anywhere but the CI runner.
        # Build only the MEOS library, not the MEOS C test binaries: those link
        # the GEOS C++ API, which the arm64-linux vcpkg triplet does not carry.
        -DBUILD_TESTING=OFF
        -DCMAKE_C_FLAGS="-Dsession_timezone=meos_session_timezone"
        -DCMAKE_CXX_FLAGS="-Dsession_timezone=meos_session_timezone"
)

vcpkg_cmake_install()

# Guard the linkage the extension depends on. A DuckDB extension is distributed
# as one .duckdb_extension file, so a shared libmeos here yields a binary that
# references @rpath/libmeos.dylib (or libmeos.so) and loads only on the machine
# that built it. Fail during the port build, where the cause is obvious, rather
# than when a user downloads the artifact and dlopen reports a missing library.
file(GLOB _meos_shared
    "${CURRENT_PACKAGES_DIR}/lib/libmeos.so*"
    "${CURRENT_PACKAGES_DIR}/lib/libmeos.dylib"
    "${CURRENT_PACKAGES_DIR}/lib/meos.dll")
if(_meos_shared)
    message(FATAL_ERROR
        "MEOS port: a SHARED libmeos was built (${_meos_shared}). The DuckDB "
        "extension links MEOS statically; do not override BUILD_SHARED_LIBS.")
endif()
if(NOT EXISTS "${CURRENT_PACKAGES_DIR}/lib/libmeos.a"
   AND NOT EXISTS "${CURRENT_PACKAGES_DIR}/lib/meos.lib")
    message(FATAL_ERROR
        "MEOS port: no static libmeos was installed under "
        "${CURRENT_PACKAGES_DIR}/lib. The MobilityDB build must honour "
        "BUILD_SHARED_LIBS=OFF.")
endif()

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
