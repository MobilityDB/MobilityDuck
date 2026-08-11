# MobilityDuck builds its MEOS source from a committed MobilityDB commit pin.
# A fixed ref keeps the libmeos source identical across CI runs, so the extension
# pipeline's ccache reuses the libmeos compiler objects instead of recompiling the
# whole library every build — a moving master tip changes the source on every push
# and defeats that cache. The committed generated UDF surface comes from this SAME
# pin, so the libmeos this port builds and the surface that links against it always
# match one upstream commit. Advance the tracked version by bumping this SHA and
# regenerating the surface from it.
set(_MEOS_REF "e9883a1e5134678b6ae01b0ebb9410f7f4ec1f20")
message(STATUS "MEOS port: building MobilityDB at pinned ${_MEOS_REF}")

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

# Upstream gap: at this pin `meos/CMakeLists.txt` HARDCODES the linkage of the
# standalone library — `if(MEOS) add_library(meos SHARED ...)`. `BUILD_SHARED_LIBS`
# is read nowhere in the MobilityDB tree, so vcpkg's `-DBUILD_SHARED_LIBS=OFF`
# (derived from the static triplet, and visibly present in the configure line) is
# inert: `-DMEOS=ON` alone forces a .dylib/.so. The STATIC branch is the `else()`
# — i.e. MobilityDB-the-PostgreSQL-extension — which does not run the standalone
# install rules or emit the generated meos.h surface, so it is not a usable
# substitute. An earlier pin (742c1fb) declared `add_library(meos ${OBJECTS})`
# with no explicit type and carried an `option(BUILD_SHARED_LIBS)`, which is why
# the same port produced a static libmeos.a before the pin advanced.
#
# Restore that behaviour: keep the SHARED branch for standalone consumers who ask
# for it, and add a static branch for MEOS=ON + BUILD_SHARED_LIBS=OFF. The APPLE
# `-Wl,-undefined,dynamic_lookup` / SUFFIX ".dylib" block must stay inside the
# shared branch only — applied to an archive it would emit an ar file misnamed
# libmeos.dylib and leave MEOS's own external symbols unresolved.
vcpkg_replace_string(
    "${SOURCE_PATH}/meos/CMakeLists.txt"
    [=[if(MEOS)
  # Build as SHARED library for standalone use
  add_library(${MEOS_LIB_NAME} SHARED ${MEOS_OBJECTS})
  message(STATUS "Building MEOS as SHARED library")

  if(APPLE)
    set_target_properties(${MEOS_LIB_NAME} PROPERTIES
      SUFFIX ".dylib"
      LINK_FLAGS "-Wl,-undefined,dynamic_lookup"
    )
  endif()
else()]=]
    [=[if(MEOS AND BUILD_SHARED_LIBS)
  # Build as SHARED library for standalone use
  add_library(${MEOS_LIB_NAME} SHARED ${MEOS_OBJECTS})
  message(STATUS "Building MEOS as SHARED library")

  if(APPLE)
    set_target_properties(${MEOS_LIB_NAME} PROPERTIES
      SUFFIX ".dylib"
      LINK_FLAGS "-Wl,-undefined,dynamic_lookup"
    )
  endif()
elseif(MEOS)
  # Standalone MEOS with BUILD_SHARED_LIBS=OFF: same install surface as the
  # shared branch, but an archive the consumer can absorb into its own binary.
  #
  # MEOS_OBJECTS covers only the OBJECT libraries. pgtypes and postgis are
  # STATIC, and the `target_link_libraries(meos pgtypes postgis)` calls below
  # merely record usage requirements on a STATIC target — unlike a SHARED
  # libmeos, which absorbed their members at link time. Left alone, libmeos.a
  # ships without ~300 symbols it references itself (bool_in, cstring_to_text,
  # GEOS2LWGEOM, lwgeom_*, pc_*), and the consumer would have to discover
  # libpgtypes.a/libpostgis.a/libpc.a and order them by hand — around a genuine
  # cycle, since pgtypes calls back into meos_error. Fold the underlying object
  # libraries in instead, so the archive carries exactly what the shared library
  # did. Both subdirectories are added before meos/ in the top-level
  # CMakeLists, so the targets already exist; the optional ones follow the same
  # family switches that gate MEOS_OBJECTS above. libpgcommon is deliberately
  # omitted, mirroring its commented-out entry in POSTGIS_OBJECTS.
  set(MEOS_STATIC_OBJECTS "$<TARGET_OBJECTS:pgtypes>")
  foreach(_meos_static_dep liblwgeom ryu librtcore pointcloud_libpc)
    if(TARGET ${_meos_static_dep})
      list(APPEND MEOS_STATIC_OBJECTS "$<TARGET_OBJECTS:${_meos_static_dep}>")
    endif()
  endforeach()

  add_library(${MEOS_LIB_NAME} STATIC ${MEOS_OBJECTS} ${MEOS_STATIC_OBJECTS})
  message(STATUS "Building MEOS as STATIC library (BUILD_SHARED_LIBS=OFF)")

  set_target_properties(${MEOS_LIB_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
else()]=]
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
        # The flag only reaches the meos target because of the linkage patch
        # above; upstream ignores it at this pin.
        # The MEOS families are compiled as OBJECT libraries and folded into
        # libmeos via $<TARGET_OBJECTS:...>. Those objects do not inherit the
        # meos target's POSITION_INDEPENDENT_CODE, and a static libmeos is
        # ultimately linked into a shared .duckdb_extension, so PIC has to be
        # set tree-wide rather than per-target (a no-op on arm64 macOS, required
        # on x64 Linux where non-PIC objects fail the final shared link).
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
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
