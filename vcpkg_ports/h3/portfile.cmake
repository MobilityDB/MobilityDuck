vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO uber/h3
    REF v${VERSION}
    SHA512 6ed93c5e69adbba9804282b5814f1617d4c930b677df4735e4d4cf10fcba813f61b6be3a125d191d375e52e3e22af7c244efb007f27ca487b34eae9e24fb6c7b
    HEAD_REF master
)

vcpkg_cmake_configure(
    SOURCE_PATH ${SOURCE_PATH}
    OPTIONS
        -DBUILD_BENCHMARKS=OFF
        -DBUILD_FUZZERS=OFF
        -DBUILD_FILTERS=OFF
        -DBUILD_GENERATORS=OFF
        -DBUILD_TESTING=OFF
        # Disable the in-tree developer tooling that H3 turns on when it finds a
        # clang-tidy/clang-format binary plus its `.clang-tidy` file. Under the
        # wasm32-emscripten toolchain the runner's clang-tidy runs on H3's own
        # sources with `-warnings-as-errors`, so vendored H3 lint findings
        # (readability-braces-around-statements, non-const globals) fail the build.
        # Newer upstream vcpkg h3 ports carry these same OFF flags; MobilityDB does
        # not lint upstream vendored dependencies either.
        -DENABLE_DOCS=OFF
        -DENABLE_FORMAT=OFF
        -DENABLE_LINTING=OFF
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/${PORT})

vcpkg_copy_pdbs()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/debug/bin" "${CURRENT_PACKAGES_DIR}/debug/include")

file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
