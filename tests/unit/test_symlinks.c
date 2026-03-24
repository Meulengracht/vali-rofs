/**
 * Copyright, Philip Meulengracht
 *
 * Test suite for symlink security and path resolution
 * Tests for:
 * - Cyclic symlinks detection
 * - Deep symlink chain limits
 * - Malformed symlink targets
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/vafs.h>
#include <vafs/directory.h>
#include <vafs/file.h>

#define TEST_IMAGE_PATH "/tmp/test_symlinks.vafs"

static int g_test_passed = 0;
static int g_test_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s (line %d): %s\n", __func__, __LINE__, message); \
            g_test_failed++; \
            return -1; \
        } \
    } while (0)

#define TEST_PASS(message) \
    do { \
        fprintf(stdout, "PASS: %s: %s\n", __func__, message); \
        g_test_passed++; \
        return 0; \
    } while (0)

/**
 * Test 1: Create an image with a simple cyclic symlink (A -> B -> A)
 */
static int test_simple_cyclic_symlink(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    int status;

    vafs_config_initialize(&config);
    status = vafs_create(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory");

    // Create cyclic symlinks: link_a -> link_b, link_b -> link_a
    status = vafs_directory_create_symlink(root, "link_a", "/link_b");
    TEST_ASSERT(status == 0, "Failed to create link_a");

    status = vafs_directory_create_symlink(root, "link_b", "/link_a");
    TEST_ASSERT(status == 0, "Failed to create link_b");

    vafs_directory_close(root);
    vafs_close(vafs);

    // Now reopen and try to follow the cyclic symlinks
    status = vafs_open_file(TEST_IMAGE_PATH, &vafs);
    TEST_ASSERT(status == 0, "Failed to open test image");

    struct VaFsFileHandle* file = NULL;
    status = vafs_file_open(vafs, "/link_a", &file);
    TEST_ASSERT(status != 0 && errno == ELOOP, "Expected ELOOP for cyclic symlink");

    vafs_close(vafs);
    remove(TEST_IMAGE_PATH);

    TEST_PASS("Cyclic symlinks properly detected and rejected");
}

/**
 * Test 2: Create a deeply nested symlink chain that exceeds the limit
 */
static int test_deep_symlink_chain(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    char symlink_name[64];
    char symlink_target[64];
    int status;
    int i;

    vafs_config_initialize(&config);
    status = vafs_create(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory");

    // Create a file at the end of the chain
    struct VaFsFileHandle* file_handle = NULL;
    status = vafs_directory_create_file(root, "target_file", 0644, &file_handle);
    TEST_ASSERT(status == 0, "Failed to create target file");
    const char* content = "test";
    vafs_file_write(file_handle, (void*)content, strlen(content));
    vafs_file_close(file_handle);

    // Create a chain of 50 symlinks (exceeds VAFS_SYMLINK_MAX_DEPTH of 40)
    strcpy(symlink_target, "/target_file");
    for (i = 50; i >= 1; i--) {
        snprintf(symlink_name, sizeof(symlink_name), "link_%d", i);
        status = vafs_directory_create_symlink(root, symlink_name, symlink_target);
        TEST_ASSERT(status == 0, "Failed to create symlink in chain");
        snprintf(symlink_target, sizeof(symlink_target), "/link_%d", i);
    }

    vafs_directory_close(root);
    vafs_close(vafs);

    // Now reopen and try to follow the deep chain
    status = vafs_open_file(TEST_IMAGE_PATH, &vafs);
    TEST_ASSERT(status == 0, "Failed to open test image");

    struct VaFsFileHandle* file = NULL;
    status = vafs_file_open(vafs, "/link_1", &file);
    TEST_ASSERT(status != 0 && errno == ELOOP, "Expected ELOOP for deep symlink chain");

    vafs_close(vafs);
    remove(TEST_IMAGE_PATH);

    TEST_PASS("Deep symlink chains properly limited");
}

/**
 * Test 3: Test malformed symlink target (empty string)
 */
static int test_malformed_empty_target(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    int status;

    vafs_config_initialize(&config);
    status = vafs_create(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory");

    // Create a symlink with empty target
    status = vafs_directory_create_symlink(root, "empty_link", "");
    TEST_ASSERT(status == 0, "Failed to create symlink with empty target");

    vafs_directory_close(root);
    vafs_close(vafs);

    // Now reopen and try to follow the malformed symlink
    status = vafs_open_file(TEST_IMAGE_PATH, &vafs);
    TEST_ASSERT(status == 0, "Failed to open test image");

    struct VaFsFileHandle* file = NULL;
    status = vafs_file_open(vafs, "/empty_link", &file);
    TEST_ASSERT(status != 0 && errno == EINVAL, "Expected EINVAL for empty symlink target");

    vafs_close(vafs);
    remove(TEST_IMAGE_PATH);

    TEST_PASS("Empty symlink targets properly rejected");
}

/**
 * Test 4: Test extremely long symlink target (exceeds VAFS_PATH_MAX)
 */
static int test_malformed_long_target(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    char* long_path;
    int status;

    // Create a path that's too long (> 4096 bytes)
    long_path = malloc(5000);
    TEST_ASSERT(long_path != NULL, "Failed to allocate long path");
    memset(long_path, 'a', 4999);
    long_path[4999] = '\0';

    vafs_config_initialize(&config);
    status = vafs_create(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory");

    // Try to create a symlink with an extremely long target
    status = vafs_directory_create_symlink(root, "long_link", long_path);
    // The creation might succeed or fail depending on validation at creation time
    // but resolution should definitely fail

    vafs_directory_close(root);
    vafs_close(vafs);

    // Try to open and follow if it was created
    status = vafs_open_file(TEST_IMAGE_PATH, &vafs);
    if (status == 0) {
        struct VaFsFileHandle* file = NULL;
        status = vafs_file_open(vafs, "/long_link", &file);
        // Should fail with ENAMETOOLONG or EINVAL
        TEST_ASSERT(status != 0, "Expected failure for extremely long symlink target");
        vafs_close(vafs);
    }

    free(long_path);
    remove(TEST_IMAGE_PATH);

    TEST_PASS("Extremely long symlink targets properly handled");
}

/**
 * Test 5: Test indirect cyclic symlinks (A -> B -> C -> A)
 */
static int test_indirect_cyclic_symlink(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    int status;

    vafs_config_initialize(&config);
    status = vafs_create(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory");

    // Create indirect cyclic symlinks: A -> B -> C -> A
    status = vafs_directory_create_symlink(root, "link_a", "/link_b");
    TEST_ASSERT(status == 0, "Failed to create link_a");

    status = vafs_directory_create_symlink(root, "link_b", "/link_c");
    TEST_ASSERT(status == 0, "Failed to create link_b");

    status = vafs_directory_create_symlink(root, "link_c", "/link_a");
    TEST_ASSERT(status == 0, "Failed to create link_c");

    vafs_directory_close(root);
    vafs_close(vafs);

    // Now reopen and try to follow the cyclic symlinks
    status = vafs_open_file(TEST_IMAGE_PATH, &vafs);
    TEST_ASSERT(status == 0, "Failed to open test image");

    struct VaFsFileHandle* file = NULL;
    status = vafs_file_open(vafs, "/link_a", &file);
    TEST_ASSERT(status != 0 && errno == ELOOP, "Expected ELOOP for indirect cyclic symlink");

    vafs_close(vafs);
    remove(TEST_IMAGE_PATH);

    TEST_PASS("Indirect cyclic symlinks properly detected and rejected");
}

/**
 * Test 6: Test valid symlink chain within limits
 */
static int test_valid_symlink_chain(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    char symlink_name[64];
    char symlink_target[64];
    int status;
    int i;

    vafs_config_initialize(&config);
    status = vafs_create(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory");

    // Create a file at the end of the chain
    struct VaFsFileHandle* file_handle = NULL;
    status = vafs_directory_create_file(root, "target_file", 0644, &file_handle);
    TEST_ASSERT(status == 0, "Failed to create target file");
    const char* content = "test_content";
    status = vafs_file_write(file_handle, (void*)content, strlen(content));
    TEST_ASSERT(status == 0, "Failed to write to file");
    vafs_file_close(file_handle);

    // Create a chain of 10 symlinks (well within VAFS_SYMLINK_MAX_DEPTH of 40)
    strcpy(symlink_target, "/target_file");
    for (i = 10; i >= 1; i--) {
        snprintf(symlink_name, sizeof(symlink_name), "link_%d", i);
        status = vafs_directory_create_symlink(root, symlink_name, symlink_target);
        TEST_ASSERT(status == 0, "Failed to create symlink in chain");
        snprintf(symlink_target, sizeof(symlink_target), "/link_%d", i);
    }

    vafs_directory_close(root);
    vafs_close(vafs);

    // Now reopen and try to follow the valid chain
    status = vafs_open_file(TEST_IMAGE_PATH, &vafs);
    TEST_ASSERT(status == 0, "Failed to open test image");

    struct VaFsFileHandle* file = NULL;
    status = vafs_file_open(vafs, "/link_1", &file);
    TEST_ASSERT(status == 0, "Failed to follow valid symlink chain");

    if (file) {
        size_t length = vafs_file_length(file);
        TEST_ASSERT(length == strlen(content), "File length mismatch");

        char buffer[64] = {0};
        size_t read = vafs_file_read(file, buffer, sizeof(buffer));
        TEST_ASSERT(read == strlen(content), "Read size mismatch");
        TEST_ASSERT(strcmp(buffer, content) == 0, "File content mismatch");

        vafs_file_close(file);
    }

    vafs_close(vafs);
    remove(TEST_IMAGE_PATH);

    TEST_PASS("Valid symlink chains work correctly within depth limits");
}

int main(int argc, char** argv)
{
    printf("Running VaFS symlink security tests...\n\n");

    test_simple_cyclic_symlink();
    test_deep_symlink_chain();
    test_malformed_empty_target();
    test_malformed_long_target();
    test_indirect_cyclic_symlink();
    test_valid_symlink_chain();

    printf("\n========================================\n");
    printf("Test Results:\n");
    printf("  Passed: %d\n", g_test_passed);
    printf("  Failed: %d\n", g_test_failed);
    printf("========================================\n");

    return g_test_failed > 0 ? 1 : 0;
}
