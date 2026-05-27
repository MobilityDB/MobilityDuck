vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO estebanzimanyi/MobilityDB
    # MobilityDB accumulate/parity-1.4 tip (PR #22) — carries the h3indexset
    # static-geometry API (geo_to_h3index_set, ever_eq_anyof_h3indexset_th3index)
    # and the extended-type C API (tcbuffer_from_mfjson, …) that the older
    # dfdd2554 pin lacked.
    REF bb659c69381a1d44ea6c9cfd32207cdae8f80f3a
    SHA512 15e635cef54845a3b2f1d03c568cbbafa26cf8b27a4f47ec1a5d6f61597ff83f3485dc12a0dd61dcb56afd924889f2e8ac0f02e0dc19bf8609d2e89dbaa9aae9
)

vcpkg_replace_string(
    "${SOURCE_PATH}/postgres/utils/CMakeLists.txt"
    "set_property(TARGET utils PROPERTY POSITION_INDEPENDENT_CODE ON)"
    [=[
set_property(TARGET utils PROPERTY POSITION_INDEPENDENT_CODE ON)

if(MEOS)
  target_include_directories(utils PRIVATE "${CMAKE_SOURCE_DIR}/meos/include")
endif()
]=]
)

# Upstream gap at commit beddae670: `meos/include/h3/th3index_internal.h`
# does `#include <fmgr.h>` unconditionally.  `fmgr.h` is a PG-internal
# header and is not bundled in MEOS's `postgres/` subtree, so the
# standalone MEOS build of `meos/src/h3/h3index.c` fails with
# `fatal error: fmgr.h: No such file or directory`.  Guard the
# include with `#if !MEOS`, mirroring the same idiom already used by
# `meos/include/temporal/temporal.h`.
vcpkg_replace_string(
    "${SOURCE_PATH}/meos/include/h3/th3index_internal.h"
    [=[
#include <postgres.h>
#include <fmgr.h>
]=]
    [=[
#include <postgres.h>
#if ! MEOS
#include <fmgr.h>
#endif
]=]
)

# Upstream gap at commit beddae670: `meos/CMakeLists.txt` builds the
# `h3` OBJECT library (via `add_subdirectory(h3)` + `add_library`)
# but the `PROJECT_OBJECTS` list that feeds the final
# `add_library(meos ${PROJECT_OBJECTS})` lists every other optional
# family (cbuffer / npoint / pose / rgeo) and silently omits `h3`.
# Without this injection libmeos ships without H3 symbols, so any
# consumer linking against `meos` sees ~120 `undefined reference to
# 'th3index_*'` link errors.
vcpkg_replace_string(
    "${SOURCE_PATH}/meos/CMakeLists.txt"
    [=[if(RGEO)
  message(STATUS "Including rigid geometries")
  set(PROJECT_OBJECTS ${PROJECT_OBJECTS} "$<TARGET_OBJECTS:rgeo>")
endif()]=]
    [=[if(RGEO)
  message(STATUS "Including rigid geometries")
  set(PROJECT_OBJECTS ${PROJECT_OBJECTS} "$<TARGET_OBJECTS:rgeo>")
endif()
if(H3)
  message(STATUS "Including temporal H3 index (th3index)")
  set(PROJECT_OBJECTS ${PROJECT_OBJECTS} "$<TARGET_OBJECTS:h3>")
endif()]=]
)

