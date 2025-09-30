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

set(EXTENSION_SOURCES
    src/mobilityduck_extension.cpp
    src/temporal/temporal.cpp
    src/temporal/temporal_functions.cpp
    src/temporal/tbox.cpp
    src/temporal/tbox_functions.cpp
    src/geo/stbox.cpp
    src/geo/stbox_functions.cpp
    src/geo/tgeompoint.cpp
    src/geo/tgeompoint_functions.cpp
    src/temporal/set.cpp
    src/temporal/span.cpp
    src/geo/geoset.cpp
    src/temporal/spanset.cpp
    src/geo/tgeometry.cpp
    src/geo/tgeometry_in_out.cpp
    src/index/rtree_module.cpp
    src/index/rtree_index_create_physical.cpp
    src/index/rtree_index_scan.cpp
    src/index/rtree_optimize_scan.cpp
)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} "" ${EXTENSION_SOURCES})

# ----- SRID CSV defaults (works with/without vcpkg) -----
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
  target_compile_definitions(${EXTENSION_NAME} PRIVATE
    MDUCK_DEFAULT_SRID_CSV="${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/spatial_ref_sys.csv"
    MDUCK_SRID_ENV_NAME="SPATIAL_REF_SYS"
  )
  target_compile_definitions(${LOADABLE_EXTENSION_NAME} PRIVATE
    MDUCK_DEFAULT_SRID_CSV="${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/spatial_ref_sys.csv"
    MDUCK_SRID_ENV_NAME="SPATIAL_REF_SYS"
  )
else()  
  target_compile_definitions(${EXTENSION_NAME} PRIVATE
    MDUCK_DEFAULT_SRID_CSV="${CMAKE_SOURCE_DIR}/data/spatial_ref_sys.csv"
    MDUCK_SRID_ENV_NAME="SPATIAL_REF_SYS"
  )
  target_compile_definitions(${LOADABLE_EXTENSION_NAME} PRIVATE
    MDUCK_DEFAULT_SRID_CSV="${CMAKE_SOURCE_DIR}/data/spatial_ref_sys.csv"
    MDUCK_SRID_ENV_NAME="SPATIAL_REF_SYS"
  )
endif()


# -----------------------------
# json-c target / include handling
# -----------------------------
if (TARGET json-c::json-c)
  set(JSONC_TGT json-c::json-c)
else()
  target_include_directories(${EXTENSION_NAME} PRIVATE ${JSON-C_INCLUDE_DIRS})
  target_include_directories(${LOADABLE_EXTENSION_NAME} PRIVATE ${JSON-C_INCLUDE_DIRS})
  set(JSONC_TGT ${JSON-C_LIBRARIES})
endif()

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

if (WIN32)  
  # target_link_libraries(${EXTENSION_NAME} ws2_32 crypt32)
  # target_link_libraries(${LOADABLE_EXTENSION_NAME} ws2_32 crypt32)
endif()

# -----------------------------
# Install (static extension)
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