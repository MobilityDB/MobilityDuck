vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO estebanzimanyi/MobilityDB
    REF dfdd25545dde07e5b5ae6771da500a3bdb81ee7d
    SHA512 80091ba952bea195c61fd82d0e700bc3a608f60fd4a5bc6d9f4ae480df94ef5bcbabadf178ffac3ee8e7684b64db0c17b416d2d18f9c11125ade530757ab9c05
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
        -DH3=ON
        "-DH3_LIBRARY=${_MEOS_H3_LIB}"
        "-DH3_INCLUDE_DIR=${_MEOS_H3_INC}"
        -DBUILD_SHARED_LIBS=ON
        -DCMAKE_C_FLAGS="-Dsession_timezone=meos_session_timezone"
        -DCMAKE_CXX_FLAGS="-Dsession_timezone=meos_session_timezone"
)

vcpkg_cmake_build(TARGET all)
vcpkg_cmake_install()

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
