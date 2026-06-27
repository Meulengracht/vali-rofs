
# include subdirectories
add_subdirectory(cache)

# add primary library target
add_library(vafs STATIC
    builder.c
    config.c
    crc.c
    directory_descriptor.c
    directory_cache.c
    directory.c
    file.c
    log.c
    reader.c
    stream.c
    streamdevice.c
    symlink.c
    utils.c
    vafs.c
    xattr.c
)
target_include_directories(vafs PRIVATE include/vafs)
target_include_directories(vafs PUBLIC include)
target_link_libraries(vafs PUBLIC vafs-blockcache)
