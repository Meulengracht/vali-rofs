/**
 * Comprehensive regression test suite for malformed VaFS images
 *
 * This program generates a comprehensive set of malformed VaFS images
 * to prevent future safety regressions. It covers:
 * - Truncated image cases
 * - Invalid offset cases
 * - Invalid descriptor length cases
 * - Invalid feature record cases
 * - Symlink loop cases
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/vafs.h>
#include "test_common.h"

// Test case structure
typedef struct {
    const char* name;
    const char* description;
    int (*generator)(const char* filename);
} TestCase;

// ============================================================================
// TRUNCATED IMAGE CASES
// ============================================================================

static int test_truncated_header_partial(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    // Write only part of the header (16 bytes instead of full header)
    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    fwrite(&header, 16, 1, fp);
    fclose(fp);
    return 0;
}

static int test_truncated_header_magic_only(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    // Write only the magic number
    uint32_t magic = VA_FS_MAGIC;
    fwrite(&magic, sizeof(magic), 1, fp);
    fclose(fp);
    return 0;
}

static int test_truncated_after_header(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    test_write_vafs_header(fp);
    // Don't write the stream header - truncate right after VaFS header
    fclose(fp);
    return 0;
}

static int test_truncated_descriptor_stream(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    test_write_vafs_header(fp);
    test_write_stream_header(fp, VA_FS_DESCRIPTOR_BLOCK_SIZE);
    // Write partial directory header (2 bytes instead of 4)
    uint16_t partialCount = 1;
    fwrite(&partialCount, sizeof(partialCount), 1, fp);
    fclose(fp);
    return 0;
}

static int test_truncated_in_descriptor(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    test_write_vafs_header(fp);
    test_write_stream_header(fp, VA_FS_DESCRIPTOR_BLOCK_SIZE);

    VaFsDirectoryHeader_t dirHeader = {1};
    fwrite(&dirHeader, sizeof(dirHeader), 1, fp);

    // Write partial file descriptor (only 10 bytes)
    VaFsFileDescriptor_t fileDesc = {0};
    fileDesc.Base.Type = VA_FS_DESCRIPTOR_TYPE_FILE;
    fileDesc.Base.Length = sizeof(VaFsFileDescriptor_t) + 10;
    fwrite(&fileDesc, 10, 1, fp);
    fclose(fp);
    return 0;
}

// ============================================================================
// INVALID OFFSET CASES
// ============================================================================

static int test_invalid_descriptor_offset_zero(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.DescriptorBlockOffset = 0; // INVALID - should be after header
    header.DataBlockOffset = sizeof(VaFsHeader_t) + VA_FS_DESCRIPTOR_BLOCK_SIZE;
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);

    test_write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

static int test_invalid_descriptor_offset_before_header(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.DescriptorBlockOffset = 10; // INVALID - before end of header
    header.DataBlockOffset = sizeof(VaFsHeader_t) + VA_FS_DESCRIPTOR_BLOCK_SIZE;
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);

    test_write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

static int test_invalid_data_offset_before_descriptor(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.DescriptorBlockOffset = sizeof(VaFsHeader_t);
    header.DataBlockOffset = sizeof(VaFsHeader_t); // INVALID - same as descriptor offset
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);

    test_write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

static int test_invalid_offset_overflow(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.DescriptorBlockOffset = 0xFFFFFFF0; // Very large - will overflow
    header.DataBlockOffset = 0xFFFFFFFF;
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);

    test_write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

static int test_invalid_root_descriptor_offset_huge(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.DescriptorBlockOffset = sizeof(VaFsHeader_t);
    header.DataBlockOffset = sizeof(VaFsHeader_t) + sizeof(VaFsStreamHeader_t) + VA_FS_DESCRIPTOR_BLOCK_SIZE;
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0xFFFFFFFF; // INVALID - way beyond block size
    fwrite(&header, sizeof(header), 1, fp);

    test_write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

// ============================================================================
// INVALID DESCRIPTOR LENGTH CASES
// ============================================================================

static int test_descriptor_length_too_small(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    test_write_vafs_header(fp);
    test_write_stream_header(fp, VA_FS_DESCRIPTOR_BLOCK_SIZE);

    VaFsDirectoryHeader_t dirHeader = {1};
    fwrite(&dirHeader, sizeof(dirHeader), 1, fp);

    // File descriptor with length smaller than base descriptor
    VaFsFileDescriptor_t fileDesc = {0};
    fileDesc.Base.Type = VA_FS_DESCRIPTOR_TYPE_FILE;
    fileDesc.Base.Length = 2; // INVALID - too small!
    fileDesc.Data.Index = VA_FS_INVALID_BLOCK;
    fileDesc.Data.Offset = VA_FS_INVALID_OFFSET;
    fwrite(&fileDesc, sizeof(fileDesc), 1, fp);
    fclose(fp);
    return 0;
}

static int test_descriptor_length_zero(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    test_write_vafs_header(fp);
    test_write_stream_header(fp, VA_FS_DESCRIPTOR_BLOCK_SIZE);

    VaFsDirectoryHeader_t dirHeader = {1};
    fwrite(&dirHeader, sizeof(dirHeader), 1, fp);

    // File descriptor with zero length
    VaFsFileDescriptor_t fileDesc = {0};
    fileDesc.Base.Type = VA_FS_DESCRIPTOR_TYPE_FILE;
    fileDesc.Base.Length = 0; // INVALID!
    fileDesc.Data.Index = VA_FS_INVALID_BLOCK;
    fileDesc.Data.Offset = VA_FS_INVALID_OFFSET;
    fwrite(&fileDesc, sizeof(fileDesc), 1, fp);
    fclose(fp);
    return 0;
}

static int test_descriptor_length_exceeds_block(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    test_write_vafs_header(fp);
    test_write_stream_header(fp, VA_FS_DESCRIPTOR_BLOCK_SIZE);

    VaFsDirectoryHeader_t dirHeader = {1};
    fwrite(&dirHeader, sizeof(dirHeader), 1, fp);

    // File descriptor claiming to be larger than block size
    VaFsFileDescriptor_t fileDesc = {0};
    fileDesc.Base.Type = VA_FS_DESCRIPTOR_TYPE_FILE;
    fileDesc.Base.Length = 65535; // Maximum uint16_t
    fileDesc.Data.Index = VA_FS_INVALID_BLOCK;
    fileDesc.Data.Offset = VA_FS_INVALID_OFFSET;
    fwrite(&fileDesc, sizeof(fileDesc), 1, fp);
    fclose(fp);
    return 0;
}

static int test_descriptor_length_mismatched_symlink(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    test_write_vafs_header(fp);
    test_write_stream_header(fp, VA_FS_DESCRIPTOR_BLOCK_SIZE);

    VaFsDirectoryHeader_t dirHeader = {1};
    fwrite(&dirHeader, sizeof(dirHeader), 1, fp);

    // Symlink where NameLength + TargetLength doesn't match total length
    VaFsSymlinkDescriptor_t symlinkDesc = {0};
    symlinkDesc.Base.Type = VA_FS_DESCRIPTOR_TYPE_SYMLINK;
    symlinkDesc.Base.Length = sizeof(VaFsSymlinkDescriptor_t) + 20; // Claims 20 bytes
    symlinkDesc.NameLength = 5;  // But these only add to 10
    symlinkDesc.TargetLength = 5;
    fwrite(&symlinkDesc, sizeof(symlinkDesc), 1, fp);
    fwrite("link\0", 5, 1, fp);
    fwrite("targ\0", 5, 1, fp);
    fclose(fp);
    return 0;
}

// ============================================================================
// INVALID FEATURE RECORD CASES
// ============================================================================

static int test_feature_count_exceeds_max(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.FeatureCount = VA_FS_MAX_FEATURES + 1; // INVALID - too many!
    header.Reserved = 0;
    header.DescriptorBlockOffset = sizeof(VaFsHeader_t);
    header.DataBlockOffset = sizeof(VaFsHeader_t) + sizeof(VaFsStreamHeader_t) + VA_FS_DESCRIPTOR_BLOCK_SIZE;
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);

    test_write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

static int test_feature_count_max(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.FeatureCount = 65535; // Maximum uint16_t
    header.Reserved = 0;
    header.DescriptorBlockOffset = sizeof(VaFsHeader_t);
    header.DataBlockOffset = sizeof(VaFsHeader_t) + sizeof(VaFsStreamHeader_t) + VA_FS_DESCRIPTOR_BLOCK_SIZE;
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);

    test_write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

static int test_reserved_field_nonzero(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.FeatureCount = 0;
    header.Reserved = 0xDEAD; // INVALID - should be zero
    header.DescriptorBlockOffset = sizeof(VaFsHeader_t);
    header.DataBlockOffset = sizeof(VaFsHeader_t) + sizeof(VaFsStreamHeader_t) + VA_FS_DESCRIPTOR_BLOCK_SIZE;
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);

    test_write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

// ============================================================================
// SYMLINK LOOP CASES
// ============================================================================

static int test_symlink_self_reference(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    test_write_vafs_header(fp);
    test_write_stream_header(fp, VA_FS_DESCRIPTOR_BLOCK_SIZE);

    VaFsDirectoryHeader_t dirHeader = {1};
    fwrite(&dirHeader, sizeof(dirHeader), 1, fp);

    // Symlink pointing to itself
    const char* name = "self";
    const char* target = "/self";
    VaFsSymlinkDescriptor_t symlinkDesc = {0};
    symlinkDesc.Base.Type = VA_FS_DESCRIPTOR_TYPE_SYMLINK;
    symlinkDesc.NameLength = strlen(name) + 1;
    symlinkDesc.TargetLength = strlen(target) + 1;
    symlinkDesc.Base.Length = sizeof(VaFsSymlinkDescriptor_t) + symlinkDesc.NameLength + symlinkDesc.TargetLength;
    fwrite(&symlinkDesc, sizeof(symlinkDesc), 1, fp);
    fwrite(name, symlinkDesc.NameLength, 1, fp);
    fwrite(target, symlinkDesc.TargetLength, 1, fp);
    fclose(fp);
    return 0;
}

static int test_symlink_zero_length_name(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    test_write_vafs_header(fp);
    test_write_stream_header(fp, VA_FS_DESCRIPTOR_BLOCK_SIZE);

    VaFsDirectoryHeader_t dirHeader = {1};
    fwrite(&dirHeader, sizeof(dirHeader), 1, fp);

    // Symlink with zero-length name
    VaFsSymlinkDescriptor_t symlinkDesc = {0};
    symlinkDesc.Base.Type = VA_FS_DESCRIPTOR_TYPE_SYMLINK;
    symlinkDesc.Base.Length = sizeof(VaFsSymlinkDescriptor_t);
    symlinkDesc.NameLength = 0; // INVALID!
    symlinkDesc.TargetLength = 0; // INVALID!
    fwrite(&symlinkDesc, sizeof(symlinkDesc), 1, fp);
    fclose(fp);
    return 0;
}

static int test_symlink_excessive_name_length(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    test_write_vafs_header(fp);
    test_write_stream_header(fp, VA_FS_DESCRIPTOR_BLOCK_SIZE);

    VaFsDirectoryHeader_t dirHeader = {1};
    fwrite(&dirHeader, sizeof(dirHeader), 1, fp);

    // Symlink with excessively long name (over VAFS_NAME_MAX of 256)
    VaFsSymlinkDescriptor_t symlinkDesc = {0};
    symlinkDesc.Base.Type = VA_FS_DESCRIPTOR_TYPE_SYMLINK;
    symlinkDesc.NameLength = 10000; // INVALID - way too long
    symlinkDesc.TargetLength = 10;
    symlinkDesc.Base.Length = sizeof(VaFsSymlinkDescriptor_t) + 10010;
    fwrite(&symlinkDesc, sizeof(symlinkDesc), 1, fp);
    fclose(fp);
    return 0;
}

// ============================================================================
// DIRECTORY ENTRY CASES
// ============================================================================

static int test_directory_excessive_entry_count(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    test_write_vafs_header(fp);
    test_write_stream_header(fp, VA_FS_DESCRIPTOR_BLOCK_SIZE);

    // Directory claiming to have maximum entries
    VaFsDirectoryHeader_t dirHeader = {0xFFFFFFFF};
    fwrite(&dirHeader, sizeof(dirHeader), 1, fp);
    fclose(fp);
    return 0;
}

static int test_directory_entry_count_limit(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    test_write_vafs_header(fp);
    test_write_stream_header(fp, VA_FS_DESCRIPTOR_BLOCK_SIZE);

    // Directory with 1 million + 1 entries (exceeds VAFS_DIRECTORY_MAX_ENTRIES)
    VaFsDirectoryHeader_t dirHeader = {1000001};
    fwrite(&dirHeader, sizeof(dirHeader), 1, fp);
    fclose(fp);
    return 0;
}

// ============================================================================
// INVALID HEADER CASES
// ============================================================================

static int test_invalid_magic(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = 0xDEADBEEF; // INVALID!
    header.Version = VA_FS_VERSION;
    header.DescriptorBlockOffset = sizeof(VaFsHeader_t);
    header.DataBlockOffset = sizeof(VaFsHeader_t) + sizeof(VaFsStreamHeader_t) + VA_FS_DESCRIPTOR_BLOCK_SIZE;
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);

    test_write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

static int test_invalid_version(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = 0x00020000; // Future version
    header.DescriptorBlockOffset = sizeof(VaFsHeader_t);
    header.DataBlockOffset = sizeof(VaFsHeader_t) + sizeof(VaFsStreamHeader_t) + VA_FS_DESCRIPTOR_BLOCK_SIZE;
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);

    test_write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

static int test_invalid_root_block_index(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.DescriptorBlockOffset = sizeof(VaFsHeader_t);
    header.DataBlockOffset = sizeof(VaFsHeader_t) + sizeof(VaFsStreamHeader_t) + VA_FS_DESCRIPTOR_BLOCK_SIZE;
    header.RootDescriptor.Index = VA_FS_INVALID_BLOCK; // INVALID!
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);

    test_write_minimal_descriptor_stream(fp);
    fclose(fp);
    return 0;
}

// ============================================================================
// Test Case Registry
// ============================================================================

static TestCase g_test_cases[] = {
    // Truncated cases
    {"truncated_header_partial", "Header truncated mid-structure", test_truncated_header_partial},
    {"truncated_header_magic_only", "Only magic number present", test_truncated_header_magic_only},
    {"truncated_after_header", "File ends after VaFS header", test_truncated_after_header},
    {"truncated_descriptor_stream", "Descriptor stream truncated", test_truncated_descriptor_stream},
    {"truncated_in_descriptor", "File truncated mid-descriptor", test_truncated_in_descriptor},

    // Invalid offset cases
    {"invalid_descriptor_offset_zero", "Descriptor offset is zero", test_invalid_descriptor_offset_zero},
    {"invalid_descriptor_offset_before_header", "Descriptor offset before header end", test_invalid_descriptor_offset_before_header},
    {"invalid_data_offset_before_descriptor", "Data offset equals descriptor offset", test_invalid_data_offset_before_descriptor},
    {"invalid_offset_overflow", "Offsets cause arithmetic overflow", test_invalid_offset_overflow},
    {"invalid_root_descriptor_offset_huge", "Root descriptor offset exceeds bounds", test_invalid_root_descriptor_offset_huge},

    // Invalid descriptor length cases
    {"descriptor_length_too_small", "Descriptor length smaller than base", test_descriptor_length_too_small},
    {"descriptor_length_zero", "Descriptor length is zero", test_descriptor_length_zero},
    {"descriptor_length_exceeds_block", "Descriptor length exceeds block size", test_descriptor_length_exceeds_block},
    {"descriptor_length_mismatched_symlink", "Symlink length fields mismatch", test_descriptor_length_mismatched_symlink},

    // Invalid feature record cases
    {"feature_count_exceeds_max", "Feature count exceeds maximum", test_feature_count_exceeds_max},
    {"feature_count_max", "Feature count at uint16_t maximum", test_feature_count_max},
    {"reserved_field_nonzero", "Reserved field is non-zero", test_reserved_field_nonzero},

    // Symlink loop cases
    {"symlink_self_reference", "Symlink points to itself", test_symlink_self_reference},
    {"symlink_zero_length_name", "Symlink with zero-length name", test_symlink_zero_length_name},
    {"symlink_excessive_name_length", "Symlink with excessive name length", test_symlink_excessive_name_length},

    // Directory cases
    {"directory_excessive_entry_count", "Directory with uint32_t max entries", test_directory_excessive_entry_count},
    {"directory_entry_count_limit", "Directory exceeds entry limit", test_directory_entry_count_limit},

    // Header cases
    {"invalid_magic", "Invalid magic number", test_invalid_magic},
    {"invalid_version", "Unsupported version", test_invalid_version},
    {"invalid_root_block_index", "Invalid root block index", test_invalid_root_block_index},
};

// ============================================================================
// Main Program
// ============================================================================

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <output_directory>\n", argv[0]);
        return 1;
    }

    const char* output_dir = argv[1];
    int num_tests = sizeof(g_test_cases) / sizeof(g_test_cases[0]);
    int failed = 0;
    int created = 0;

    printf("Comprehensive VaFS Malformed Image Regression Suite\n");
    printf("====================================================\n");
    printf("Generating %d test images in: %s/\n\n", num_tests, output_dir);

    for (int i = 0; i < num_tests; i++) {
        char filename[512];
        snprintf(filename, sizeof(filename), "%s/regression_%s.vafs", output_dir, g_test_cases[i].name);

        printf("[%3d/%3d] %-40s ... ", i + 1, num_tests, g_test_cases[i].name);
        fflush(stdout);

        if (g_test_cases[i].generator(filename) != 0) {
            printf("FAILED (generation error)\n");
            failed++;
            continue;
        }

        // Verify the image is rejected by the parser
        struct VaFs* vafs = NULL;
        int result = vafs_open_file(filename, &vafs);

        if (result == 0) {
            printf("FAILED (accepted when should reject)\n");
            vafs_close(vafs);
            failed++;
        } else {
            printf("OK\n");
            created++;
        }
    }

    printf("\n====================================================\n");
    printf("Summary:\n");
    printf("  Created:  %d test images\n", created);
    printf("  Failed:   %d test images\n", failed);
    printf("  Total:    %d test cases\n", num_tests);
    printf("====================================================\n");

    return failed > 0 ? 1 : 0;
}
