# TEMPORARY PROVISIONAL PIN. The core tcbuffer and tnpoint ports are
# clean clones that bind the per-type MF-JSON I/O: tcbufferFromMFJSON
# needs the tcbuffer MF-JSON support from MobilityDB PR #1051
# (feat/tcbuffer-mfjson), and tnpointFromMFJSON needs the network-point
# MF-JSON support from MobilityDB PR #951 (split/tnpoint-mfjson-io,
# wired through the generic temporal_from_mfjson dispatch). Neither is
# on MobilityDB/MobilityDB master yet, so the pin points at an
# integration branch on the contributor fork that composes both PRs on
# top of MobilityDB master: estebanzimanyi/MobilityDB
# meos-mduck-ports-tcbuffer-tnpoint = cherry-pick #1051
# (e624027f5b59f483476c8e5c26471f9e252a5e61) then #951
# (800f80bfdc8dc4ce86516aacb85e48131bcf7454); the two only collide in
# disjoint #if CBUFFER vs #if NPOINT blocks of type_in.c / type_out.c
# and were union-merged with no logic change. This is provisional
# pending the #134 -> #145 MobilityDuck chain plus PRs #1051 and #951
# merging into MobilityDB master.
#
# Flip-to-merged-master recipe (apply once #1051 AND #951 are merged AND
# #145 has landed): set REPO back to MobilityDB/MobilityDB, set REF to
# the merged master commit that includes both MF-JSON changes, and
# recompute
#   curl -sL https://github.com/MobilityDB/MobilityDB/archive/<sha>.tar.gz | sha512sum
# for SHA512. Then delete this comment block. The OPTIONS below (the #145
# CBUFFER/NPOINT/POSE/RGEO enablers) are unchanged by this pin and compose
# with any REPO/REF/SHA512.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO estebanzimanyi/MobilityDB
    REF 1a4f498b1689fdcbe77ae0bbaf8b53df1eb59554
    SHA512 da7c32c8f02b684dd8f68315ad1d128a59aa2027deae971b6fe94cba2de9303ca53b30676c98ba4e48a6721ab181007ce58ca4263ce2037554f171af6eca2f7a
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
