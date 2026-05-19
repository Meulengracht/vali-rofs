cmake_minimum_required(VERSION 3.14.3)

# Add test executable for malformed descriptors
add_executable(test-malformed test_malformed.c)
target_link_libraries(test-malformed vafs)
target_include_directories(test-malformed PRIVATE
    ${CMAKE_SOURCE_DIR}/libvafs/include
    ${CMAKE_SOURCE_DIR}/libvafs
)
add_test(NAME test-malformed COMMAND test-malformed ${CMAKE_CURRENT_BINARY_DIR})

# Add test executable for symlink security
add_executable(test-symlinks test_symlinks.c)
target_link_libraries(test-symlinks vafs)
target_include_directories(test-symlinks PRIVATE
    ${CMAKE_SOURCE_DIR}/libvafs/include
    ${CMAKE_SOURCE_DIR}/libvafs
)
add_test(NAME test-symlinks COMMAND test-symlinks)

# Add test executable for wide directory lookup behavior
add_executable(test-directory-lookups test_directory_lookups.c)
target_link_libraries(test-directory-lookups vafs)
target_include_directories(test-directory-lookups PRIVATE
    ${CMAKE_SOURCE_DIR}/libvafs/include
    ${CMAKE_SOURCE_DIR}/libvafs/include/vafs
    ${CMAKE_SOURCE_DIR}/libvafs
)
add_test(NAME test-directory-lookups COMMAND test-directory-lookups)

# Add test executable for header validation
add_executable(test-header-validation test_header_validation.c)
target_link_libraries(test-header-validation vafs)
target_include_directories(test-header-validation PRIVATE
    ${CMAKE_SOURCE_DIR}/libvafs/include
    ${CMAKE_SOURCE_DIR}/libvafs
)
add_test(NAME test-header-validation COMMAND test-header-validation ${CMAKE_CURRENT_BINARY_DIR})

# Add comprehensive regression test suite
add_executable(test-regression test_regression.c)
target_link_libraries(test-regression vafs)
target_include_directories(test-regression PRIVATE
    ${CMAKE_SOURCE_DIR}/libvafs/include
    ${CMAKE_SOURCE_DIR}/libvafs
)
add_test(NAME test-regression COMMAND test-regression ${CMAKE_CURRENT_BINARY_DIR})

# Add focused regression test for file read position semantics
add_executable(test-file-read test_file_read.c)
target_link_libraries(test-file-read vafs)
target_include_directories(test-file-read PRIVATE
    ${CMAKE_SOURCE_DIR}/libvafs/include
    ${CMAKE_SOURCE_DIR}/libvafs
)
add_test(NAME test-file-read COMMAND test-file-read)
