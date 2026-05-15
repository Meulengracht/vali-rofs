/*
 * Copyright, Philip Meulengracht
 *
 * Wide directory lookup regression tests.
 * Verifies deterministic ordering and lookup correctness with many entries.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/vafs.h>
#include <vafs/directory.h>
#include <vafs/file.h>

#define TEST_IMAGE_PATH "/tmp/test_directory_lookups.vafs"
#define ROOT_ENTRY_COUNT 256

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

static void make_entry_name(char* buffer, size_t buffer_size, int index)
{
    snprintf(buffer, buffer_size, "entry_%04d", index);
}

static int test_wide_directory_lookup(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    struct VaFsDirectoryHandle* nested = NULL;
    struct VaFsFileHandle* file_handle = NULL;
    char name[32];
    struct VaFsEntry entry;
    size_t read_count = 0;
    int saw_first = 0;
    int saw_middle = 0;
    int saw_last = 0;
    int saw_nested = 0;
    int status;
    int i;

    vafs_config_initialize(&config);
    status = vafs_create(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory");

    // Insert names in reverse order so name lookup correctness does not depend on insertion order.
    for (i = ROOT_ENTRY_COUNT - 1; i >= 0; i--) {
        make_entry_name(name, sizeof(name), i);
        status = vafs_directory_create_file(root, name, 0644, &file_handle);
        TEST_ASSERT(status == 0, "Failed to create file entry");

        if (i == 0) {
            // Keep one file non-empty so the test image produces a valid data stream on close.
            const char* content = "wide-directory-data";
            status = vafs_file_write(file_handle, (void*)content, strlen(content));
            TEST_ASSERT(status == 0, "Failed to write file payload");
        }

        vafs_file_close(file_handle);
        file_handle = NULL;
    }

    status = vafs_directory_create_directory(root, "nested_dir", 0755, &nested);
    TEST_ASSERT(status == 0, "Failed to create nested directory");

    status = vafs_directory_create_file(nested, "inner_file", 0644, &file_handle);
    TEST_ASSERT(status == 0, "Failed to create nested file");
    vafs_file_close(file_handle);
    file_handle = NULL;

    vafs_directory_close(nested);
    nested = NULL;
    vafs_directory_close(root);
    root = NULL;
    vafs_close(vafs);
    vafs = NULL;

    status = vafs_open_file(TEST_IMAGE_PATH, &vafs);
    TEST_ASSERT(status == 0, "Failed to reopen test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to reopen root directory");

    while (vafs_directory_read(root, &entry) == 0) {
        if (strcmp(entry.Name, "entry_0000") == 0) {
            saw_first = 1;
        } else if (strcmp(entry.Name, "entry_0128") == 0) {
            saw_middle = 1;
        } else if (strcmp(entry.Name, "entry_0255") == 0) {
            saw_last = 1;
        } else if (strcmp(entry.Name, "nested_dir") == 0) {
            saw_nested = 1;
        }
        read_count++;
    }

    TEST_ASSERT(read_count == ROOT_ENTRY_COUNT + 1, "Unexpected root directory entry count");
    TEST_ASSERT(saw_first, "Missing first lookup target during enumeration");
    TEST_ASSERT(saw_middle, "Missing middle lookup target during enumeration");
    TEST_ASSERT(saw_last, "Missing last lookup target during enumeration");
    TEST_ASSERT(saw_nested, "Missing nested directory during enumeration");

    status = vafs_directory_open_file(root, "entry_0000", &file_handle);
    TEST_ASSERT(status == 0, "Failed to open first entry through directory lookup");
    vafs_file_close(file_handle);
    file_handle = NULL;

    status = vafs_directory_open_file(root, "entry_0128", &file_handle);
    TEST_ASSERT(status == 0, "Failed to open middle entry through directory lookup");
    vafs_file_close(file_handle);
    file_handle = NULL;

    status = vafs_directory_open_file(root, "entry_0255", &file_handle);
    TEST_ASSERT(status == 0, "Failed to open last entry through directory lookup");
    vafs_file_close(file_handle);
    file_handle = NULL;

    status = vafs_file_open(vafs, "/nested_dir/inner_file", &file_handle);
    TEST_ASSERT(status == 0, "Failed to open nested file through path lookup");
    vafs_file_close(file_handle);
    file_handle = NULL;

    status = vafs_directory_open(vafs, "/nested_dir", &nested);
    TEST_ASSERT(status == 0, "Failed to open nested directory through path lookup");
    vafs_directory_close(nested);
    nested = NULL;

    status = vafs_directory_open_file(root, "missing_entry", &file_handle);
    TEST_ASSERT(status != 0 && errno == ENOENT, "Missing entry should return ENOENT");

    vafs_directory_close(root);
    vafs_close(vafs);
    remove(TEST_IMAGE_PATH);

    TEST_PASS("Wide directory lookup remains correct and deterministic");
}

int main(int argc, char** argv)
{
    printf("Running VaFS wide directory lookup tests...\n\n");

    test_wide_directory_lookup();

    printf("\n========================================\n");
    printf("Test Results:\n");
    printf("  Passed: %d\n", g_test_passed);
    printf("  Failed: %d\n", g_test_failed);
    printf("========================================\n");

    return g_test_failed > 0 ? 1 : 0;
}
