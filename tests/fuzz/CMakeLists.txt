cmake_minimum_required(VERSION 3.14.3)

# Check if fuzzing is enabled
if (NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(WARNING "Fuzzing requires Clang compiler. Skipping fuzzing targets.")
    return()
endif()

# Common fuzzing flags
set(FUZZ_FLAGS "-fsanitize=fuzzer,address,undefined" CACHE STRING "Fuzzing compiler flags")
set(FUZZ_LINK_FLAGS "-fsanitize=fuzzer,address,undefined" CACHE STRING "Fuzzing linker flags")

# Function to add a fuzzing target
function(add_fuzz_target target_name source_file)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} vafs)
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/libvafs/include
        ${CMAKE_SOURCE_DIR}/libvafs
    )
    target_compile_options(${target_name} PRIVATE ${FUZZ_FLAGS})
    target_link_options(${target_name} PRIVATE ${FUZZ_LINK_FLAGS})
endfunction()

# Add fuzzing targets
add_fuzz_target(fuzz_image_open fuzz_image_open.c)
add_fuzz_target(fuzz_directory_traversal fuzz_directory_traversal.c)
add_fuzz_target(fuzz_file_operations fuzz_file_operations.c)
add_fuzz_target(fuzz_symlink_resolution fuzz_symlink_resolution.c)

# Install fuzzing targets
install(TARGETS
    fuzz_image_open
    fuzz_directory_traversal
    fuzz_file_operations
    fuzz_symlink_resolution
    DESTINATION bin/fuzz
)
