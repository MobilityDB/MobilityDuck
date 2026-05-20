# MEOS-1.4 integration branch on the user's fork — combines all 15 open
# MEOS-1.4 bump PRs into one buildable commit:
#   #1075 portable bare-name aliases    #1090 geog_in fall-through fix
#   #1081 tcbuffer accessors            #1094 tolerance fix (0.0)
#   #1082 tnpoint accessors             #1095 meos_error contract doc (Wave 1)
#   #1083 tcbuffer/tpose ctors          #1096 meos_error CI ratchet (Wave 2)
#   #1084 from_base time ctors          #1097 per-site fall-through fixes (Wave 3)
#   #1085 tpose_from_mfjson             #1098 math/aggfuncs/datagen fixes (Wave 4)
#   #1088 geodetic spatialrels          #1100 ea_* geodetic completion (closes #1092)
#                                       #1101 signature uniformization (closes #1079)
# Branch: https://github.com/estebanzimanyi/MobilityDB/tree/integration/meos-1.4-bump
# Both libmeos and libMobilityDB-1.4 built green from this SHA (Linux x86_64,
# 2026-05-20).  Once the 15 PRs land on MobilityDB master, switch the REPO line
# back to `MobilityDB/MobilityDB` and update REF/SHA512 to that master tip.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO estebanzimanyi/MobilityDB
    REF 80c24f46d042baa2613515b0f5b82255f21fb522
    SHA512 c21d978270bcd2e996506bc692abd3c3a8e57f31032443e07107cdc25ac8b7ebed7df42f71ad8b740e732ea35317524d3b14089277038efabb774a2dd53c0b23
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
