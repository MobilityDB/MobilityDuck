vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO estebanzimanyi/MobilityDB
    REF 3db47f887c61f049a6a03db55c48bedf6d10eee4
    SHA512 b73123bca036813c43937f90f0d0ce45af5cb9e39d6a597304199d21ae854212319a3a7b58ecd075eb5678d89d6d5990b9faf63dd29bd8c9a4e2ed83282b94c5
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
        # (tcbuffer, tnpoint, tpose, trgeometry) into MobilityDuck. RGEO
        # depends on POSE.
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
