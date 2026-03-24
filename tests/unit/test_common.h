/**
 * Common definitions and utilities for VaFS tests
 *
 * This header provides shared structures, constants, and helper functions
 * for generating test images with various malformed conditions.
 */

#ifndef __TEST_COMMON_H__
#define __TEST_COMMON_H__

#include <stdio.h>
#include <stdint.h>

// VaFS format structures (from private.h)
#define VA_FS_MAGIC       0x3144524D
#define VA_FS_VERSION     0x00010000
#define STREAM_MAGIC      0x314D5356
#define VA_FS_INVALID_BLOCK  0xFFFF
#define VA_FS_INVALID_OFFSET 0xFFFFFFFF
#define VA_FS_DESCRIPTOR_BLOCK_SIZE (8 * 1024)
#define VA_FS_DATA_DEFAULT_BLOCKSIZE (128 * 1024)
#define VA_FS_MAX_FEATURES 16

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

/**
 * Write a valid VaFS header to file
 */
static inline void test_write_vafs_header(FILE* fp) {
    VaFsHeader_t header = {0};
    header.Magic = VA_FS_MAGIC;
    header.Version = VA_FS_VERSION;
    header.Architecture = 0;
    header.FeatureCount = 0;
    header.Reserved = 0;
    header.DescriptorBlockOffset = sizeof(VaFsHeader_t);
    header.DataBlockOffset = sizeof(VaFsHeader_t) + sizeof(VaFsStreamHeader_t) + VA_FS_DESCRIPTOR_BLOCK_SIZE;
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);
}

/**
 * Write a stream header to file
 */
static inline void test_write_stream_header(FILE* fp, uint32_t blockSize) {
    VaFsStreamHeader_t header = {0};
    header.Magic = STREAM_MAGIC;
    header.BlockSize = blockSize;
    header.BlockHeadersOffset = blockSize + sizeof(VaFsStreamHeader_t);
    header.BlockHeadersCount = 0;
    fwrite(&header, sizeof(header), 1, fp);
}

/**
 * Write a minimal descriptor stream with empty directory
 */
static inline void test_write_minimal_descriptor_stream(FILE* fp) {
    test_write_stream_header(fp, VA_FS_DESCRIPTOR_BLOCK_SIZE);
    VaFsDirectoryHeader_t dirHeader = {0};
    fwrite(&dirHeader, sizeof(dirHeader), 1, fp);
    // Pad to block size
    uint8_t pad[VA_FS_DESCRIPTOR_BLOCK_SIZE - sizeof(VaFsStreamHeader_t) - sizeof(VaFsDirectoryHeader_t)] = {0};
    fwrite(pad, sizeof(pad), 1, fp);
}

#endif // __TEST_COMMON_H__
