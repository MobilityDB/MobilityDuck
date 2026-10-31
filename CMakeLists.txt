#
# This MobilityDB code is provided under The PostgreSQL License.
# Copyright (c) 2016-2026, Université libre de Bruxelles and MobilityDB
# contributors
#
# MobilityDB includes portions of PostGIS version 3 source code released
# under the GNU General Public License (GPLv2 or later).
# Copyright (c) 2001-2026, PostGIS contributors
#
# Permission to use, copy, modify, and distribute this software and its
# documentation for any purpose, without fee, and without a written
# agreement is hereby granted, provided that the above copyright notice and
# this paragraph and the following two paragraphs appear in all copies.
#
# IN NO EVENT SHALL UNIVERSITE LIBRE DE BRUXELLES BE LIABLE TO ANY PARTY FOR
# DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
# LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
# EVEN IF UNIVERSITE LIBRE DE BRUXELLES HAS BEEN ADVISED OF THE POSSIBILITY
# OF SUCH DAMAGE.
#
# UNIVERSITE LIBRE DE BRUXELLES SPECIFICALLY DISCLAIMS ANY WARRANTIES,
# INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
# AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON
# AN "AS IS" BASIS, AND UNIVERSITE LIBRE DE BRUXELLES HAS NO OBLIGATIONS TO
# PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
#

cmake_minimum_required(VERSION 3.5)

