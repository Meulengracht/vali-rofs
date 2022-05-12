
# include subdirectories
add_subdirectory(cache)

# add primary library target
add_library(vafs STATIC
    config.c
    crc.c
    directory.c
    file.c
    log.c
    stream.c
    streamdevice.c
    symlink.c
    utils.c
    vafs.c
)
target_include_directories(vafs PRIVATE include/vafs)
target_include_directories(vafs PUBLIC include)
target_link_libraries(vafs PUBLIC vafs-blockcache)
