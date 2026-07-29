cmake_minimum_required(VERSION 3.5)

# -----------------------------
# Project
# -----------------------------
set(TARGET_NAME mobilityduck)
project(${TARGET_NAME} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/cmake" ${CMAKE_MODULE_PATH})

# -----------------------------
# Endianness for WKB (MEOS flag)
# -----------------------------
include(TestBigEndian)
TEST_BIG_ENDIAN(IS_BIG_ENDIAN)
if(IS_BIG_ENDIAN)
  add_definitions(-DMEOS_IS_BIG_ENDIAN=1)
else()
  add_definitions(-DMEOS_IS_BIG_ENDIAN=0)
endif()

# -----------------------------
# Dependencies (vcpkg)
# -----------------------------
find_package(GEOS CONFIG REQUIRED)
find_package(PROJ CONFIG REQUIRED)
find_package(GSL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(json-c CONFIG QUIET)
if(NOT json-c_FOUND)
  find_package(JSON-C REQUIRED)
endif()

# MEOS from overlay port
find_package(MEOS CONFIG REQUIRED)

# H3 headers: the all-families libmeos exposes meos_h3.h, which includes
# <h3api.h>. SIBLING-FROM MobilityDB CMakeLists.txt H3 block (find_path
# H3_INCLUDE_DIR NAMES h3api.h PATH_SUFFIXES h3) so code compiling against
# meos_h3.h sees the system libh3 headers, just as the meos port (-DH3=ON) does.
# The dir is attached to the extension targets below (mirroring the json-c
# block) because build_*_extension targets don't inherit directory includes.
# SIBLING-FROM MobilityDB CMakeLists.txt H3 block; NO_CMAKE_FIND_ROOT_PATH + explicit
# system PATHS are the binding-side adaptation: under the vcpkg toolchain find_path is
# confined to the vcpkg root and never sees the system /usr/include/h3 that libh3-dev ships.
find_path(H3_INCLUDE_DIR NAMES h3api.h
  PATH_SUFFIXES h3
  PATHS /usr/include /usr/local/include
  NO_CMAKE_FIND_ROOT_PATH)
if(NOT H3_INCLUDE_DIR)
  message(FATAL_ERROR "h3api.h not found; install libh3-dev (the meos port is built with -DH3=ON).")
endif()

if(TARGET GEOS::geos_c)
  set(GEOS_TGT GEOS::geos_c)
elseif(TARGET GEOS::geos)
  set(GEOS_TGT GEOS::geos)
else()
  message(FATAL_ERROR "Neither GEOS::geos_c nor GEOS::geos was found.")
endif()

# -----------------------------
# DuckDB extension targets
# -----------------------------
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

include_directories(src/include)
# Vendored duckdb-spatial headers are third-party: include them as SYSTEM so their
# warnings (e.g. the two sgl.hpp variants redefining SGL_ASSERT) do not surface in
# MobilityDuck translation units. MDB's own src/include stays non-SYSTEM (full warnings).
include_directories(SYSTEM duckdb-spatial/src)
include_directories(SYSTEM duckdb-spatial/src/spatial)

set(EXTENSION_SOURCES
    src/spatial_ref_sys_csv.inc
    src/mobilityduck_extension.cpp
    src/generated/generated_temporal_udfs.cpp
    src/generated/generated_type_registration.cpp
    src/temporal/temporal.cpp
    src/temporal/temporal_functions.cpp
    src/temporal/temporal_aggregates.cpp
    src/temporal/tbox.cpp
    src/temporal/tbox_functions.cpp
    src/geo/stbox.cpp
    src/geo/stbox_functions.cpp
    src/geo/tgeompoint.cpp
    src/geo/tgeompoint_functions.cpp
    src/geo/tgeogpoint.cpp
    src/geo/tgeogpoint_functions.cpp
    src/temporal/set.cpp
    src/temporal/set_functions.cpp
    src/temporal/span.cpp
    src/temporal/span_functions.cpp
    src/temporal/span_aggregates.cpp
    src/temporal/temporal_aggregates.cpp
    src/temporal/span_table_functions.cpp
    src/geo/geoset.cpp
    src/geo/spatial_aggregates.cpp
    src/temporal/spanset.cpp
    src/temporal/spanset_functions.cpp
    src/geo/tgeometry.cpp
    src/geo/tgeometry_in_out.cpp
    src/geo/tgeometry_ops.cpp
    src/geo/tgeography.cpp
    src/geo/tgeography_in_out.cpp
    src/geo/tgeography_ops.cpp
    src/pointcloud/tpcpoint.cpp
    src/pointcloud/tpcpoint_in_out.cpp
    src/pointcloud/tpcpatch.cpp
    src/pointcloud/tpcpatch_in_out.cpp
    src/geo/tgeogpoint.cpp
    src/geo/tgeogpoint_in_out.cpp
    src/geo/tgeogpoint_ops.cpp
    src/quadbin/tquadbin.cpp
    src/temporal/temporal_blob.cpp
    src/json/tjsonb.cpp
    src/json/tjsonb_in_out.cpp
    src/h3/th3index.cpp
    src/cbuffer/tcbuffer.cpp
    src/index/rtree_module.cpp
    src/single_tile_getters.cpp
    src/index/rtree_index_create_physical.cpp
    src/index/rtree_index_scan.cpp
    src/index/rtree_optimize_scan.cpp
    src/temporal/temporal_parquet.cpp
)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} "" ${EXTENSION_SOURCES})

# -----------------------------
# json-c target / include handling
# -----------------------------
if(TARGET json-c::json-c)
  set(JSONC_TGT json-c::json-c)
else()
  target_include_directories(${EXTENSION_NAME} SYSTEM PRIVATE ${JSON-C_INCLUDE_DIRS})
  target_include_directories(${LOADABLE_EXTENSION_NAME} SYSTEM PRIVATE ${JSON-C_INCLUDE_DIRS})
  set(JSONC_TGT ${JSON-C_LIBRARIES})
endif()

# H3 headers for meos_h3.h (see the find_path above) — attached per-target like
# json-c, since build_*_extension targets don't inherit directory includes.
target_include_directories(${EXTENSION_NAME} SYSTEM PRIVATE ${H3_INCLUDE_DIR})
target_include_directories(${LOADABLE_EXTENSION_NAME} SYSTEM PRIVATE ${H3_INCLUDE_DIR})

# -----------------------------
# Link libraries 
# -----------------------------
target_link_libraries(${EXTENSION_NAME}
  MEOS::meos
  ${GEOS_TGT}
  PROJ::proj
  GSL::gsl
  GSL::gslcblas
  ${JSONC_TGT}
  OpenSSL::SSL
  OpenSSL::Crypto
)

target_link_libraries(${LOADABLE_EXTENSION_NAME}
  MEOS::meos
  ${GEOS_TGT}
  PROJ::proj
  GSL::gsl
  GSL::gslcblas
  ${JSONC_TGT}
  OpenSSL::SSL
  OpenSSL::Crypto
)

# -----------------------------
# Install (static & loadable)
# -----------------------------
install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}"
)

install(
  TARGETS ${LOADABLE_EXTENSION_NAME}
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}"
  RUNTIME DESTINATION "${INSTALL_LIB_DIR}"
)
