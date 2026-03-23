cmake_minimum_required(VERSION 3.14.3)

# Build benchmark library
add_library(vafs-benchmark STATIC
    benchmark.c
)

target_include_directories(vafs-benchmark PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Build benchmark executable
add_executable(vafs-bench
    vafs_bench.c
    filter.c
)

target_compile_definitions(vafs-bench PRIVATE __VAFS_FILTER_BRIEFLZ)

target_link_libraries(vafs-bench
    vafs-benchmark
    vafs
    vafs-blockcache
    brieflz
)

target_include_directories(vafs-bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../libvafs/include
)

# Install benchmark executable
install(TARGETS vafs-bench
    RUNTIME DESTINATION bin
)
