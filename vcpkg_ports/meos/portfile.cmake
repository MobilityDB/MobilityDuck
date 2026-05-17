# TEMPORARY PROVISIONAL PIN. The core tcbuffer port is a clean clone that
# binds the tcbuffer MF-JSON I/O (tcbufferFromMFJSON). That MEOS surface
# only exists on MobilityDB PR #1051 (feat/tcbuffer-mfjson), not yet on
# MobilityDB/MobilityDB master, so the pin points at that PR's head commit
# on the contributor fork. This is provisional pending the #134 -> #145
# MobilityDuck chain plus PR #1051 merging into MobilityDB master.
#
# Flip-to-merged-master recipe (apply once #1051 is merged AND #145 has
# landed): set REPO back to MobilityDB/MobilityDB, set REF to the merged
# master commit that includes the tcbuffer MF-JSON change, and recompute
#   curl -sL https://github.com/MobilityDB/MobilityDB/archive/<sha>.tar.gz | sha512sum
# for SHA512. Then delete this comment block. The OPTIONS below (the #145
# CBUFFER/NPOINT/POSE/RGEO enablers) are unchanged by this pin and compose
# with any REPO/REF/SHA512.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO estebanzimanyi/MobilityDB
    REF e624027f5b59f483476c8e5c26471f9e252a5e61
    SHA512 6c1d82b7a3ca8c121ecf0766609ff71c38f678dd18a116591d9e4d295eec3f12b6d338321a460dd046c9e3805311137a22a427f1fe4dee75b4e5c0bd6a822ea7
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
