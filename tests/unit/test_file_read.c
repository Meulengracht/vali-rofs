/**
 * Regression tests for file read semantics.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif
#include <vafs/vafs.h>
#include <vafs/builder.h>
#include <vafs/reader.h>
#include <vafs/directory.h>
#include <vafs/file.h>
#include "test_common.h"

#define TEST_IMAGE_PATH "test_file_read.vafs"
#define TEST_BLOCK_SIZE 8192u
#define TEST_DATA_BLOCK_SIZE (16u * 1024u)
#define TEST_TAIL_SIZE  128u
#define TEST_EXPANDING_CODEC_ID "expand"
#define TEST_DESCRIPTOR_CODEC_ID "dexp"
#define TEST_DATA_CODEC_ID "data"
#define TEST_CUSTOM_DESCRIPTOR_CODEC_ID "cust"

static struct VaFsGuid g_filterGuid = VA_FS_FEATURE_FILTER;
static int g_descriptor_encode_calls = 0;
static int g_data_encode_calls = 0;
static int g_custom_descriptor_decode_calls = 0;

#define TEST_CUSTOM_DESCRIPTOR_FILTER_TYPE 0x56414653u

struct ReadAtOnlyBuffer {
    const uint8_t* Data;
    size_t         Length;
    int            ReadAtCalls;
};

#if defined(_WIN32) || defined(_WIN64)
struct BlockingReadAtBuffer {
    const uint8_t* Data;
    size_t         Length;
    HANDLE         FirstReadEntered;
    HANDLE         SecondReadEntered;
    HANDLE         ReleaseReads;
    volatile LONG  BlockingEnabled;
    volatile LONG  BlockingReadCalls;
};

struct ConcurrentReadWorker {
    struct VaFsFileHandle* File;
    char*                  Buffer;
    size_t                 Size;
    size_t                 BytesRead;
    int                    ErrnoValue;
};
#endif

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

static struct VaFsMetadata metadata_for_mode(
    enum VaFsEntryType type,
    uint32_t           mode)
{
    struct VaFsMetadata metadata;

    vafs_metadata_initialize(&metadata);
    vafs_metadata_set_mode(&metadata, type, mode);
    return metadata;
}

static void fill_pattern(char* buffer, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        buffer[i] = (char)('A' + (i % 26));
    }
}

static int expanding_encode(const void* input, size_t inputLength, void** output, size_t* outputLength)
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

static int fail_decode(const void* input, size_t inputLength, void* output, size_t outputLength, size_t* bytesWrittenOut)
{
    (void)input;
    (void)inputLength;
    (void)output;
    (void)outputLength;
    (void)bytesWrittenOut;
    errno = ENOTSUP;
    return -1;
}

static int descriptor_expanding_encode(const void* input, size_t inputLength, void** output, size_t* outputLength)
{
    g_descriptor_encode_calls++;
    return expanding_encode(input, inputLength, output, outputLength);
}

static int data_expanding_encode(const void* input, size_t inputLength, void** output, size_t* outputLength)
{
    g_data_encode_calls++;
    return expanding_encode(input, inputLength, output, outputLength);
}

static int install_expanding_filter(struct VaFs* vafs)
{
    struct VaFsFeatureEncoding filter;

    // Install a filter that always expands data so the writer must fall back to
    // BLOCK_FLAG_STORED instead of persisting filtered bytes.

    memset(&filter, 0, sizeof(filter));
    memcpy(&filter.Header.Guid, &g_filterGuid, sizeof(struct VaFsGuid));
    filter.Header.Length = sizeof(struct VaFsFeatureEncoding);
    strncpy(filter.DescriptorEncoding, TEST_EXPANDING_CODEC_ID, sizeof(filter.DescriptorEncoding) - 1);
    strncpy(filter.DataEncoding, TEST_EXPANDING_CODEC_ID, sizeof(filter.DataEncoding) - 1);

    return vafs_builder_add_feature(vafs, &filter.Header);
}

static void configure_split_stream_codecs(struct VaFsBuilderConfiguration* configuration)
{
    // Give each stream its own encode hook so the test can prove descriptor and
    // data writes dispatch independently.

    configuration->Codecs[0] = (struct VaFsCodec) {
        .ID = TEST_DESCRIPTOR_CODEC_ID,
        .Encode = descriptor_expanding_encode,
        .Decode = fail_decode
    };
    configuration->Codecs[1] = (struct VaFsCodec) {
        .ID = TEST_DATA_CODEC_ID,
        .Encode = data_expanding_encode,
        .Decode = fail_decode
    };
}

static int custom_descriptor_encode(const void* input, size_t inputLength, void** output, size_t* outputLength)
{
    uint8_t* encoded;
    size_t   readOffset = 0;
    size_t   writeOffset = 0;

    // This simple zero-run codec is intentionally custom to the test: it is
    // good enough to shrink descriptor blocks that contain many zero-filled
    // metadata fields, but libvafs must not know anything about how it works.
    encoded = malloc((inputLength * 2u) + 2u);
    if (encoded == NULL) {
        errno = ENOMEM;
        return -1;
    }

    while (readOffset < inputLength) {
        size_t zeroRun = 0;

        while ((readOffset + zeroRun) < inputLength &&
               ((const uint8_t*)input)[readOffset + zeroRun] == 0 &&
               zeroRun < 255u) {
            zeroRun++;
        }

        if (zeroRun >= 4u) {
            encoded[writeOffset++] = 0;
            encoded[writeOffset++] = (uint8_t)zeroRun;
            readOffset += zeroRun;
            continue;
        }

        size_t literalStart = readOffset;
        size_t literalLength = 0;

        while (readOffset < inputLength && literalLength < 255u) {
            zeroRun = 0;
            while ((readOffset + zeroRun) < inputLength &&
                   ((const uint8_t*)input)[readOffset + zeroRun] == 0 &&
                   zeroRun < 4u) {
                zeroRun++;
            }

            if (zeroRun >= 4u) {
                break;
            }

            readOffset++;
            literalLength++;
        }

        encoded[writeOffset++] = 1;
        encoded[writeOffset++] = (uint8_t)literalLength;
        memcpy(encoded + writeOffset, ((const uint8_t*)input) + literalStart, literalLength);
        writeOffset += literalLength;
    }

    *output = encoded;
    *outputLength = writeOffset;
    return 0;
}

static int custom_descriptor_decode(const void* input, size_t inputLength, void* output, size_t outputLength, size_t* bytesWrittenOut)
{
    size_t readOffset = 0;
    size_t writeOffset = 0;

    g_custom_descriptor_decode_calls++;

    while (readOffset < inputLength) {
        uint8_t tag;
        uint8_t length;

        if ((inputLength - readOffset) < 2u) {
            errno = EINVAL;
            return -1;
        }

        tag = ((uint8_t*)input)[readOffset++];
        length = ((uint8_t*)input)[readOffset++];

        if ((size_t)length > (outputLength - writeOffset)) {
            errno = ENOSPC;
            return -1;
        }

        if (tag == 0) {
            memset(((uint8_t*)output) + writeOffset, 0, length);
            writeOffset += length;
            continue;
        }

        if (tag != 1 || (inputLength - readOffset) < length) {
            errno = EINVAL;
            return -1;
        }

        memcpy(((uint8_t*)output) + writeOffset, ((uint8_t*)input) + readOffset, length);
        readOffset += length;
        writeOffset += length;
    }

    *bytesWrittenOut = writeOffset;
    return 0;
}

static int install_custom_descriptor_filter(struct VaFs* vafs)
{
    struct VaFsFeatureEncoding filter;

    memset(&filter, 0, sizeof(filter));
    memcpy(&filter.Header.Guid, &g_filterGuid, sizeof(struct VaFsGuid));
    filter.Header.Length = sizeof(struct VaFsFeatureEncoding);
    strncpy(filter.DescriptorEncoding, TEST_CUSTOM_DESCRIPTOR_CODEC_ID, sizeof(filter.DescriptorEncoding) - 1);

    return vafs_builder_add_feature(vafs, &filter.Header);
}

static void configure_custom_descriptor_codec(struct VaFsBuilderConfiguration* configuration)
{
    configuration->Codecs[0] = (struct VaFsCodec) {
        .ID = TEST_CUSTOM_DESCRIPTOR_CODEC_ID,
        .Encode = custom_descriptor_encode,
        .Decode = custom_descriptor_decode
    };
}

static int read_stream_block_sizes(uint32_t* descriptorBlockSizeOut, uint32_t* dataBlockSizeOut)
{
    FILE*             fp;
    VaFsHeader_t      header;

    // Stream layout metadata now lives in the outer image header, so the test
    // can verify descriptor/data block sizes without seeking into stream-local
    // headers.

    fp = fopen(TEST_IMAGE_PATH, "rb");
    if (fp == NULL) {
        return -1;
    }

    if (fread(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        return -1;
    }

    *descriptorBlockSizeOut = header.DescriptorStream.BlockSize;
    *dataBlockSizeOut = header.DataStream.BlockSize;

    fclose(fp);
    return 0;
}

static int load_image_bytes(void** bufferOut, size_t* lengthOut)
{
    FILE* fp;
    long  length;
    void* buffer;

    fp = fopen(TEST_IMAGE_PATH, "rb");
    if (fp == NULL) {
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }

    length = ftell(fp);
    if (length <= 0) {
        fclose(fp);
        return -1;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }

    buffer = malloc((size_t)length);
    if (buffer == NULL) {
        fclose(fp);
        errno = ENOMEM;
        return -1;
    }

    if (fread(buffer, 1, (size_t)length, fp) != (size_t)length) {
        fclose(fp);
        free(buffer);
        return -1;
    }

    fclose(fp);
    *bufferOut = buffer;
    *lengthOut = (size_t)length;
    return 0;
}

static int read_at_only(void* userData, uint64_t offset, void* buffer, size_t length, size_t* bytesRead)
{
    struct ReadAtOnlyBuffer* image = userData;

    if (image == NULL || buffer == NULL || bytesRead == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (offset > image->Length || length > image->Length - (size_t)offset) {
        errno = EIO;
        return -1;
    }

    memcpy(buffer, image->Data + offset, length);
    *bytesRead = length;
    image->ReadAtCalls++;
    return 0;
}

static int read_at_only_get_size(void* userData, uint64_t* sizeOut)
{
    struct ReadAtOnlyBuffer* image = userData;

    if (image == NULL || sizeOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    *sizeOut = image->Length;
    return 0;
}

#if defined(_WIN32) || defined(_WIN64)
static int blocking_read_at(void* userData, uint64_t offset, void* buffer, size_t length, size_t* bytesRead)
{
    struct BlockingReadAtBuffer* image = userData;

    if (image == NULL || buffer == NULL || bytesRead == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (offset > image->Length || length > image->Length - (size_t)offset) {
        errno = EIO;
        return -1;
    }

    // When blocking is enabled, hold the first two readAt calls long enough
    // for the test to observe whether both file handles reach the backend.
    if (InterlockedCompareExchange(&image->BlockingEnabled, 0, 0) != 0) {
        LONG readCalls = InterlockedIncrement(&image->BlockingReadCalls);

        if (readCalls == 1) {
            SetEvent(image->FirstReadEntered);
        }
        else if (readCalls == 2) {
            SetEvent(image->SecondReadEntered);
        }

        if (WaitForSingleObject(image->ReleaseReads, 5000) != WAIT_OBJECT_0) {
            errno = EBUSY;
            return -1;
        }
    }

    memcpy(buffer, image->Data + offset, length);
    *bytesRead = length;
    return 0;
}

static DWORD WINAPI concurrent_read_worker(LPVOID parameter)
{
    struct ConcurrentReadWorker* worker = parameter;

    // Run one file-handle read to completion and capture both the byte count
    // and the thread-local errno visible to the caller path.
    worker->BytesRead = vafs_file_read(worker->File, worker->Buffer, worker->Size);
    worker->ErrnoValue = errno;
    return 0;
}
#endif

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
        vafs_reader_close(vafs);
    }
    remove(TEST_IMAGE_PATH);
}

static int test_sequential_reads_advance_position(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsBuilderConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    struct VaFsFileHandle* file = NULL;
    char* expected = NULL;
    char* firstChunk = NULL;
    char secondChunk[TEST_TAIL_SIZE] = { 0 };
    size_t expectedLength = TEST_BLOCK_SIZE + TEST_TAIL_SIZE;
    struct VaFsMetadata fileMetadata = metadata_for_mode(VaFsEntryType_File, 0644);
    size_t read;
    int status;

    // Create a payload that spans a block boundary, reopen it, and prove that
    // consecutive reads continue from the previous file position.

    expected = malloc(expectedLength);
    firstChunk = malloc(TEST_BLOCK_SIZE);
    TEST_ASSERT(expected != NULL && firstChunk != NULL, "Failed to allocate test buffers");

    fill_pattern(expected, expectedLength);

    vafs_builder_config_initialize(&config);
    vafs_builder_config_set_data_block_size(&config, TEST_BLOCK_SIZE);

    status = vafs_builder_new(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory");

    status = vafs_directory_create_file(root, "payload", &fileMetadata, &file);
    TEST_ASSERT(status == 0, "Failed to create test file");

    read = vafs_file_write(file, expected, expectedLength);
    TEST_ASSERT(read == 0, "Failed to write test payload");

    vafs_file_close(file);
    file = NULL;
    vafs_directory_close(root);
    root = NULL;
    vafs_builder_close(vafs);
    vafs = NULL;

    status = vafs_reader_open_file(TEST_IMAGE_PATH, NULL, &vafs);
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
    struct VaFsBuilderConfiguration config;
    struct VaFsReaderConfiguration readerConfig;
    struct VaFsCodec codecs[] = {
        {
            .ID = TEST_EXPANDING_CODEC_ID,
            .Encode = expanding_encode,
            .Decode = fail_decode
        }
    };
    struct VaFsDirectoryHandle* root = NULL;
    struct VaFsFileHandle* file = NULL;
    const char payload[] = "stored block payload";
    char buffer[sizeof(payload)] = { 0 };
    struct VaFsMetadata fileMetadata = metadata_for_mode(VaFsEntryType_File, 0644);
    size_t read;
    int status;

    // Use an expanding codec so the writer chooses BLOCK_FLAG_STORED, then
    // reopen with a decode hook that fails if stored blocks incorrectly decode.

    vafs_builder_config_initialize(&config);
    vafs_builder_config_set_data_block_size(&config, TEST_BLOCK_SIZE);
    config.Codecs[0] = codecs[0];
    config.Codecs[1] = codecs[0];

    status = vafs_builder_new(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create filtered test image");

    status = install_expanding_filter(vafs);
    TEST_ASSERT(status == 0, "Failed to install expanding filter");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory");

    status = vafs_directory_create_file(root, "stored", &fileMetadata, &file);
    TEST_ASSERT(status == 0, "Failed to create filtered test file");

    read = vafs_file_write(file, (void*)payload, sizeof(payload) - 1);
    TEST_ASSERT(read == 0, "Failed to write filtered payload");

    vafs_file_close(file);
    file = NULL;
    vafs_directory_close(root);
    root = NULL;
    vafs_builder_close(vafs);
    vafs = NULL;

    vafs_reader_config_initialize(&readerConfig);
    vafs_reader_config_set_codecs(&readerConfig, codecs, 1);
    status = vafs_reader_open_file(TEST_IMAGE_PATH, &readerConfig, &vafs);
    TEST_ASSERT(status == 0, "Failed to reopen filtered test image");

    status = vafs_file_open(vafs, "/stored", &file);
    TEST_ASSERT(status == 0, "Failed to open stored-block file with registered codec");

    read = vafs_file_read(file, buffer, sizeof(payload) - 1);
    TEST_ASSERT(read == sizeof(payload) - 1, "Stored-block read size mismatch");
    TEST_ASSERT(memcmp(buffer, payload, sizeof(payload) - 1) == 0,
        "Stored blocks should be readable without invoking runtime decode");

    cleanup_image(vafs, NULL, file);
    TEST_PASS("Stored blocks bypass decode when compression is not beneficial");
}

static int test_descriptor_and_data_streams_can_diverge(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsBuilderConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    struct VaFsFileHandle* file = NULL;
    uint32_t descriptorBlockSize;
    uint32_t dataBlockSize;
    const char payload[] = "separate stream policies";
    struct VaFsMetadata fileMetadata = metadata_for_mode(VaFsEntryType_File, 0644);
    int status;

    // Configure distinct block sizes and runtime callbacks, then verify both
    // streams exercised their own policy and persisted their own block size.

    g_descriptor_encode_calls = 0;
    g_data_encode_calls = 0;

    vafs_builder_config_initialize(&config);
    vafs_builder_config_set_descriptor_block_size(&config, TEST_BLOCK_SIZE);
    vafs_builder_config_set_data_block_size(&config, TEST_DATA_BLOCK_SIZE);
    configure_split_stream_codecs(&config);

    status = vafs_builder_new(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create split-policy test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory");

    status = vafs_directory_create_file(root, "split", &fileMetadata, &file);
    TEST_ASSERT(status == 0, "Failed to create split test file");

    TEST_ASSERT(vafs_file_write(file, (void*)payload, sizeof(payload) - 1) == 0, "Failed to write split test payload");

    vafs_file_close(file);
    file = NULL;
    vafs_directory_close(root);
    root = NULL;
    vafs_builder_close(vafs);
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

static int test_root_open_waits_for_custom_filter_ops(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsBuilderConfiguration config;
    struct VaFsReaderConfiguration readerConfig;
    struct VaFsCodec customCodec = {
        .ID = TEST_CUSTOM_DESCRIPTOR_CODEC_ID,
        .Encode = custom_descriptor_encode,
        .Decode = custom_descriptor_decode
    };
    struct VaFsDirectoryHandle* root = NULL;
    struct VaFsDirectoryHandle* reopenedRoot = NULL;
    struct VaFsFileHandle* file = NULL;
    struct VaFsFeatureEncoding* filter = NULL;
    char name[32];
    struct VaFsMetadata fileMetadata = metadata_for_mode(VaFsEntryType_File, 0644);
    int status;

    // Custom descriptor codecs are persisted by ID and must be registered when
    // the reader opens the image.

    vafs_builder_config_initialize(&config);
    configure_custom_descriptor_codec(&config);
    status = vafs_builder_new(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create custom-filter test image");

    status = install_custom_descriptor_filter(vafs);
    TEST_ASSERT(status == 0, "Failed to install custom descriptor filter");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory for custom-filter image");

    for (int i = 0; i < 64; ++i) {
        snprintf(name, sizeof(name), "entry-%02d", i);
        status = vafs_directory_create_file(root, name, &fileMetadata, &file);
        TEST_ASSERT(status == 0, "Failed to create custom-filter descriptor entry");
        vafs_file_close(file);
        file = NULL;
    }

    vafs_directory_close(root);
    root = NULL;
    vafs_builder_close(vafs);
    vafs = NULL;

    g_custom_descriptor_decode_calls = 0;

    errno = 0;
    status = vafs_reader_open_file(TEST_IMAGE_PATH, NULL, &vafs);
    TEST_ASSERT(status != 0 && errno == ENOTSUP, "Open should fail before custom descriptor codec is registered");

    vafs_reader_config_initialize(&readerConfig);
    vafs_reader_config_set_codecs(&readerConfig, &customCodec, 1);
    status = vafs_reader_open_file(TEST_IMAGE_PATH, &readerConfig, &vafs);
    TEST_ASSERT(status == 0, "Open should succeed after custom descriptor codec is registered");

    status = vafs_reader_query_feature(vafs, &g_filterGuid, (struct VaFsFeatureHeader**)&filter);
    TEST_ASSERT(status == 0, "Failed to query persisted custom filter feature");
    TEST_ASSERT(strcmp(filter->DescriptorEncoding, TEST_CUSTOM_DESCRIPTOR_CODEC_ID) == 0,
        "Persisted descriptor codec id mismatch");

    status = vafs_directory_open(vafs, "/", &reopenedRoot);
    TEST_ASSERT(status == 0, "Failed to open root directory after registering custom descriptor codec");
    TEST_ASSERT(g_custom_descriptor_decode_calls > 0,
        "Expected custom descriptor decode to run during lazy root open");

    cleanup_image(vafs, reopenedRoot, NULL);
    TEST_PASS("Custom descriptor codecs are supplied through reader configuration");
}

static int test_open_ops_accepts_read_at_only_backend(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsBuilderConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    struct VaFsFileHandle* file = NULL;
    struct VaFsReaderBackendOps ops = { 0 };
    struct ReadAtOnlyBuffer image = { 0 };
    void* imageData = NULL;
    const char payload[] = "read-at backend";
    char buffer[sizeof(payload)] = { 0 };
    struct VaFsMetadata fileMetadata = metadata_for_mode(VaFsEntryType_File, 0644);
    size_t read;
    int status;

    // Prove that the read path no longer requires a mutable device cursor by
    // reopening an image through a backend that only implements readAt.

    vafs_builder_config_initialize(&config);
    vafs_builder_config_set_data_block_size(&config, TEST_BLOCK_SIZE);

    status = vafs_builder_new(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create readAt-only test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory");

    status = vafs_directory_create_file(root, "ops", &fileMetadata, &file);
    TEST_ASSERT(status == 0, "Failed to create readAt-only test file");

    TEST_ASSERT(vafs_file_write(file, (void*)payload, sizeof(payload) - 1) == 0, "Failed to write readAt-only payload");

    vafs_file_close(file);
    file = NULL;
    vafs_directory_close(root);
    root = NULL;
    vafs_builder_close(vafs);
    vafs = NULL;

    status = load_image_bytes(&imageData, &image.Length);
    TEST_ASSERT(status == 0, "Failed to load image into memory");
    image.Data = imageData;

    ops.readAt = read_at_only;
    ops.getSize = read_at_only_get_size;

    status = vafs_reader_open_ops(&ops, &image, NULL, &vafs);
    TEST_ASSERT(status == 0, "Failed to open image with readAt-only backend");

    status = vafs_file_open(vafs, "/ops", &file);
    TEST_ASSERT(status == 0, "Failed to open file through readAt-only backend");

    read = vafs_file_read(file, buffer, sizeof(payload) - 1);
    TEST_ASSERT(read == sizeof(payload) - 1, "Read size mismatch through readAt-only backend");
    TEST_ASSERT(memcmp(buffer, payload, sizeof(payload) - 1) == 0, "Read content mismatch through readAt-only backend");
    TEST_ASSERT(image.ReadAtCalls > 0, "Expected readAt callback to service the read path");

    cleanup_image(vafs, NULL, file);
    free(imageData);
    TEST_PASS("Read-only custom backends can rely on readAt without seek+read");
}

static int test_boundary_read_does_not_prefetch_next_stream_block(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsBuilderConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    struct VaFsFileHandle* first = NULL;
    struct VaFsFileHandle* second = NULL;
    struct VaFsReaderBackendOps ops = { 0 };
    struct ReadAtOnlyBuffer image = { 0 };
    void* imageData = NULL;
    char* payload = NULL;
    char* buffer = NULL;
    const char tailPayload[] = "tail";
    struct VaFsMetadata fileMetadata = metadata_for_mode(VaFsEntryType_File, 0644);
    size_t read;
    int status;

    // Read exactly one logical block from the first file while a later block
    // exists in the same stream. The read path should not fetch the successor
    // block once the caller's request is already complete.

    payload = malloc(TEST_BLOCK_SIZE);
    buffer = malloc(TEST_BLOCK_SIZE);
    TEST_ASSERT(payload != NULL && buffer != NULL, "Failed to allocate boundary-read buffers");

    fill_pattern(payload, TEST_BLOCK_SIZE);

    vafs_builder_config_initialize(&config);
    vafs_builder_config_set_data_block_size(&config, TEST_BLOCK_SIZE);

    status = vafs_builder_new(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create boundary-read test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory");

    status = vafs_directory_create_file(root, "first", &fileMetadata, &first);
    TEST_ASSERT(status == 0, "Failed to create first boundary-read file");
    TEST_ASSERT(vafs_file_write(first, payload, TEST_BLOCK_SIZE) == 0, "Failed to write first boundary-read payload");
    vafs_file_close(first);
    first = NULL;

    status = vafs_directory_create_file(root, "second", &fileMetadata, &second);
    TEST_ASSERT(status == 0, "Failed to create second boundary-read file");
    TEST_ASSERT(vafs_file_write(second, (void*)tailPayload, sizeof(tailPayload) - 1) == 0,
        "Failed to write second boundary-read payload");
    vafs_file_close(second);
    second = NULL;

    vafs_directory_close(root);
    root = NULL;
    vafs_builder_close(vafs);
    vafs = NULL;

    status = load_image_bytes(&imageData, &image.Length);
    TEST_ASSERT(status == 0, "Failed to load boundary-read image");
    image.Data = imageData;

    ops.readAt = read_at_only;
    ops.getSize = read_at_only_get_size;
    status = vafs_reader_open_ops(&ops, &image, NULL, &vafs);
    TEST_ASSERT(status == 0, "Failed to reopen boundary-read image");

    status = vafs_file_open(vafs, "/first", &first);
    TEST_ASSERT(status == 0, "Failed to open first boundary-read file");

    image.ReadAtCalls = 0;
    read = vafs_file_read(first, buffer, TEST_BLOCK_SIZE);
    TEST_ASSERT(read == TEST_BLOCK_SIZE, "Boundary read size mismatch");
    TEST_ASSERT(memcmp(buffer, payload, TEST_BLOCK_SIZE) == 0, "Boundary read content mismatch");
    TEST_ASSERT(image.ReadAtCalls == 1, "Boundary read should consume exactly one backend block read");

    cleanup_image(vafs, NULL, first);
    free(imageData);
    free(buffer);
    free(payload);
    TEST_PASS("Reads that finish on a block boundary do not prefetch the next stream block");
}

static int test_concurrent_file_handles_do_not_serialize_on_stream_lock(void)
{
#if defined(_WIN32) || defined(_WIN64)
    struct VaFs* vafs = NULL;
    struct VaFsBuilderConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    struct VaFsFileHandle* writer = NULL;
    struct VaFsFileHandle* readerA = NULL;
    struct VaFsFileHandle* readerB = NULL;
    struct VaFsReaderBackendOps ops = { 0 };
    struct BlockingReadAtBuffer image = { 0 };
    void* imageData = NULL;
    struct ConcurrentReadWorker workerA = { 0 };
    struct ConcurrentReadWorker workerB = { 0 };
    char expected[64];
    char payload[256];
    HANDLE threadA = NULL;
    HANDLE threadB = NULL;
    HANDLE waitHandles[2];
    DWORD waitStatus;
    struct VaFsMetadata fileMetadata = metadata_for_mode(VaFsEntryType_File, 0644);
    int status;

    // Hold the first backend read inside readAt, then start a second file
    // handle. Before the reader refactor the second thread failed on the shared
    // stream lock with EBUSY instead of reaching the backend.

    fill_pattern(payload, sizeof(payload));
    memcpy(expected, payload, sizeof(expected));

    vafs_builder_config_initialize(&config);
    vafs_builder_config_set_data_block_size(&config, TEST_BLOCK_SIZE);

    status = vafs_builder_new(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create concurrent-read test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory");

    status = vafs_directory_create_file(root, "parallel", &fileMetadata, &writer);
    TEST_ASSERT(status == 0, "Failed to create concurrent-read test file");
    TEST_ASSERT(vafs_file_write(writer, payload, sizeof(payload)) == 0, "Failed to write concurrent-read payload");

    vafs_file_close(writer);
    writer = NULL;
    vafs_directory_close(root);
    root = NULL;
    vafs_builder_close(vafs);
    vafs = NULL;

    status = load_image_bytes(&imageData, &image.Length);
    TEST_ASSERT(status == 0, "Failed to load concurrent-read image");
    image.Data = imageData;

    image.FirstReadEntered = CreateEvent(NULL, TRUE, FALSE, NULL);
    image.SecondReadEntered = CreateEvent(NULL, TRUE, FALSE, NULL);
    image.ReleaseReads = CreateEvent(NULL, TRUE, FALSE, NULL);
    TEST_ASSERT(image.FirstReadEntered != NULL && image.SecondReadEntered != NULL && image.ReleaseReads != NULL,
        "Failed to create concurrent-read events");

    ops.readAt = blocking_read_at;
    ops.getSize = read_at_only_get_size;
    status = vafs_reader_open_ops(&ops, &image, NULL, &vafs);
    TEST_ASSERT(status == 0, "Failed to reopen image with blocking readAt backend");

    status = vafs_file_open(vafs, "/parallel", &readerA);
    TEST_ASSERT(status == 0, "Failed to open first concurrent reader");
    status = vafs_file_open(vafs, "/parallel", &readerB);
    TEST_ASSERT(status == 0, "Failed to open second concurrent reader");

    workerA.File = readerA;
    workerA.Buffer = (char*)calloc(1, sizeof(expected));
    workerA.Size = sizeof(expected);
    workerB.File = readerB;
    workerB.Buffer = (char*)calloc(1, sizeof(expected));
    workerB.Size = sizeof(expected);
    TEST_ASSERT(workerA.Buffer != NULL && workerB.Buffer != NULL, "Failed to allocate concurrent-read buffers");

    InterlockedExchange(&image.BlockingEnabled, 1);

    threadA = CreateThread(NULL, 0, concurrent_read_worker, &workerA, 0, NULL);
    TEST_ASSERT(threadA != NULL, "Failed to start first concurrent reader thread");

    waitStatus = WaitForSingleObject(image.FirstReadEntered, 5000);
    TEST_ASSERT(waitStatus == WAIT_OBJECT_0, "First concurrent reader never reached backend readAt");

    threadB = CreateThread(NULL, 0, concurrent_read_worker, &workerB, 0, NULL);
    TEST_ASSERT(threadB != NULL, "Failed to start second concurrent reader thread");

    waitHandles[0] = image.SecondReadEntered;
    waitHandles[1] = threadB;
    waitStatus = WaitForMultipleObjects(2, waitHandles, FALSE, 5000);

    SetEvent(image.ReleaseReads);

    TEST_ASSERT(WaitForSingleObject(threadA, 5000) == WAIT_OBJECT_0, "First concurrent reader thread did not finish");
    TEST_ASSERT(WaitForSingleObject(threadB, 5000) == WAIT_OBJECT_0, "Second concurrent reader thread did not finish");
    TEST_ASSERT(waitStatus == WAIT_OBJECT_0, "Second file handle failed before reaching backend readAt");
    TEST_ASSERT(workerA.BytesRead == workerA.Size, "First concurrent reader size mismatch");
    TEST_ASSERT(workerB.BytesRead == workerB.Size, "Second concurrent reader size mismatch");
    TEST_ASSERT(memcmp(workerA.Buffer, expected, sizeof(expected)) == 0, "First concurrent reader content mismatch");
    TEST_ASSERT(memcmp(workerB.Buffer, expected, sizeof(expected)) == 0, "Second concurrent reader content mismatch");
    TEST_ASSERT(image.BlockingReadCalls >= 2, "Expected both concurrent readers to reach the backend");

    CloseHandle(threadA);
    CloseHandle(threadB);
    CloseHandle(image.FirstReadEntered);
    CloseHandle(image.SecondReadEntered);
    CloseHandle(image.ReleaseReads);
    free(workerA.Buffer);
    free(workerB.Buffer);
    vafs_file_close(readerA);
    vafs_file_close(readerB);
    vafs_reader_close(vafs);
    free((void*)image.Data);
    remove(TEST_IMAGE_PATH);
    TEST_PASS("Concurrent file handles can overlap reads without sharing a stream lock");
#else
    TEST_PASS("Concurrent file-handle read regression is Windows-only in this test binary");
#endif
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

    if (status == 0) {
        status = test_root_open_waits_for_custom_filter_ops();
    }

    if (status == 0) {
        status = test_open_ops_accepts_read_at_only_backend();
    }

    if (status == 0) {
        status = test_boundary_read_does_not_prefetch_next_stream_block();
    }

    if (status == 0) {
        status = test_concurrent_file_handles_do_not_serialize_on_stream_lock();
    }

    printf("\nTest Summary: %d passed, %d failed\n", g_test_passed, g_test_failed);
    return status == 0 && g_test_failed == 0 ? 0 : 1;
}