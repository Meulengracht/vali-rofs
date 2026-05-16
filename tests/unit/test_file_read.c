/**
 * Regression tests for file read semantics.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/vafs.h>
#include <vafs/directory.h>
#include <vafs/file.h>
#include "test_common.h"

#define TEST_IMAGE_PATH "test_file_read.vafs"
#define TEST_BLOCK_SIZE 8192u
#define TEST_DATA_BLOCK_SIZE (16u * 1024u)
#define TEST_TAIL_SIZE  128u

static struct VaFsGuid g_filterGuid = VA_FS_FEATURE_FILTER;
static struct VaFsGuid g_filterOpsGuid = VA_FS_FEATURE_FILTER_OPS;
static int g_descriptor_encode_calls = 0;
static int g_data_encode_calls = 0;

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

static int expanding_encode(void* input, uint32_t inputLength, void** output, uint32_t* outputLength)
{
    uint8_t* encoded = malloc(inputLength + 1);
    if (encoded == NULL) {
        errno = ENOMEM;
        return -1;
    }

    encoded[0] = 0xA5;
    memcpy(&encoded[1], input, inputLength);
    *output = encoded;
    *outputLength = inputLength + 1;
    return 0;
}

static int fail_decode(void* input, uint32_t inputLength, void* output, uint32_t* outputLength)
{
    (void)input;
    (void)inputLength;
    (void)output;
    (void)outputLength;
    errno = ENOTSUP;
    return -1;
}

static int descriptor_expanding_encode(void* input, uint32_t inputLength, void** output, uint32_t* outputLength)
{
    g_descriptor_encode_calls++;
    return expanding_encode(input, inputLength, output, outputLength);
}

static int data_expanding_encode(void* input, uint32_t inputLength, void** output, uint32_t* outputLength)
{
    g_data_encode_calls++;
    return expanding_encode(input, inputLength, output, outputLength);
}

static int install_expanding_filter(struct VaFs* vafs)
{
    struct VaFsFeatureFilter filter;
    struct VaFsFeatureFilterOps filterOps;
    int status;

    // Install a filter that always expands data so the writer must fall back to
    // BLOCK_FLAG_STORED instead of persisting filtered bytes.

    memcpy(&filter.Header.Guid, &g_filterGuid, sizeof(struct VaFsGuid));
    filter.Header.Length = sizeof(struct VaFsFeatureFilter);
    filter.DescriptorType = VaFsFilterType_BRIEFLZ;
    filter.DataType = VaFsFilterType_BRIEFLZ;

    status = vafs_feature_add(vafs, &filter.Header);
    if (status != 0) {
        return status;
    }

    memcpy(&filterOps.Header.Guid, &g_filterOpsGuid, sizeof(struct VaFsGuid));
    filterOps.Header.Length = sizeof(struct VaFsFeatureFilterOps);
    filterOps.DescriptorEncode = expanding_encode;
    filterOps.DescriptorDecode = fail_decode;
    filterOps.DataEncode = expanding_encode;
    filterOps.DataDecode = fail_decode;
    return vafs_feature_add(vafs, &filterOps.Header);
}

static int install_split_runtime_filters(struct VaFs* vafs)
{
    struct VaFsFeatureFilterOps filterOps;

    // Give each stream its own encode hook so the test can prove descriptor and
    // data writes dispatch independently.

    memcpy(&filterOps.Header.Guid, &g_filterOpsGuid, sizeof(struct VaFsGuid));
    filterOps.Header.Length = sizeof(struct VaFsFeatureFilterOps);
    filterOps.DescriptorEncode = descriptor_expanding_encode;
    filterOps.DescriptorDecode = fail_decode;
    filterOps.DataEncode = data_expanding_encode;
    filterOps.DataDecode = fail_decode;
    return vafs_feature_add(vafs, &filterOps.Header);
}

static int read_stream_block_sizes(uint32_t* descriptorBlockSizeOut, uint32_t* dataBlockSizeOut)
{
    FILE*             fp;
    VaFsHeader_t      header;
    VaFsStreamHeader_t streamHeader;

    // Read the raw image headers back directly so the test can verify that the
    // descriptor and data block sizes were persisted independently.

    fp = fopen(TEST_IMAGE_PATH, "rb");
    if (fp == NULL) {
        return -1;
    }

    if (fread(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        return -1;
    }

    if (fseek(fp, (long)header.DescriptorBlockOffset, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    if (fread(&streamHeader, sizeof(streamHeader), 1, fp) != 1) {
        fclose(fp);
        return -1;
    }
    *descriptorBlockSizeOut = streamHeader.BlockSize;

    if (fseek(fp, (long)header.DataBlockOffset, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    if (fread(&streamHeader, sizeof(streamHeader), 1, fp) != 1) {
        fclose(fp);
        return -1;
    }
    *dataBlockSizeOut = streamHeader.BlockSize;

    fclose(fp);
    return 0;
}

static void cleanup_image(struct VaFs* vafs, struct VaFsDirectoryHandle* root, struct VaFsFileHandle* file)
{
    // Tear down in reverse ownership order so partially constructed test cases
    // can safely reuse this helper.
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

    // Create a payload that spans a block boundary, reopen it, and prove that
    // consecutive reads continue from the previous file position.

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

static int test_stored_blocks_skip_runtime_decode(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    struct VaFsFileHandle* file = NULL;
    const char payload[] = "stored block payload";
    char buffer[sizeof(payload)] = { 0 };
    size_t read;
    int status;

    // Use an expanding filter so the writer chooses BLOCK_FLAG_STORED, then
    // reopen without runtime filter ops to verify that decode is skipped.

    vafs_config_initialize(&config);
    vafs_config_set_block_size(&config, TEST_BLOCK_SIZE);

    status = vafs_create(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create filtered test image");

    status = install_expanding_filter(vafs);
    TEST_ASSERT(status == 0, "Failed to install expanding filter");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory");

    status = vafs_directory_create_file(root, "stored", 0644, &file);
    TEST_ASSERT(status == 0, "Failed to create filtered test file");

    read = vafs_file_write(file, (void*)payload, sizeof(payload) - 1);
    TEST_ASSERT(read == 0, "Failed to write filtered payload");

    vafs_file_close(file);
    file = NULL;
    vafs_directory_close(root);
    root = NULL;
    vafs_close(vafs);
    vafs = NULL;

    status = vafs_open_file(TEST_IMAGE_PATH, &vafs);
    TEST_ASSERT(status == 0, "Failed to reopen filtered test image");

    status = vafs_file_open(vafs, "/stored", &file);
    TEST_ASSERT(status == 0, "Failed to open stored-block file without runtime filter ops");

    read = vafs_file_read(file, buffer, sizeof(payload) - 1);
    TEST_ASSERT(read == sizeof(payload) - 1, "Stored-block read size mismatch");
    TEST_ASSERT(memcmp(buffer, payload, sizeof(payload) - 1) == 0,
        "Stored blocks should be readable without invoking runtime decode");

    cleanup_image(vafs, NULL, file);
    TEST_PASS("Stored blocks bypass runtime decode when compression is not beneficial");
}

static int test_descriptor_and_data_streams_can_diverge(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    struct VaFsFileHandle* file = NULL;
    uint32_t descriptorBlockSize;
    uint32_t dataBlockSize;
    const char payload[] = "separate stream policies";
    int status;

    // Configure distinct block sizes and runtime callbacks, then verify both
    // streams exercised their own policy and persisted their own block size.

    g_descriptor_encode_calls = 0;
    g_data_encode_calls = 0;

    vafs_config_initialize(&config);
    vafs_config_set_descriptor_block_size(&config, TEST_BLOCK_SIZE);
    vafs_config_set_data_block_size(&config, TEST_DATA_BLOCK_SIZE);

    status = vafs_create(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create split-policy test image");

    status = install_split_runtime_filters(vafs);
    TEST_ASSERT(status == 0, "Failed to install split runtime filters");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory");

    status = vafs_directory_create_file(root, "split", 0644, &file);
    TEST_ASSERT(status == 0, "Failed to create split test file");

    TEST_ASSERT(vafs_file_write(file, (void*)payload, sizeof(payload) - 1) == 0, "Failed to write split test payload");

    vafs_file_close(file);
    file = NULL;
    vafs_directory_close(root);
    root = NULL;
    vafs_close(vafs);
    vafs = NULL;

    TEST_ASSERT(g_descriptor_encode_calls > 0, "Descriptor stream did not use its own encode callback");
    TEST_ASSERT(g_data_encode_calls > 0, "Data stream did not use its own encode callback");

    status = read_stream_block_sizes(&descriptorBlockSize, &dataBlockSize);
    TEST_ASSERT(status == 0, "Failed to read stream headers from image");
    TEST_ASSERT(descriptorBlockSize == TEST_BLOCK_SIZE, "Descriptor stream block size mismatch");
    TEST_ASSERT(dataBlockSize == TEST_DATA_BLOCK_SIZE, "Data stream block size mismatch");

    cleanup_image(NULL, NULL, NULL);
    TEST_PASS("Descriptor and data streams can use different block sizes and filter callbacks");
}

int main(void)
{
    int status = test_sequential_reads_advance_position();

    // Stop on the first regression so later failures do not obscure which path
    // broke first in the create/open/read sequence.
    if (status == 0) {
        status = test_stored_blocks_skip_runtime_decode();
    }

    if (status == 0) {
        status = test_descriptor_and_data_streams_can_diverge();
    }

    printf("\nTest Summary: %d passed, %d failed\n", g_test_passed, g_test_failed);
    return status == 0 && g_test_failed == 0 ? 0 : 1;
}