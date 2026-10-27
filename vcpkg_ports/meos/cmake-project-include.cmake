# Included by MobilityDB's project() call (CMAKE_PROJECT_INCLUDE), for every
# source of libmeos.
#
# DuckDB's PostgreSQL parser (third_party/libpg_query) defines a global
# session_timezone, and so does MEOS (pgtypes/timezone/pgtz.c). Both land in one
# extension binary, so MEOS's is renamed. The rename is a compile definition
# rather than a CMAKE_C_FLAGS value, which would replace the flags vcpkg's
# toolchain builds from the triplet.
add_compile_definitions(session_timezone=meos_session_timezone)
