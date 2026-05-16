/**
 * Regression tests for file read semantics.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/vafs.h>
#include <vafs/directory.h>
#include <vafs/file.h>

#define TEST_IMAGE_PATH "test_file_read.vafs"
#define TEST_BLOCK_SIZE 8192u
#define TEST_TAIL_SIZE  128u

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

static void fill_pattern(char* buffer, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        buffer[i] = (char)('A' + (i % 26));
    }
}

static void cleanup_image(struct VaFs* vafs, struct VaFsDirectoryHandle* root, struct VaFsFileHandle* file)
{
    if (file != NULL) {
        vafs_file_close(file);
    }
    if (root != NULL) {
        vafs_directory_close(root);
    }
    if (vafs != NULL) {
        vafs_close(vafs);
    }
    remove(TEST_IMAGE_PATH);
}

static int test_sequential_reads_advance_position(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    struct VaFsFileHandle* file = NULL;
    char* expected = NULL;
    char* firstChunk = NULL;
    char secondChunk[TEST_TAIL_SIZE] = { 0 };
    size_t expectedLength = TEST_BLOCK_SIZE + TEST_TAIL_SIZE;
    size_t read;
    int status;

    expected = malloc(expectedLength);
    firstChunk = malloc(TEST_BLOCK_SIZE);
    TEST_ASSERT(expected != NULL && firstChunk != NULL, "Failed to allocate test buffers");

    fill_pattern(expected, expectedLength);

    vafs_config_initialize(&config);
    vafs_config_set_block_size(&config, TEST_BLOCK_SIZE);

    status = vafs_create(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory");

    status = vafs_directory_create_file(root, "payload", 0644, &file);
    TEST_ASSERT(status == 0, "Failed to create test file");

    read = vafs_file_write(file, expected, expectedLength);
    TEST_ASSERT(read == 0, "Failed to write test payload");

    vafs_file_close(file);
    file = NULL;
    vafs_directory_close(root);
    root = NULL;
    vafs_close(vafs);
    vafs = NULL;

    status = vafs_open_file(TEST_IMAGE_PATH, &vafs);
    TEST_ASSERT(status == 0, "Failed to reopen test image");

    status = vafs_file_open(vafs, "/payload", &file);
    TEST_ASSERT(status == 0, "Failed to open payload file");

    read = vafs_file_read(file, firstChunk, TEST_BLOCK_SIZE);
    TEST_ASSERT(read == TEST_BLOCK_SIZE, "First read size mismatch");
    TEST_ASSERT(memcmp(firstChunk, expected, TEST_BLOCK_SIZE) == 0, "First read content mismatch");

    read = vafs_file_read(file, secondChunk, sizeof(secondChunk));
    TEST_ASSERT(read == sizeof(secondChunk), "Second read size mismatch");
    TEST_ASSERT(memcmp(secondChunk, expected + TEST_BLOCK_SIZE, sizeof(secondChunk)) == 0,
        "Second read did not continue from the previous file position");

    TEST_ASSERT(vafs_file_read(file, secondChunk, sizeof(secondChunk)) == 0, "Expected EOF after sequential reads");

    cleanup_image(vafs, NULL, file);
    free(firstChunk);
    free(expected);
    TEST_PASS("Sequential reads advance file position across block boundaries");
}

int main(void)
{
    int status = test_sequential_reads_advance_position();

    printf("\nTest Summary: %d passed, %d failed\n", g_test_passed, g_test_failed);
    return status == 0 && g_test_failed == 0 ? 0 : 1;
}