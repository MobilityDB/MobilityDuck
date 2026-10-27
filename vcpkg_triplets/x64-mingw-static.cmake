# vcpkg's community x64-mingw-static triplet, with one addition for the gdal port.
#
# The MinGW toolchain of the Windows runner (Rtools) carries packages of its own
# under its sysroot, among them an HDF5 whose CMake configuration names an
# h5diff.exe it does not ship. GDAL's configure looks for HDF5 even when it is
# built without it, finds that configuration and stops. GDAL is built here
# without HDF5 and without Arrow, so it does not look for either, as DuckDB's
# spatial extension configures its GDAL.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_ENV_PASSTHROUGH PATH)

set(VCPKG_CMAKE_SYSTEM_NAME MinGW)

if(PORT STREQUAL "gdal")
    list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS
        -DCMAKE_DISABLE_FIND_PACKAGE_HDF5=ON
        -DCMAKE_DISABLE_FIND_PACKAGE_Arrow=ON)
endif()