# -----------------------------
# Project
# -----------------------------
set(TARGET_NAME mobilityduck)
project(${TARGET_NAME} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
find_package(PROJ CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(json-c CONFIG REQUIRED)

# MEOS from overlay port
find_package(MEOS CONFIG REQUIRED)

# The commit libmeos is built from, read from the port that builds it, so the
# version the extension reports is the one it links. MEOS exposes no runtime
# version symbol, so this build-time pin is the most precise stamp available —
# and the refresh workflow rewrites `_MEOS_REF` alone, so a second copy of the
# SHA kept by hand states the wrong commit from the first advance onward.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_ports/meos/portfile.cmake" _meos_portfile)
string(REGEX MATCH "set\\(_MEOS_REF \"([0-9a-f]+)\"" _meos_ref_match "${_meos_portfile}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "vcpkg_ports/meos/portfile.cmake: no _MEOS_REF to report as the linked MEOS commit.")
endif()
string(SUBSTRING "${CMAKE_MATCH_1}" 0 10 MOBILITYDUCK_MEOS_PIN)
message(STATUS "MobilityDuck reports linked MEOS ${MOBILITYDUCK_MEOS_PIN}")

# The linked MEOS commit and the extension version, the `git describe` of this
# tree that DuckDB defines as EXT_VERSION_MOBILITYDUCK on every object of this
# directory, are read by src/mobilityduck_extension.cpp alone, and each is
# defined on that file alone. A definition on every object puts a value that
# changes with each commit on every compile command line, so the compiler
# cache finds none of them by its direct lookup and preprocesses every one.
set_property(SOURCE src/mobilityduck_extension.cpp APPEND PROPERTY COMPILE_DEFINITIONS
  MOBILITYDUCK_MEOS_PIN="${MOBILITYDUCK_MEOS_PIN}")
if(NOT "${DUCKDB_EXTENSION_MOBILITYDUCK_EXT_VERSION}" STREQUAL "")
  remove_definitions(-DEXT_VERSION_MOBILITYDUCK="${DUCKDB_EXTENSION_MOBILITYDUCK_EXT_VERSION}")
  set_property(SOURCE src/mobilityduck_extension.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    EXT_VERSION_MOBILITYDUCK="${DUCKDB_EXTENSION_MOBILITYDUCK_EXT_VERSION}")
endif()

# H3 headers: the all-families libmeos exposes meos_h3.h, which includes
# <h3api.h>. SIBLING-FROM MobilityDB CMakeLists.txt H3 block (find_path
# H3_INCLUDE_DIR NAMES h3api.h PATH_SUFFIXES h3) so code compiling against
# meos_h3.h sees the system libh3 headers, just as the meos port (-DH3=ON) does.
# The dir is attached to the extension targets below because
# build_*_extension targets don't inherit directory includes.
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

# -----------------------------
# DuckDB extension targets
# -----------------------------
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

include_directories(src/include)

set(EXTENSION_SOURCES
    src/spatial_ref_sys_csv.inc
    src/ways_csv.inc
    src/mobilityduck_extension.cpp
    # The UDF surface is emitted as several translation units so that it compiles in
    # parallel; tools/codegen_duck_udfs.py writes exactly GENERATED_CHUNKS of them.
    src/generated/generated_temporal_udfs.cpp
    src/generated/generated_temporal_udfs_0.cpp
    src/generated/generated_temporal_udfs_1.cpp
    src/generated/generated_temporal_udfs_2.cpp
    src/generated/generated_temporal_udfs_3.cpp
    src/generated/generated_temporal_udfs_4.cpp
    src/generated/generated_temporal_udfs_5.cpp
    src/generated/generated_temporal_udfs_6.cpp
    src/generated/generated_temporal_udfs_7.cpp
    src/generated/generated_temporal_udfs_8.cpp
    src/generated/generated_temporal_udfs_9.cpp
    src/generated/generated_temporal_udfs_10.cpp
    src/generated/generated_temporal_udfs_11.cpp
    src/generated/generated_temporal_udfs_12.cpp
    src/generated/generated_temporal_udfs_13.cpp
    src/generated/generated_temporal_udfs_14.cpp
    src/generated/generated_temporal_udfs_15.cpp
    src/generated/generated_type_registration.cpp
    src/generated/generated_temporal_covering.cpp
    src/generated/generated_aggregates.cpp
    src/temporal/temporal.cpp
    src/temporal/temporal_functions.cpp
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
    src/geo/geoset.cpp
    src/temporal/spanset.cpp
    src/temporal/spanset_functions.cpp
    src/geo/tgeometry.cpp
    src/geo/tgeometry_in_out.cpp
    src/geo/tgeometry_ops.cpp
    src/geo/tgeography.cpp
    src/geo/tgeography_in_out.cpp
    src/geo/tgeography_ops.cpp
    src/pointcloud/pcschema.cpp
    src/pointcloud/tpcbox.cpp
    src/pointcloud/tpcpoint.cpp
    src/pointcloud/tpcpoint_in_out.cpp
    src/pointcloud/tpcpatch.cpp
    src/pointcloud/tpcpatch_in_out.cpp
    src/geo/tgeogpoint.cpp
    src/geo/tgeogpoint_ops.cpp
    src/quadbin/tquadbin.cpp
    src/s2cell/ts2cell.cpp
    src/raster/raquet.cpp
    src/raster/raster.cpp
    src/raster/raster_boundary.cpp
    src/raster/raster_datacube.cpp
    src/temporal/temporal_blob.cpp
    src/json/tjsonb.cpp
    src/json/tjsonb_in_out.cpp
    src/h3/th3index.cpp
    src/cbuffer/tcbuffer.cpp
    src/npoint/tnpoint.cpp
    src/pose/tpose.cpp
    src/rgeo/trgeometry.cpp
    src/posechain/tposechain.cpp
    src/index/rtree_module.cpp
    src/index/rtree_index_create_physical.cpp
    src/index/rtree_index_scan.cpp
    src/index/rtree_optimize_scan.cpp
    src/index/rtree_index_join.cpp
    src/index/sptree_index_join.cpp
    src/index/sptree_module.cpp
    src/index/sptree_index_create_physical.cpp
    src/index/sptree_index_scan.cpp
    src/index/sptree_optimize_scan.cpp
    src/temporal/temporal_parquet.cpp
)

# The datacube codec of the raster boundary (src/raster/raster_datacube.cpp)
# decompresses with the codecs DuckDB vendors in third_party. zstd, miniz and
# yyjson are libraries of duckdb_static, their headers on DuckDB's include path;
# snappy and lz4 are compiled into the extension that calls them, as the parquet
# extension compiles them (duckdb/extension/parquet/CMakeLists.txt).
set(MOBILITYDUCK_DUCKDB_THIRD_PARTY ${CMAKE_SOURCE_DIR}/third_party)
list(APPEND EXTENSION_SOURCES
    ${MOBILITYDUCK_DUCKDB_THIRD_PARTY}/snappy/snappy.cc
    ${MOBILITYDUCK_DUCKDB_THIRD_PARTY}/snappy/snappy-sinksource.cc
    ${MOBILITYDUCK_DUCKDB_THIRD_PARTY}/lz4/lz4.cpp)
include_directories(SYSTEM ${MOBILITYDUCK_DUCKDB_THIRD_PARTY}/snappy ${MOBILITYDUCK_DUCKDB_THIRD_PARTY}/lz4)

# Outside Windows and Emscripten every source but the entry file compiles to the
# same object for the static and the loadable extension: both targets build
# position-independent code (DuckDB sets CMAKE_POSITION_INDEPENDENT_CODE), and the
# loadable-only DUCKDB_BUILD_LOADABLE_EXTENSION reaches a declaration only in
# winapi.hpp and duckdb.h under _WIN32. So those sources compile once, into an
# object library both targets link, and only src/mobilityduck_extension.cpp, which
# reads that define, compiles per target. On Windows the define switches the
# linkage DuckDB's headers declare, so there each target compiles its own objects.
set(MOBILITYDUCK_ENTRY_SOURCE src/mobilityduck_extension.cpp)
if(WIN32 OR EMSCRIPTEN)
  set(MOBILITYDUCK_SHARED_OBJECTS OFF)
else()
  set(MOBILITYDUCK_SHARED_OBJECTS ON)
endif()

if(MOBILITYDUCK_SHARED_OBJECTS)
  set(MOBILITYDUCK_OBJECT_SOURCES ${EXTENSION_SOURCES})
  list(REMOVE_ITEM MOBILITYDUCK_OBJECT_SOURCES ${MOBILITYDUCK_ENTRY_SOURCE})
  add_library(${TARGET_NAME}_objects OBJECT ${MOBILITYDUCK_OBJECT_SOURCES})
  # The usage requirements of duckdb_static, which build_static_extension links.
  target_link_libraries(${TARGET_NAME}_objects PRIVATE duckdb_static)
  build_static_extension(${TARGET_NAME} ${MOBILITYDUCK_ENTRY_SOURCE}
    $<TARGET_OBJECTS:${TARGET_NAME}_objects>)
  build_loadable_extension(${TARGET_NAME} "" ${MOBILITYDUCK_ENTRY_SOURCE}
    $<TARGET_OBJECTS:${TARGET_NAME}_objects>)
  # The shared objects take the visibility the loadable extension sets for its
  # own, so it keeps exporting its entry point alone; the static archive links
  # into executables, where visibility does not change what they call.
  get_target_property(_md_visibility ${LOADABLE_EXTENSION_NAME} CXX_VISIBILITY_PRESET)
  if(_md_visibility)
    set_target_properties(${TARGET_NAME}_objects PROPERTIES
      CXX_VISIBILITY_PRESET ${_md_visibility})
  endif()
  set(MOBILITYDUCK_COMPILE_TARGETS ${EXTENSION_NAME} ${LOADABLE_EXTENSION_NAME}
    ${TARGET_NAME}_objects)
else()
  build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
  build_loadable_extension(${TARGET_NAME} "" ${EXTENSION_SOURCES})
  set(MOBILITYDUCK_COMPILE_TARGETS ${EXTENSION_NAME} ${LOADABLE_EXTENSION_NAME})
endif()

# H3 headers for meos_h3.h (see the find_path above) — attached per-target,
# since build_*_extension targets don't inherit directory includes.
foreach(_md_target ${MOBILITYDUCK_COMPILE_TARGETS})
  target_include_directories(${_md_target} SYSTEM PRIVATE ${H3_INCLUDE_DIR})
endforeach()

# -----------------------------
# MEOS's own external dependencies
# -----------------------------
# libmeos is a static archive (the meos port follows the triplet's static
# linkage), so it carries only MEOS's objects: every library MEOS itself calls
# into has to be resolved on this link line instead of inside a shared libmeos.
# PROJ and json-c already appear in the link list below; these four
# are the ones the list was missing, one per optional family the meos port
# enables with -DALL=ON — H3 for th3index, GDAL for the raster operators, and
# libxml2 plus zlib for the pgPointCloud core.
find_package(h3 CONFIG REQUIRED)
find_package(GDAL CONFIG REQUIRED)
find_package(LibXml2 REQUIRED)
find_package(ZLIB REQUIRED)

# -----------------------------
# Link libraries
# -----------------------------
set(MOBILITYDUCK_LINK_LIBS
  MEOS::meos
  PROJ::proj
  json-c::json-c
  OpenSSL::SSL
  OpenSSL::Crypto
  h3::h3
  GDAL::GDAL
  LibXml2::LibXml2
  ZLIB::ZLIB
)

# src/include/duckdb_version_compat.hpp selects the DuckDB entry points of the line
# being built and refuses a line older than v1.5, where GEOMETRY is not yet a core
# type. DuckDB's own build sets these two variables.
foreach(_md_target ${MOBILITYDUCK_COMPILE_TARGETS})
  target_compile_definitions(${_md_target} PRIVATE
    MOBILITYDUCK_DUCKDB_MAJOR=${DUCKDB_MAJOR_VERSION}
    MOBILITYDUCK_DUCKDB_MINOR=${DUCKDB_MINOR_VERSION})
endforeach()

target_link_libraries(${EXTENSION_NAME} ${MOBILITYDUCK_LINK_LIBS})
target_link_libraries(${LOADABLE_EXTENSION_NAME} ${MOBILITYDUCK_LINK_LIBS})
if(MOBILITYDUCK_SHARED_OBJECTS)
  # The include directories and definitions these libraries carry.
  target_link_libraries(${TARGET_NAME}_objects PRIVATE ${MOBILITYDUCK_LINK_LIBS})
endif()

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
