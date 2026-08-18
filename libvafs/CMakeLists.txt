
# include subdirectories
add_subdirectory(cache)

# add primary library target
add_library(vafs STATIC
    core/builder.c
    core/crc.c
    core/log.c
    core/reader.c
    core/vafs.c
    fs/directory_builder.c
    fs/directory_cache.c
    fs/directory_descriptor.c
    fs/directory_reader.c
    fs/file.c
    fs/symlink.c
    fs/utils.c
    fs/xattr.c
    stream/core.c
    stream/device.c
)
target_include_directories(vafs PRIVATE include/vafs)
target_include_directories(vafs PUBLIC include)
target_link_libraries(vafs PUBLIC vafs-blockcache)
