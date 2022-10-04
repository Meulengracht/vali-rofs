# build the mkvafs tool that is a generic utility for building
# vafs images

# find FUSE module aswell to support mounting
if (NOT WIN32 AND NOT MOLLENOS)
    find_package(FUSE REQUIRED)
endif()

# build a list of additional libraries to link against
set(ADDITIONAL_LIBS vafs)
set(ADDITIONAL_DEFINES)

# add platform libraries
if (UNIX)
    set(ADDITIONAL_LIBS ${ADDITIONAL_LIBS} -lpthread)
endif (UNIX)

# add filter library targets and includes
if (VAFS_BUILD_FILTER_APLIB)
    set(ADDITIONAL_LIBS ${ADDITIONAL_LIBS} libap)
    set(ADDITIONAL_DEFINES __VAFS_FILTER_APLIB)
endif (VAFS_BUILD_FILTER_APLIB)

# build the vafs-util tool, which is the fuse filesystem
if (NOT WIN32 AND NOT MOLLENOS)
    add_executable(vafs-util vafs.c filter.c)
    target_link_libraries(vafs-util ${FUSE_LIBRARIES} ${ADDITIONAL_LIBS})
endif ()

add_executable(mkvafs
    mkvafs.c
    filter.c
)
target_compile_definitions(mkvafs PRIVATE ${ADDITIONAL_DEFINES})
target_link_libraries(mkvafs ${ADDITIONAL_LIBS})

# build the unmkvafs tool that is a generic utility for unpacking
# vafs images
add_executable(unmkvafs
    unmkvafs.c
    filter.c
)
target_compile_definitions(unmkvafs PRIVATE ${ADDITIONAL_DEFINES})
target_link_libraries(unmkvafs ${ADDITIONAL_LIBS})

# add support for installing
install(
    TARGETS mkvafs unmkvafs 
    RUNTIME DESTINATION bin
)
