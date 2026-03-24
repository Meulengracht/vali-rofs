cmake_minimum_required(VERSION 3.14.3)

# Add unit tests subdirectory
add_subdirectory(unit)

# Option to enable fuzzing
option(VAFS_BUILD_FUZZ "Build fuzzing harnesses" OFF)

if (VAFS_BUILD_FUZZ)
    add_subdirectory(fuzz)
endif()
