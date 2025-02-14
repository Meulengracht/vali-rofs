# Contains all the filter targets in which
# we can import the filters from.

option(VAFS_BUILD_FILTER_APLIB "Build support for the aplib filter" OFF)
option(VAFS_BUILD_FILTER_BRIEFLZ "Build support for the brieflz filter" ON)

if(VAFS_BUILD_FILTER_APLIB)
    add_subdirectory(aplib)
endif(VAFS_BUILD_FILTER_APLIB)

if(VAFS_BUILD_FILTER_BRIEFLZ)
    add_subdirectory(brieflz)
endif()
