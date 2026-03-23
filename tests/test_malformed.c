/**
 * Test program for malformed VaFS descriptor validation
 *
 * This program creates intentionally malformed VaFS images to test
 * the bounds validation and error handling in the parser.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vafs/vafs.h>

// VaFS format structures (from private.h)
#define VA_FS_MAGIC       0x3144524D
#define VA_FS_VERSION     0x00010000
#define STREAM_MAGIC      0x314D5356
#define VA_FS_INVALID_BLOCK  0xFFFF
#define VA_FS_INVALID_OFFSET 0xFFFFFFFF
#define VA_FS_DESCRIPTOR_BLOCK_SIZE (8 * 1024)
#define VA_FS_DATA_DEFAULT_BLOCKSIZE (128 * 1024)

#define VA_FS_DESCRIPTOR_TYPE_FILE      0x01
#define VA_FS_DESCRIPTOR_TYPE_DIRECTORY 0x02
#define VA_FS_DESCRIPTOR_TYPE_SYMLINK   0x03

typedef uint32_t vafsblock_t;

#pragma pack(push, 1)
typedef struct {
    vafsblock_t Index;
    uint32_t    Offset;
} VaFsBlockPosition_t;

typedef struct {
    uint32_t            Magic;
    uint32_t            Version;
    uint32_t            Architecture;
    uint16_t            FeatureCount;
    uint16_t            Reserved;
    uint32_t            Attributes;
    uint32_t            DescriptorBlockOffset;
    uint32_t            DataBlockOffset;
    VaFsBlockPosition_t RootDescriptor;
} VaFsHeader_t;

typedef struct {
    uint16_t Type;
    uint16_t Length;
} VaFsDescriptor_t;

typedef struct {
    VaFsDescriptor_t    Base;
    VaFsBlockPosition_t Data;
    uint32_t            FileLength;
    uint32_t            Permissions;
} VaFsFileDescriptor_t;

typedef struct {
    VaFsDescriptor_t    Base;
    VaFsBlockPosition_t Descriptor;
    uint32_t            Permissions;
} VaFsDirectoryDescriptor_t;

typedef struct {
    VaFsDescriptor_t Base;
    uint16_t         NameLength;
    uint16_t         TargetLength;
} VaFsSymlinkDescriptor_t;

typedef struct {
    uint32_t Count;
} VaFsDirectoryHeader_t;

typedef struct {
    uint32_t Magic;
    uint32_t BlockSize;
    uint32_t BlockHeadersOffset;
    uint32_t BlockHeadersCount;
} VaFsStreamHeader_t;

typedef struct {
    uint32_t LengthOnDisk;
    uint32_t Offset;
    uint32_t Crc;
    uint16_t Flags;
    uint16_t Reserved;
} BlockHeader_t;
#pragma pack(pop)

static void write_vafs_header(FILE* fp) {
    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.Architecture = 0;
    header.FeatureCount = 0;
    header.DescriptorBlockOffset = sizeof(VaFsHeader_t);
    header.DataBlockOffset = sizeof(VaFsHeader_t) + sizeof(VaFsStreamHeader_t) + VA_FS_DESCRIPTOR_BLOCK_SIZE;
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);
}

static void write_stream_header(FILE* fp, uint32_t blockSize) {
    VaFsStreamHeader_t header = {0};
    header.Magic = STREAM_MAGIC;
    header.BlockSize = blockSize;
    header.BlockHeadersOffset = blockSize + sizeof(VaFsStreamHeader_t);
    header.BlockHeadersCount = 0;
    fwrite(&header, sizeof(header), 1, fp);
}

// Test 1: Descriptor with length too short
static int test_descriptor_too_short(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    write_vafs_header(fp);
    write_stream_header(fp, VA_FS_DESCRIPTOR_BLOCK_SIZE);

    // Write a directory header with 1 entry
    VaFsDirectoryHeader_t dirHeader = {1};
    fwrite(&dirHeader, sizeof(dirHeader), 1, fp);

    // Write a file descriptor with length less than sizeof(VaFsFileDescriptor_t)
    VaFsFileDescriptor_t fileDesc = {0};
    fileDesc.Base.Type = VA_FS_DESCRIPTOR_TYPE_FILE;
    fileDesc.Base.Length = sizeof(VaFsDescriptor_t) - 1; // TOO SHORT!
    fileDesc.Data.Index = VA_FS_INVALID_BLOCK;
    fileDesc.Data.Offset = VA_FS_INVALID_OFFSET;
    fileDesc.FileLength = 0;
    fileDesc.Permissions = 0644;
    fwrite(&fileDesc, sizeof(fileDesc), 1, fp);

    fclose(fp);
    return 0;
}

// Test 2: Descriptor with length too long (causes huge allocation)
static int test_descriptor_too_long(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    write_vafs_header(fp);
    write_stream_header(fp, VA_FS_DESCRIPTOR_BLOCK_SIZE);

    VaFsDirectoryHeader_t dirHeader = {1};
    fwrite(&dirHeader, sizeof(dirHeader), 1, fp);

    // Write a file descriptor with excessive length
    VaFsFileDescriptor_t fileDesc = {0};
    fileDesc.Base.Type = VA_FS_DESCRIPTOR_TYPE_FILE;
    fileDesc.Base.Length = 65535; // Maximum uint16_t - way too long!
    fileDesc.Data.Index = VA_FS_INVALID_BLOCK;
    fileDesc.Data.Offset = VA_FS_INVALID_OFFSET;
    fileDesc.FileLength = 0;
    fileDesc.Permissions = 0644;
    fwrite(&fileDesc, sizeof(fileDesc), 1, fp);

    fclose(fp);
    return 0;
}

// Test 3: Directory with excessive entry count
static int test_directory_excessive_count(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    write_vafs_header(fp);
    write_stream_header(fp, VA_FS_DESCRIPTOR_BLOCK_SIZE);

    // Write a directory header with way too many entries
    VaFsDirectoryHeader_t dirHeader = {0xFFFFFFFF}; // Maximum uint32_t!
    fwrite(&dirHeader, sizeof(dirHeader), 1, fp);

    fclose(fp);
    return 0;
}

// Test 4: Symlink with mismatched lengths
static int test_symlink_length_mismatch(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    write_vafs_header(fp);
    write_stream_header(fp, VA_FS_DESCRIPTOR_BLOCK_SIZE);

    VaFsDirectoryHeader_t dirHeader = {1};
    fwrite(&dirHeader, sizeof(dirHeader), 1, fp);

    // Write a symlink descriptor where NameLength + TargetLength != Length - base size
    VaFsSymlinkDescriptor_t symlinkDesc = {0};
    symlinkDesc.Base.Type = VA_FS_DESCRIPTOR_TYPE_SYMLINK;
    symlinkDesc.Base.Length = sizeof(VaFsSymlinkDescriptor_t) + 20; // Says 20 bytes follow
    symlinkDesc.NameLength = 5;  // But these only add up to 10!
    symlinkDesc.TargetLength = 5;
    fwrite(&symlinkDesc, sizeof(symlinkDesc), 1, fp);
    fwrite("link\0", 5, 1, fp);
    fwrite("targ\0", 5, 1, fp);

    fclose(fp);
    return 0;
}

// Test 5: Symlink with zero length fields
static int test_symlink_zero_length(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    write_vafs_header(fp);
    write_stream_header(fp, VA_FS_DESCRIPTOR_BLOCK_SIZE);

    VaFsDirectoryHeader_t dirHeader = {1};
    fwrite(&dirHeader, sizeof(dirHeader), 1, fp);

    // Write a symlink descriptor with zero-length name
    VaFsSymlinkDescriptor_t symlinkDesc = {0};
    symlinkDesc.Base.Type = VA_FS_DESCRIPTOR_TYPE_SYMLINK;
    symlinkDesc.Base.Length = sizeof(VaFsSymlinkDescriptor_t);
    symlinkDesc.NameLength = 0;  // Invalid!
    symlinkDesc.TargetLength = 0; // Invalid!
    fwrite(&symlinkDesc, sizeof(symlinkDesc), 1, fp);

    fclose(fp);
    return 0;
}

// Test 6: File descriptor with no name
static int test_file_no_name(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    write_vafs_header(fp);
    write_stream_header(fp, VA_FS_DESCRIPTOR_BLOCK_SIZE);

    VaFsDirectoryHeader_t dirHeader = {1};
    fwrite(&dirHeader, sizeof(dirHeader), 1, fp);

    // Write a file descriptor with exact base size (no name)
    VaFsFileDescriptor_t fileDesc = {0};
    fileDesc.Base.Type = VA_FS_DESCRIPTOR_TYPE_FILE;
    fileDesc.Base.Length = sizeof(VaFsFileDescriptor_t); // No room for name!
    fileDesc.Data.Index = VA_FS_INVALID_BLOCK;
    fileDesc.Data.Offset = VA_FS_INVALID_OFFSET;
    fileDesc.FileLength = 0;
    fileDesc.Permissions = 0644;
    fwrite(&fileDesc, sizeof(fileDesc), 1, fp);

    fclose(fp);
    return 0;
}

// Test 7: Symlink with excessive name/target lengths
static int test_symlink_excessive_length(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    write_vafs_header(fp);
    write_stream_header(fp, VA_FS_DESCRIPTOR_BLOCK_SIZE);

    VaFsDirectoryHeader_t dirHeader = {1};
    fwrite(&dirHeader, sizeof(dirHeader), 1, fp);

    // Write a symlink descriptor with excessively long name
    VaFsSymlinkDescriptor_t symlinkDesc = {0};
    symlinkDesc.Base.Type = VA_FS_DESCRIPTOR_TYPE_SYMLINK;
    symlinkDesc.NameLength = 10000;  // Way over VAFS_NAME_MAX (256)
    symlinkDesc.TargetLength = 10;
    symlinkDesc.Base.Length = sizeof(VaFsSymlinkDescriptor_t) + 10000 + 10;
    fwrite(&symlinkDesc, sizeof(symlinkDesc), 1, fp);

    fclose(fp);
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <output_directory>\n", argv[0]);
        return 1;
    }

    const char* outdir = argv[1];
    char filename[512];

    printf("Creating malformed VaFS test images in: %s\n", outdir);

    snprintf(filename, sizeof(filename), "%s/test_descriptor_too_short.vafs", outdir);
    printf("Creating: %s\n", filename);
    test_descriptor_too_short(filename);

    snprintf(filename, sizeof(filename), "%s/test_descriptor_too_long.vafs", outdir);
    printf("Creating: %s\n", filename);
    test_descriptor_too_long(filename);

    snprintf(filename, sizeof(filename), "%s/test_directory_excessive_count.vafs", outdir);
    printf("Creating: %s\n", filename);
    test_directory_excessive_count(filename);

    snprintf(filename, sizeof(filename), "%s/test_symlink_length_mismatch.vafs", outdir);
    printf("Creating: %s\n", filename);
    test_symlink_length_mismatch(filename);

    snprintf(filename, sizeof(filename), "%s/test_symlink_zero_length.vafs", outdir);
    printf("Creating: %s\n", filename);
    test_symlink_zero_length(filename);

    snprintf(filename, sizeof(filename), "%s/test_file_no_name.vafs", outdir);
    printf("Creating: %s\n", filename);
    test_file_no_name(filename);

    snprintf(filename, sizeof(filename), "%s/test_symlink_excessive_length.vafs", outdir);
    printf("Creating: %s\n", filename);
    test_symlink_excessive_length(filename);

    printf("\nAll test images created successfully.\n");
    return 0;
}
