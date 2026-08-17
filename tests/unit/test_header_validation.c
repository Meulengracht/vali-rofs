/**
 * Test program for VaFS header validation
 *
 * This program creates intentionally malformed VaFS images with invalid headers
 * to test the bounds validation and error handling in __verify_header().
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vafs/vafs.h>
#include <vafs/reader.h>
#include <vafs/builder.h>
#include "test_common.h"

static void write_valid_header(FILE* fp) {
    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.Architecture = 0;
    header.FeatureCount = 0;
    header.Reserved = 0;
    test_initialize_stream_layouts(&header);
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);
}

static void write_minimal_descriptor_stream(FILE* fp) {
    test_write_minimal_descriptor_stream(fp);
}

// Test 1: Invalid magic number
static int test_invalid_magic(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = 0xDEADBEEF; // WRONG!
    header.Version = VA_FS_VERSION;
    header.FeatureCount = 0;
    header.Reserved = 0;
    test_initialize_stream_layouts(&header);
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);

    write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

// Test 2: Invalid version
static int test_invalid_version(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = 0x00040000; // Unsupported future version
    header.FeatureCount = 0;
    header.Reserved = 0;
    test_initialize_stream_layouts(&header);
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);

    write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

// Test 3: Feature count exceeds maximum
static int test_excessive_feature_count(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.FeatureCount = VA_FS_MAX_FEATURES + 1; // TOO MANY!
    header.Reserved = 0;
    test_initialize_stream_layouts(&header);
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);

    write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

// Test 4: Non-zero reserved field
static int test_nonzero_reserved(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.FeatureCount = 0;
    header.Reserved = 0xABCD; // Should be zero!
    test_initialize_stream_layouts(&header);
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);

    write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

// Test 5: Descriptor block offset before header end
static int test_descriptor_offset_too_small(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.FeatureCount = 0;
    header.Reserved = 0;
    test_initialize_stream_layouts(&header);
    header.DescriptorStream.DataOffset = 10; // Before end of header!
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);

    write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

// Test 6: Data block offset before or equal to descriptor block offset
static int test_data_offset_collision(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.FeatureCount = 0;
    header.Reserved = 0;
    test_initialize_stream_layouts(&header);
    header.DataStream.DataOffset = header.DescriptorStream.DataOffset; // Same as descriptor offset!
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);

    write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

// Test 7: Descriptor offset too large (overflow scenario)
static int test_descriptor_offset_too_large(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.FeatureCount = 0;
    header.Reserved = 0;
    test_initialize_stream_layouts(&header);
    header.DescriptorStream.IndexOffset = header.DescriptorStream.DataOffset - 1; // Index before data
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);

    write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

// Test 8: Invalid root descriptor block index
static int test_invalid_root_block_index(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.FeatureCount = 0;
    header.Reserved = 0;
    test_initialize_stream_layouts(&header);
    header.RootDescriptor.Index = VA_FS_INVALID_BLOCK; // Invalid!
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);

    write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

// Test 9: Invalid root descriptor offset
static int test_invalid_root_offset(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.FeatureCount = 0;
    header.Reserved = 0;
    test_initialize_stream_layouts(&header);
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = VA_FS_INVALID_OFFSET; // Invalid!
    fwrite(&header, sizeof(header), 1, fp);

    write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

// Test 10: Root descriptor offset exceeds block size
static int test_root_offset_exceeds_block_size(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.FeatureCount = 0;
    header.Reserved = 0;
    test_initialize_stream_layouts(&header);
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = VA_FS_DESCRIPTOR_BLOCK_SIZE + 100; // Too large!
    fwrite(&header, sizeof(header), 1, fp);

    write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

// Test 11: Truncated header (file too short)
static int test_truncated_header(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    // Write only half the header
    fwrite(&header, sizeof(header) / 2, 1, fp);
    fclose(fp);
    return 0;
}

typedef struct {
    const char* name;
    int (*test_func)(const char*);
} TestCase;

static TestCase tests[] = {
    {"invalid_magic", test_invalid_magic},
    {"invalid_version", test_invalid_version},
    {"excessive_feature_count", test_excessive_feature_count},
    {"nonzero_reserved", test_nonzero_reserved},
    {"descriptor_offset_too_small", test_descriptor_offset_too_small},
    {"data_offset_collision", test_data_offset_collision},
    {"descriptor_offset_too_large", test_descriptor_offset_too_large},
    {"invalid_root_block_index", test_invalid_root_block_index},
    {"invalid_root_offset", test_invalid_root_offset},
    {"root_offset_exceeds_block_size", test_root_offset_exceeds_block_size},
    {"truncated_header", test_truncated_header},
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <output_directory>\n", argv[0]);
        return 1;
    }

    const char* output_dir = argv[1];
    int num_tests = sizeof(tests) / sizeof(tests[0]);
    int failed = 0;

    printf("Creating %d malformed header test images in %s/\n", num_tests, output_dir);

    for (int i = 0; i < num_tests; i++) {
        char filename[256];
        snprintf(filename, sizeof(filename), "%s/header_%s.vafs", output_dir, tests[i].name);

        printf("  Creating %s... ", filename);
        fflush(stdout);

        if (tests[i].test_func(filename) != 0) {
            printf("FAILED to create\n");
            failed++;
            continue;
        }

        // Try to open the image - it should fail
        struct VaFs* vafs = NULL;
        int result = vafs_reader_open_file(filename, NULL, &vafs);

        if (result == 0) {
            printf("FAILED (image was accepted when it should be rejected)\n");
            vafs_reader_close(vafs);
            failed++;
        } else {
            printf("OK (rejected as expected)\n");
        }
    }

    printf("\nSummary: %d/%d tests passed\n", num_tests - failed, num_tests);
    return failed > 0 ? 1 : 0;
}
