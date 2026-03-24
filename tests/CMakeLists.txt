cmake_minimum_required(VERSION 3.14.3)

# Add test executable for malformed descriptors
add_executable(test-malformed test_malformed.c)
target_link_libraries(test-malformed vafs)
target_include_directories(test-malformed PRIVATE
    ${CMAKE_SOURCE_DIR}/libvafs/include
    ${CMAKE_SOURCE_DIR}/libvafs
)

# Add test executable for symlink security
add_executable(test-symlinks test_symlinks.c)
target_link_libraries(test-symlinks vafs)
target_include_directories(test-symlinks PRIVATE
    ${CMAKE_SOURCE_DIR}/libvafs/include
    ${CMAKE_SOURCE_DIR}/libvafs
)
