vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO estebanzimanyi/MobilityDB
    REF f4b82557ab2992ddd8f343710c48b0107063dbbb
    SHA512 50d30dd2c3bf634bc2148d32b1178c9e8fe41654ff5386e7db0f6c10b77ff5ce5a1f7352d9dc85a1432ea1300c15b4f7a20ba2991ad2e31772f03ffd9be79fd2
)

# No source patching is required at this pin: the th3 build-gap + postgres/utils
# patches older pins needed are fixed upstream (th3 is an integrated family;
# postgres/ -> pgtypes/ via #751), and the pgtypes standalone-build includes are
# depth-independent (bare `#include "meos_error.h"` + meos/include on the pgtypes
# path) as of ecosystem-pin-2026-06-14c.

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
        -DH3=ON
        "-DH3_LIBRARY=${_MEOS_H3_LIB}"
        "-DH3_INCLUDE_DIR=${_MEOS_H3_INC}"
        -DBUILD_SHARED_LIBS=ON
        # Build only the MEOS library, not the MEOS C test binaries: those link
        # the GEOS C++ API, which the arm64-linux vcpkg triplet does not carry.
        -DBUILD_TESTING=OFF
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