# Upstream gap at commit beddae670: `meos/CMakeLists.txt` carries
# `install()` rules for `meos_npoint.h` / `meos_pose.h` /
# `meos_rgeo.h` / `meos_cbuffer.h` but no rule for `meos_h3.h`.
# Without it the H3 public header is missing from the installed
# `include/` directory, so any consumer of `#include <meos_h3.h>`
# fails to compile.
vcpkg_replace_string(
    "${SOURCE_PATH}/meos/CMakeLists.txt"
    [=[if(RGEO)
  install(
    FILES "${CMAKE_SOURCE_DIR}/meos/include/meos_rgeo.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()]=]
    [=[if(RGEO)
  install(
    FILES "${CMAKE_SOURCE_DIR}/meos/include/meos_rgeo.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()
if(H3)
  install(
    FILES "${CMAKE_SOURCE_DIR}/meos/include/meos_h3.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()]=]
)

# Upstream gap at commit beddae670: the h3-side source files call
# `ensure_srid_is_latlong()` (declared in
# `meos/include/geo/tgeo_spatialfuncs.h`) without including that
# header, yielding implicit-declaration errors under `MEOS=1`.
foreach(_h3_src
        meos/src/h3/h3_geo.c
        meos/src/h3/th3index_latlng.c
        meos/src/h3/th3index_metrics.c)
    if(EXISTS "${SOURCE_PATH}/${_h3_src}")
        vcpkg_replace_string(
            "${SOURCE_PATH}/${_h3_src}"
            "#include <meos_internal_geo.h>"
            [=[
#include <meos_internal_geo.h>

#include "geo/tgeo_spatialfuncs.h"
]=]
        )
    endif()
endforeach()

# vcpkg installs h3 at the per-triplet
# `installed/<triplet>/{lib,include/h3}` layout, but MEOS's own
# `find_library(NAMES h3)` / `find_path(NAMES h3api.h PATH_SUFFIXES h3)`
# does not consult vcpkg's CMAKE_PREFIX_PATH on every triplet
# (notably `arm64-linux-release`).  Pass the resolved paths explicitly.
set(_meos_h3_lib_candidates
    "${CURRENT_INSTALLED_DIR}/lib/libh3.a"
    "${CURRENT_INSTALLED_DIR}/lib/libh3.so"
    "${CURRENT_INSTALLED_DIR}/lib/libh3${CMAKE_STATIC_LIBRARY_SUFFIX}"
    "${CURRENT_INSTALLED_DIR}/lib/libh3${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(_MEOS_H3_LIB "")
foreach(_cand IN LISTS _meos_h3_lib_candidates)
    if(EXISTS "${_cand}")
        set(_MEOS_H3_LIB "${_cand}")
        break()
    endif()
endforeach()
if(NOT _MEOS_H3_LIB)
    message(FATAL_ERROR "MEOS port: cannot locate vcpkg-installed libh3 under ${CURRENT_INSTALLED_DIR}/lib")
endif()
# h3's header lands at `include/h3/h3api.h` (subdirectory).  MEOS
# source uses `#include <h3api.h>` so the include path must point
# at `include/h3`.
set(_MEOS_H3_INC_CANDIDATES
    "${CURRENT_INSTALLED_DIR}/include/h3"
    "${CURRENT_INSTALLED_DIR}/include")
set(_MEOS_H3_INC "")
foreach(_cand IN LISTS _MEOS_H3_INC_CANDIDATES)
    if(EXISTS "${_cand}/h3api.h")
        set(_MEOS_H3_INC "${_cand}")
        break()
    endif()
endforeach()
if(NOT _MEOS_H3_INC)
    message(FATAL_ERROR "MEOS port: cannot locate vcpkg-installed h3api.h under ${CURRENT_INSTALLED_DIR}/include or ${CURRENT_INSTALLED_DIR}/include/h3")
endif()

# Newer C compilers (CI's GCC 14 / gcc-toolset-14 on Linux and AppleClang 17
# on macOS) promote several long-standing C laxities to hard errors by default:
# implicit-function-declaration, incompatible-pointer-types, int-conversion.
# The pinned MEOS builds cleanly on GCC 11 (these are warnings there) but its
# postgres-vendored code and a few internal call sites rely on those laxities,
# so the build fails on the newer toolchains. Downgrade exactly that C-permerror
# set back to warnings. These -Wno-error flags are honoured by BOTH GCC and
# Clang (unlike -fpermissive, which Clang silently ignores for C, leaving the
# macOS build broken). add_compile_options is inherited by every MEOS target
# defined afterwards.
vcpkg_replace_string(
    "${SOURCE_PATH}/CMakeLists.txt"
    "add_compile_definitions(_USE_MATH_DEFINES)"
    "add_compile_definitions(_USE_MATH_DEFINES)\nadd_compile_options(-Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion)"
)

# DIAGNOSTIC (#170, NOT for merge): when the PostGIS hex parser rejects an
# odd-length hex on macOS arm64, dump a backtrace + the offending string so
# the CI log reveals exactly which producer/caller built the bad hex.
vcpkg_replace_string(
    "${SOURCE_PATH}/postgis/liblwgeom/lwin_wkb.c"
    "#include <math.h>\n#include <limits.h>"
    "#include <math.h>\n#include <limits.h>\n#include <stdio.h>\n#include <execinfo.h>"
)
vcpkg_replace_string(
    "${SOURCE_PATH}/postgis/liblwgeom/lwin_wkb.c"
    "lwerror(\"Invalid hex string, length (%zu) has to be a multiple of two!\", hexsize)"
    "do { void *_bt[40]; int _n = backtrace(_bt, 40); char **_s = backtrace_symbols(_bt, _n); fprintf(stderr, \"[#170 DIAG] odd hexsize=%zu hex=[%.200s]\\n\", hexsize, hexbuf); for (int _k = 0; _k < _n; _k++) fprintf(stderr, \"[#170 BT] %s\\n\", _s[_k]); free(_s); lwerror(\"Invalid hex string, length (%zu) has to be a multiple of two!\", hexsize); } while (0)"
)

# Upstream MEOS-standalone gap: meos/include/pointcloud/{pcpoint,pcpatch}.h
# define DatumGetPcpointP / DatumGetPcpatchP via PG_DETOAST_DATUM
# UNCONDITIONALLY, unlike every other type header (temporal.h etc.) which
# uses `((T *) DatumGetPointer(X))` under `#if MEOS`. PG_DETOAST_DATUM lives
# only in PostgreSQL's fmgr.h (not bundled for MEOS), so libmeos built with
# -DPOINTCLOUD=ON has unresolved `PG_DETOAST_DATUM` at link. Mirror the MEOS
# branch (MEOS values are never TOASTed). FIX UPSTREAM: add the #if MEOS guard.
vcpkg_replace_string(
    "${SOURCE_PATH}/meos/include/pointcloud/pcpoint.h"
    "((Pcpoint *) PG_DETOAST_DATUM(X))"
    "((Pcpoint *) DatumGetPointer(X))"
)
vcpkg_replace_string(
    "${SOURCE_PATH}/meos/include/pointcloud/pcpatch.h"
    "((Pcpatch *) PG_DETOAST_DATUM(X))"
    "((Pcpatch *) DatumGetPointer(X))"
)

# pgPointCloud enabler. -DPOINTCLOUD=ON makes meos/src/pointcloud/
# CMakeLists.txt FATAL_ERROR unless pointcloud-pg/lib/libpc.a exists; it is
# built as a side effect of pgPointCloud's `./autogen.sh && ./configure &&
# make`, which cannot run here (no autotools-usable PostgreSQL, no pg_config).
# The vendored pointcloud-pg/lib sources need only libxml2 and zlib (no
# PostgreSQL headers), and pointcloud-pg/lib/Makefile builds libpc.a directly
# via `ar rs` with the XML2/ZLIB CPPFLAGS from config.mk. So we generate
# config.mk + lib/pc_config.h the way pgPointCloud's autotools would (filling
# only the @VARS@ the lib OBJS consume, with vcpkg's libxml2/zlib paths), then
# build the libpc.a archive target directly. CUnit/LazPerf stay disabled.
set(POINTCLOUD_DIR "${SOURCE_PATH}/pointcloud-pg")

# pgPointCloud's lib/Makefile uses a plain `cc` with no awareness of vcpkg's
# macOS cross-compile settings, so on the osx builds (e.g. an x86_64 target
# compiled on an arm64 runner) it emits objects of the wrong architecture and
# the final extension link rejects libpc.a ("found architecture 'arm64',
# required architecture 'x86_64'"). Mirror vcpkg's macOS target flags into the
# libpc build. Empty (no-op) on Linux.
set(_PC_OSX_FLAGS "")
if(VCPKG_TARGET_IS_OSX)
    # Prefer the triplet's explicit arch list; fall back to the always-set
    # VCPKG_TARGET_ARCHITECTURE (community osx triplets often leave
    # VCPKG_OSX_ARCHITECTURES empty).
    if(VCPKG_OSX_ARCHITECTURES)
        foreach(_a IN LISTS VCPKG_OSX_ARCHITECTURES)
            string(APPEND _PC_OSX_FLAGS " -arch ${_a}")
        endforeach()
    elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
        string(APPEND _PC_OSX_FLAGS " -arch x86_64")
    elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
        string(APPEND _PC_OSX_FLAGS " -arch arm64")
    endif()
    if(VCPKG_OSX_DEPLOYMENT_TARGET)
        string(APPEND _PC_OSX_FLAGS " -mmacosx-version-min=${VCPKG_OSX_DEPLOYMENT_TARGET}")
    endif()
    if(VCPKG_OSX_SYSROOT)
        string(APPEND _PC_OSX_FLAGS " -isysroot ${VCPKG_OSX_SYSROOT}")
    endif()
endif()

file(WRITE "${POINTCLOUD_DIR}/config.mk"
"CC = cc
CFLAGS = -O2 -fPIC${_PC_OSX_FLAGS} -Dstringbuffer_append=pc_stringbuffer_append -Dstringbuffer_aprintf=pc_stringbuffer_aprintf -Dstringbuffer_avprintf=pc_stringbuffer_avprintf -Dstringbuffer_clear=pc_stringbuffer_clear -Dstringbuffer_copy=pc_stringbuffer_copy -Dstringbuffer_create=pc_stringbuffer_create -Dstringbuffer_create_with_size=pc_stringbuffer_create_with_size -Dstringbuffer_destroy=pc_stringbuffer_destroy -Dstringbuffer_getlength=pc_stringbuffer_getlength -Dstringbuffer_getstring=pc_stringbuffer_getstring -Dstringbuffer_getstringcopy=pc_stringbuffer_getstringcopy -Dstringbuffer_lastchar=pc_stringbuffer_lastchar -Dstringbuffer_makeroom=pc_stringbuffer_makeroom -Dstringbuffer_release_string=pc_stringbuffer_release_string -Dstringbuffer_set=pc_stringbuffer_set -Dstringbuffer_trim_trailing_white=pc_stringbuffer_trim_trailing_white -Dstringbuffer_trim_trailing_zeroes=pc_stringbuffer_trim_trailing_zeroes
CXXFLAGS += -fPIC -std=c++0x${_PC_OSX_FLAGS}
SQLPP =

XML2_CPPFLAGS = -I${CURRENT_INSTALLED_DIR}/include/libxml2
XML2_LDFLAGS = -L${CURRENT_INSTALLED_DIR}/lib -lxml2

ZLIB_CPPFLAGS = -I${CURRENT_INSTALLED_DIR}/include
ZLIB_LDFLAGS = -L${CURRENT_INSTALLED_DIR}/lib -lz

CUNIT_CPPFLAGS =
CUNIT_LDFLAGS =

PG_CONFIG =
PGXS =

LIB_A = libpc.a
LIB_A_LAZPERF = liblazperf.a

LAZPERF_STATUS = disabled
LAZPERF_CPPFLAGS =

PGSQL_MAJOR_VERSION =
")

# lib/pc_config.h: pc_api.h does `#include \"pc_config.h\"`, normally emitted
# by config.status. Mirror the no-lazperf / no-cunit autotools output (leave
# HAVE_LAZPERF / HAVE_CUNIT undefined). POINTCLOUD_VERSION = Version.config.
file(WRITE "${POINTCLOUD_DIR}/lib/pc_config.h"
"/* #undef LIBXML2_VERSION */

/* #undef PGSQL_VERSION */

/* #undef HAVE_LAZPERF */

/* #undef HAVE_CUNIT */

#define PROJECT_SOURCE_DIR \"${POINTCLOUD_DIR}\"

#define POINTCLOUD_VERSION \"1.2.5\"
")

# Build the libpc.a archive target directly (NOT `all`, which recurses into
# cunit/ and needs CUnit). config.mk is included by lib/Makefile.
vcpkg_execute_required_process(
    COMMAND make -C "${POINTCLOUD_DIR}/lib" libpc.a
    WORKING_DIRECTORY "${POINTCLOUD_DIR}/lib"
    LOGNAME "build-libpc-${TARGET_TRIPLET}"
)

# NOTE: the stringbuffer_* symbol collision between pgPointCloud's lib
# (a verbatim fork of PostGIS liblwgeom/stringbuffer.c) and MEOS's own
# bundled liblwgeom is resolved at COMPILE time by the
# -Dstringbuffer_*=pc_stringbuffer_* defines injected into config.mk's
# CFLAGS above. That renames every definition AND reference inside libpc.a
# to a private pc_stringbuffer_* namespace, keeping libpc.a self-contained
# (it does NOT depend on MEOS providing those symbols) and needing no
# post-build symbol surgery. This is portable across GCC and Clang, unlike
# the previous objcopy --redefine-syms pass (GNU objcopy is absent on macOS)
# and unlike dropping stringbuffer.o (which left other libpc objects with
# unresolved stringbuffer_* references in targets that don't link libmeos).

# meos/src/pointcloud/CMakeLists.txt checks exactly this path.
if(NOT EXISTS "${POINTCLOUD_DIR}/lib/libpc.a")
    message(FATAL_ERROR
        "pgPointCloud enabler failed: ${POINTCLOUD_DIR}/lib/libpc.a "
        "was not produced by the libpc.a make target.")
endif()

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DMEOS=ON
        # Opt-in MEOS modules required to port the extended temporal types
        # (tcbuffer, tnpoint, tpose, trgeometry) into MobilityDuck. RGEO is a
        # dependent option that requires POSE.
        -DCBUFFER=ON
        -DNPOINT=ON
        -DPOSE=ON
        -DRGEO=ON
        -DPOINTCLOUD=ON
        -DH3=ON
        "-DH3_LIBRARY=${_MEOS_H3_LIB}"
        "-DH3_INCLUDE_DIR=${_MEOS_H3_INC}"
        -DBUILD_SHARED_LIBS=ON
        -DCMAKE_C_FLAGS="-Dsession_timezone=meos_session_timezone"
        -DCMAKE_CXX_FLAGS="-Dsession_timezone=meos_session_timezone"
)

vcpkg_cmake_build(TARGET all)
vcpkg_cmake_install()

# meos/src/pointcloud/CMakeLists.txt links libpc.a into the `pointcloud`
# OBJECT library as a usage requirement only: the static libmeos.a does not
# absorb libpc.a's objects nor carry a link interface, so a consumer linking
# libmeos.a sees unresolved pc_point_get_x/y/z/... . Install libpc.a alongside
# libmeos.a and propagate it (+ libxml2 / zlib) through MEOS::meos's
# INTERFACE_LINK_LIBRARIES so the extension link resolves the pgPointCloud
# symbols.
file(INSTALL "${SOURCE_PATH}/pointcloud-pg/lib/libpc.a"
     DESTINATION "${CURRENT_PACKAGES_DIR}/lib")

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
  # When MEOS was built with -DPOINTCLOUD=ON, libmeos.a calls into the
  # pgPointCloud archive libpc.a (and libxml2 / zlib). The static libmeos.a
  # does not carry that link interface, so propagate it here.
  set(_meos_libdir "${CMAKE_CURRENT_LIST_DIR}/../../lib")
  if (EXISTS "${_meos_libdir}/libpc.a")
    set(_meos_pc_iface "${_meos_libdir}/libpc.a")
    file(GLOB _meos_xml2 "${_meos_libdir}/libxml2.a" "${_meos_libdir}/libxml2.lib")
    file(GLOB _meos_zlib "${_meos_libdir}/libz.a" "${_meos_libdir}/libzlib.a" "${_meos_libdir}/zlib.lib")
    if (_meos_xml2)
      list(APPEND _meos_pc_iface ${_meos_xml2})
    endif()
    if (_meos_zlib)
      list(APPEND _meos_pc_iface ${_meos_zlib})
    endif()
    # libxml2 uses iconv for charset conversion. On Linux iconv lives in libc,
    # but on macOS it is a separate system library (libiconv), so the final
    # link otherwise fails with "Undefined symbols: _iconv/_iconv_open/
    # _iconv_close for architecture x86_64".
    if (APPLE)
      list(APPEND _meos_pc_iface "-liconv")
    endif()
    set_target_properties(MEOS::meos PROPERTIES
      INTERFACE_LINK_LIBRARIES "${_meos_pc_iface}"
    )
  endif()
endif()
]=])

file(WRITE "${CURRENT_PACKAGES_DIR}/share/meos/usage" [=[
MEOS installed.

CMake:
  find_package(MEOS CONFIG REQUIRED)
  target_link_libraries(your_target PRIVATE MEOS::meos)
]=])
