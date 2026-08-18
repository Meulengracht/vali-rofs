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
#include <stdlib.h>
#include <string.h>
#include "core/crc.h"

// VaFS format structures (from private.h)
#define VA_FS_MAGIC       0x3144524D
#define VA_FS_VERSION     0x00030000
#define VA_FS_INVALID_BLOCK  0xFFFF
#define VA_FS_INVALID_OFFSET 0xFFFFFFFF
#define VA_FS_DESCRIPTOR_BLOCK_SIZE (8 * 1024)
#define VA_FS_DATA_DEFAULT_BLOCKSIZE (128 * 1024)
#define VA_FS_MAX_FEATURES 16
#define BLOCK_FLAG_STORED 0x0001

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
    uint32_t BlockSize;
    uint32_t DataOffset;
    uint32_t DataLength;
    uint32_t IndexOffset;
    uint32_t IndexCount;
    uint32_t Reserved;
} VaFsStreamLayout_t;

typedef struct {
    uint32_t            Magic;
    uint32_t            Version;
    uint32_t            Architecture;
    uint16_t            FeatureCount;
    uint16_t            Reserved;
    uint32_t            Attributes;
    VaFsStreamLayout_t  DescriptorStream;
    VaFsStreamLayout_t  DataStream;
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
    uint32_t LengthOnDisk;
    uint32_t Offset;
    uint32_t Crc;
    uint16_t Flags;
    uint16_t Reserved;
} BlockHeader_t;
#pragma pack(pop)

static inline void test_initialize_stream_layouts(VaFsHeader_t* header) {
    uint32_t descriptorDataOffset = sizeof(VaFsHeader_t);
    uint32_t descriptorIndexOffset = descriptorDataOffset + VA_FS_DESCRIPTOR_BLOCK_SIZE;
    uint32_t dataDataOffset = descriptorIndexOffset + sizeof(BlockHeader_t);

    header->DescriptorStream.BlockSize = VA_FS_DESCRIPTOR_BLOCK_SIZE;
    header->DescriptorStream.DataOffset = descriptorDataOffset;
    header->DescriptorStream.DataLength = VA_FS_DESCRIPTOR_BLOCK_SIZE;
    header->DescriptorStream.IndexOffset = descriptorIndexOffset;
    header->DescriptorStream.IndexCount = 1;

    header->DataStream.BlockSize = VA_FS_DATA_DEFAULT_BLOCKSIZE;
    header->DataStream.DataOffset = dataDataOffset;
    header->DataStream.DataLength = 0;
    header->DataStream.IndexOffset = dataDataOffset;
    header->DataStream.IndexCount = 0;
}

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
    test_initialize_stream_layouts(&header);
    header.RootDescriptor.Index = 0;
    header.RootDescriptor.Offset = 0;
    fwrite(&header, sizeof(header), 1, fp);
}

/**
 * Compatibility shim for old tests. Streams no longer carry an on-disk header;
 * the outer VaFS header owns stream layout metadata.
 */
static inline void test_write_stream_header(FILE* fp, uint32_t blockSize) {
    (void)fp;
    (void)blockSize;
}

static inline uint32_t test_descriptor_crc(const uint8_t* buffer, size_t length) {
    crc_init();
    return crc_calculate(CRC_BEGIN, (uint8_t*)buffer, length);
}

static inline int test_finish_descriptor_stream(FILE* fp) {
    long logicalLength;
    uint8_t* buffer;
    BlockHeader_t blockHeader = {0};

    logicalLength = ftell(fp) - (long)sizeof(VaFsHeader_t);
    if (logicalLength < 0 || logicalLength > VA_FS_DESCRIPTOR_BLOCK_SIZE) {
        return -1;
    }

    buffer = malloc((size_t)logicalLength);
    if (buffer == NULL && logicalLength != 0) {
        return -1;
    }

    if (fseek(fp, (long)sizeof(VaFsHeader_t), SEEK_SET) != 0) {
        free(buffer);
        return -1;
    }
    if (logicalLength != 0 && fread(buffer, (size_t)logicalLength, 1, fp) != 1) {
        free(buffer);
        return -1;
    }

    blockHeader.LengthOnDisk = (uint32_t)logicalLength;
    blockHeader.Offset = 0;
    blockHeader.Crc = test_descriptor_crc(buffer, (size_t)logicalLength);
    blockHeader.Flags = BLOCK_FLAG_STORED;
    free(buffer);

    if (fseek(fp, (long)(sizeof(VaFsHeader_t) + VA_FS_DESCRIPTOR_BLOCK_SIZE), SEEK_SET) != 0) {
        return -1;
    }
    return fwrite(&blockHeader, sizeof(blockHeader), 1, fp) == 1 ? 0 : -1;
}

/**
 * Write a minimal descriptor stream with empty directory
 */
static inline void test_write_minimal_descriptor_stream(FILE* fp) {
    test_write_stream_header(fp, VA_FS_DESCRIPTOR_BLOCK_SIZE);
    VaFsDirectoryHeader_t dirHeader = {0};
    fwrite(&dirHeader, sizeof(dirHeader), 1, fp);
    test_finish_descriptor_stream(fp);
}

#endif // __TEST_COMMON_H__
