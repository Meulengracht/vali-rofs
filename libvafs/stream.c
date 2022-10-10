/**
 * Copyright 2022, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Vali Initrd Filesystem
 * - Contains the implementation of the Vali Initrd Filesystem.
 *   This filesystem is used to store the initrd of the kernel.
 */

#include "cache/blockcache.h"
#include "crc.h"
#include <errno.h>
#include "private.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define STREAM_MAGIC       0x314D5356 // VSM1

#define STREAM_TYPE_FILE   0
#define STREAM_TYPE_MEMORY 1

#define STREAM_CACHE_SIZE  32

VAFS_ONDISK_STRUCT(BlockHeader, {
    uint32_t LengthOnDisk;
    uint32_t Offset;
    uint32_t Crc;
    uint16_t Flags;
    uint16_t Reserved;
});

struct VaFsStreamBlockHeaders {
    uint32_t            Count;
    uint32_t            Capacity; 
    struct BlockHeader* Headers;
};

VAFS_ONDISK_STRUCT(VaFsStreamHeader, {
    uint32_t Magic;
    uint32_t BlockSize;
    uint32_t BlockHeadersOffset;
    uint32_t BlockHeadersCount;
});

struct VaFsStream {
    struct VaFsStreamHeader       Header;
    struct VaFsStreamDevice*      Device;
    long                          DeviceOffset;
    VaFsFilterEncodeFunc          Encode;
    VaFsFilterDecodeFunc          Decode;
    struct VaFsBlockCache*        BlockCache;
    struct VaFsStreamBlockHeaders BlockHeaders;

    // The block buffer is used for staging data before
    // we flush it to the data stream. The staging buffer
    // is always the size of the block size.
    char*       BlockBuffer;
    vafsblock_t BlockBufferIndex;
    uint32_t    BlockBufferOffset;
};

static int __new_stream(
    struct VaFsStreamDevice* device,
    long                     deviceOffset,
    struct VaFsStream**      streamOut)
{
    struct VaFsStream* stream;

    VAFS_DEBUG("__new_stream(offset=%lu)\n", deviceOffset);
    
    stream = (struct VaFsStream*)malloc(sizeof(struct VaFsStream));
    if (!stream) {
        errno = ENOMEM;
        return -1;
    }

    memset(stream, 0, sizeof(struct VaFsStream));

    stream->Device       = device;
    stream->DeviceOffset = deviceOffset;
    
    *streamOut = stream;
    return 0;
}

static int __allocate_blockbuffer(
    struct VaFsStream* stream)
{
    VAFS_DEBUG("__allocate_blockbuffer()\n");
    
    stream->BlockBuffer = malloc(stream->Header.BlockSize);
    if (!stream->BlockBuffer) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

int vafs_stream_create(
    struct VaFsStreamDevice* device,
    long                     deviceOffset,
    uint32_t                 blockSize,
    struct VaFsStream**      streamOut)
{
    struct VaFsStream* stream;
    int                status;
    size_t             written;

    if (device == NULL || streamOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = __new_stream(device, deviceOffset, &stream);
    if (status != 0) {
        return -1;
    }

    // initialize the stream header to initial values
    stream->Header.Magic     = STREAM_MAGIC;
    stream->Header.BlockSize = blockSize;

    // write the initial stream header
    status = vafs_streamdevice_write(device, &stream->Header, sizeof(struct VaFsStreamHeader), &written);
    if (status != 0) {
        VAFS_DEBUG("vafs_stream_create: failed to write stream header\n");
        vafs_stream_close(stream);
        return -1;
    }

    // allocate the block buffer
    status = __allocate_blockbuffer(stream);
    if (status != 0) {
        VAFS_DEBUG("vafs_stream_create: failed to allocate block buffer\n");
        vafs_stream_close(stream);
        return -1;
    }

    *streamOut = stream;
    return 0;
}

static long  __get_header_offset(
    struct VaFsStream* stream)
{
    return stream->DeviceOffset;
}

static long __get_data_offset(
    struct VaFsStream* stream)
{
    return stream->DeviceOffset + sizeof(struct VaFsStreamHeader);
}

static long __get_block_headers_offset(
    struct VaFsStream* stream)
{
    return stream->DeviceOffset + stream->Header.BlockHeadersOffset;
}

static struct BlockHeader* __get_block_header(
    struct VaFsStream* stream,
    vafsblock_t        block)
{
    if (block >= stream->BlockHeaders.Count) {
        return NULL;
    }
    return &stream->BlockHeaders.Headers[block];
}

static int __verify_header(
    struct VaFsStreamHeader* header)
{
    if (header->Magic != STREAM_MAGIC) {
        VAFS_ERROR("__verify_header: invalid stream magic\n");
        return -1;
    }

    if (header->BlockSize < VA_FS_DATA_MIN_BLOCKSIZE || header->BlockSize > VA_FS_DATA_MAX_BLOCKSIZE) {
        VAFS_ERROR("__verify_header: invalid block size: %u\n", header->BlockSize);
        return -1;
    }

    VAFS_DEBUG("__verify_header: block size: %u\n", header->BlockSize);
    VAFS_DEBUG("__verify_header: block headers offset: %u\n", header->BlockHeadersOffset);
    VAFS_DEBUG("__verify_header: block headers count: %u\n", header->BlockHeadersCount);

    return 0;
}

static int __load_block_headers(
    struct VaFsStream* stream)
{
    int    status;
    size_t read;

    VAFS_DEBUG("__load_block_headers()\n");

    // allocate the block headers
    stream->BlockHeaders.Count    = stream->Header.BlockHeadersCount;
    stream->BlockHeaders.Capacity = stream->Header.BlockHeadersCount;
    stream->BlockHeaders.Headers  = (struct BlockHeader*)malloc(sizeof(struct BlockHeader) * stream->Header.BlockHeadersCount);
    if (!stream->BlockHeaders.Headers) {
        errno = ENOMEM;
        return -1;
    }

    // seek to block headers
    VAFS_DEBUG("__load_block_headers: seeking to block headers %lu\n", __get_block_headers_offset(stream));
    vafs_streamdevice_seek(stream->Device, __get_block_headers_offset(stream), SEEK_SET);

    // read the block headers
    status = vafs_streamdevice_read(
        stream->Device, stream->BlockHeaders.Headers,
        sizeof(struct BlockHeader) * stream->BlockHeaders.Count,
        &read
    );
    if (status != 0) {
        VAFS_ERROR("__load_block_headers: failed to read block headers: %i\n", status);
        return status;
    }
    return 0;
}

static int __load_metadata(
    struct VaFsStream* stream)
{
    size_t read;
    int    status;

    VAFS_DEBUG("__load_metadata()\n");

    status = vafs_streamdevice_read(
        stream->Device,
        &stream->Header,
        sizeof(struct VaFsStreamHeader),
        &read
    );
    if (status != 0) {
        return -1;
    }

    status = __verify_header(&stream->Header);
    if (status != 0) {
        return -1;
    }
    return __load_block_headers(stream);
}

int vafs_stream_open(
    struct VaFsStreamDevice* device,
    long                     deviceOffset,
    struct VaFsStream**      streamOut)
{
    struct VaFsStream* stream;
    int                status;
    VAFS_DEBUG("vafs_stream_open(offset=%lu)\n", deviceOffset);

    if (device == NULL || streamOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = __new_stream(device, deviceOffset, &stream);
    if (status != 0) {
        return -1;
    }

    status = __load_metadata(stream);
    if (status != 0) {
        VAFS_ERROR("vafs_stream_open: failed to load metadata\n");
        vafs_stream_close(stream);
        return -1;
    }

    // create the block cache
    status = vafs_cache_create(STREAM_CACHE_SIZE, &stream->BlockCache);
    if (status != 0) {
        VAFS_ERROR("vafs_stream_open: failed to create block cache\n");
        vafs_stream_close(stream);
        return -1;
    }

    // allocate the block buffer
    status = __allocate_blockbuffer(stream);
    if (status != 0) {
        VAFS_ERROR("vafs_stream_open: failed to allocate block buffer\n");
        vafs_stream_close(stream);
        return -1;
    }

    *streamOut = stream;
    return 0;
}

int vafs_stream_set_filter(
    struct VaFsStream*   stream,
    VaFsFilterEncodeFunc encode,
    VaFsFilterDecodeFunc decode)
{
    if (stream == NULL) {
        errno = EINVAL;
        return -1;
    }

    stream->Encode = encode;
    stream->Decode = decode;
    return 0;
}

int vafs_stream_position(
    struct VaFsStream* stream, 
    vafsblock_t*       blockOut,
    uint32_t*          offsetOut)
{
    if (stream == NULL || blockOut == NULL || offsetOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    *blockOut  = stream->BlockBufferIndex;
    *offsetOut = stream->BlockBufferOffset;
    return 0;
}

static uint32_t __get_blockbuffer_crc(
    struct VaFsStream* stream,
    size_t             length)
{
    return crc_calculate(
        CRC_BEGIN, 
        (uint8_t*)stream->BlockBuffer, 
        length
    );
}

static int __load_blockbuffer(
    struct VaFsStream* stream,
    vafsblock_t        blockIndex)
{
    struct BlockHeader* blockHeader;
    void*               blockData;
    size_t              blockSize;
    size_t              read;
    uint32_t            crc;
    int                 status;
    long                position;
    VAFS_DEBUG("__load_blockbuffer(block=%u)\n", blockIndex);

    // Always check the block cache first
    status = vafs_cache_get(stream->BlockCache, blockIndex, &blockData, &blockSize);
    if (status == 0) {
        // We have the block in the cache, so we can just copy the data
        // to the block buffer and return.
        memcpy(stream->BlockBuffer, blockData, blockSize);
        return 0;
    }

    blockHeader = __get_block_header(stream, blockIndex);
    if (!blockHeader) {
        VAFS_ERROR("__load_blockbuffer: invalid block index: %u\n", blockIndex);
        return -1;
    }

    VAFS_DEBUG("__load_blockbuffer: block offset: %u\n", blockHeader->Offset);
    VAFS_DEBUG("__load_blockbuffer: block size: %u\n", blockHeader->LengthOnDisk);

    blockSize   = blockHeader->LengthOnDisk;
    blockData   = malloc(blockSize);
    if (!blockData) {
        errno = ENOMEM;
        return -1;
    }

    position = vafs_streamdevice_seek(stream->Device, stream->DeviceOffset + blockHeader->Offset, SEEK_SET);
    if (position == -1) {
        VAFS_ERROR("__load_blockbuffer: failed to seek to block: %i\n", blockIndex);
        free(blockData);
        return -1;
    }

    status = vafs_streamdevice_read(stream->Device, blockData, blockSize, &read);
    if (status) {
        return status;
    }

    // Handle data filters
    if (stream->Decode) {
        uint32_t blockBufferSize = stream->Header.BlockSize;

        VAFS_DEBUG("__load_blockbuffer decoding buffer of size %zu\n", blockSize);
        status = stream->Decode(blockData, (uint32_t)blockSize, stream->BlockBuffer, &blockBufferSize);
        if (status) {
            VAFS_ERROR("__load_blockbuffer: failed to decode block, %i\n", errno);
            free(blockData);
            return status;
        }
        VAFS_DEBUG("__load_blockbuffer decoded buffer size %u\n", blockBufferSize);

        // TODO we should keep a current length of block
        // verify the length of the block is correct, the actual size
        // of the decoded data is now in blockSize
        blockSize = blockBufferSize;
    }
    else {
        memcpy(stream->BlockBuffer, blockData, blockSize);
    }
    free(blockData);

    crc = __get_blockbuffer_crc(stream, blockSize);
    if (crc != blockHeader->Crc) {
        VAFS_WARN("__load_blockbuffer: CRC mismatch: %u != %u\n", crc, blockHeader->Crc);
        errno = EIO;
        return -1;
    }

    // Cache the block
    status = vafs_cache_set(stream->BlockCache, blockIndex, stream->BlockBuffer, blockSize);
    if (status) {
        VAFS_WARN("__load_blockbuffer: failed to cache block %u\n", blockIndex);
    }
    return 0;
}

int vafs_stream_seek(
    struct VaFsStream* stream, 
    vafsblock_t        blockIndex,
    uint32_t           blockOffset)
{
    struct BlockHeader* blockHeader;
    int                 status;
    vafsblock_t         targetBlock  = blockIndex;
    uint32_t            targetOffset = blockOffset;
    vafsblock_t         i            = blockIndex;
    VAFS_DEBUG("vafs_stream_seek(blockIndex=%u, blockOffset=%u)\n",
        blockIndex, blockOffset);

    if (stream == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    // seek to start of stream
    while (1) {
        blockHeader = __get_block_header(stream, i);
        if (!blockHeader) {
            errno = EINVAL;
            return -1;
        }

        // have we reached the target block, and does it contain our index?
        if (i == targetBlock) {
            // is the offset inside the current block?
            if (targetOffset < stream->Header.BlockSize) {
                break; // yep, we are done here
            }

            // nope, reduce offset, switch to next block
            targetOffset -= stream->Header.BlockSize;
            targetBlock++;
        }
        i++;
    }

    status = __load_blockbuffer(stream, targetBlock);
    if (status) {
        VAFS_ERROR("vafs_stream_seek: load blockbuffer failed: %i\n", status);
        return status;
    }

    stream->BlockBufferIndex  = targetBlock;
    stream->BlockBufferOffset = targetOffset;
    return 0;
}

static int __add_block_header(
    struct VaFsStream* stream,
    uint32_t           blockLength)
{
    long     offset;
    uint32_t crc;

    offset = vafs_streamdevice_seek(stream->Device, 0, SEEK_CUR);

    VAFS_DEBUG("__add_block_header: adding block mapping %u => %lu\n",
        stream->BlockBufferIndex, offset);
    VAFS_DEBUG("__add_block_header: block length %u\n", blockLength);

    // perform the CRC on the uncompressed data
    crc = __get_blockbuffer_crc(stream, stream->BlockBufferOffset);

    if (stream->BlockHeaders.Count == stream->BlockHeaders.Capacity) {
        struct BlockHeader* newHeaders;
        uint32_t            newCapacity;

        newCapacity = stream->BlockHeaders.Capacity * 2;
        if (newCapacity == 0) {
            newCapacity = 8;
        }

        newHeaders = realloc(stream->BlockHeaders.Headers, newCapacity * sizeof(struct BlockHeader));
        if (!newHeaders) {
            errno = ENOMEM;
            return -1;
        }
        memset(newHeaders, 0, (newCapacity - stream->BlockHeaders.Capacity) * sizeof(struct BlockHeader));

        stream->BlockHeaders.Headers  = newHeaders;
        stream->BlockHeaders.Capacity = newCapacity;
    }

    stream->BlockHeaders.Headers[stream->BlockHeaders.Count].LengthOnDisk = blockLength;
    stream->BlockHeaders.Headers[stream->BlockHeaders.Count].Offset       = (uint32_t)offset;
    stream->BlockHeaders.Headers[stream->BlockHeaders.Count].Crc          = crc;
    stream->BlockHeaders.Headers[stream->BlockHeaders.Count].Flags        = 0;
    stream->BlockHeaders.Count++;
    return 0;
}

static int __flush_block(
    struct VaFsStream* stream)
{
    void*    compressedData = stream->BlockBuffer;
    uint32_t compressedSize = stream->BlockBufferOffset;
    size_t   written;
    int      status;
    VAFS_DEBUG("__flush_block(blockLength=%u)\n", stream->BlockBufferOffset);

    if (!stream->BlockBufferOffset) {
        // empty block, ignore it
        return 0;
    }
    
    // Handle compressions
    if (stream->Encode) {
        status = stream->Encode(stream->BlockBuffer, stream->BlockBufferOffset, &compressedData, &compressedSize);
        if (status) {
            return status;
        }
        VAFS_DEBUG("__flush_block compressed buffer size %u\n", compressedSize);
    }

    // add index mapping
    status = __add_block_header(stream, compressedSize);
    if (status) {
        VAFS_ERROR("__flush_block: failed to add block header\n");
        return status;
    }

    status = vafs_streamdevice_write(stream->Device, compressedData, compressedSize, &written);
    if (status) {
        VAFS_ERROR("__flush_block: failed to write block data\n");
        return status;
    }

    // In the case of a compressed stream, we need to free the compressed data
    if (stream->Encode) {
        free(compressedData);
    }

    stream->BlockBufferIndex++;
    stream->BlockBufferOffset = 0;
    return status;
}

int vafs_stream_write(
    struct VaFsStream* stream,
    const void*        buffer,
    size_t             size)
{
    uint8_t* data = (uint8_t*)buffer;
    size_t   bytesToWrite = size;
    VAFS_DEBUG("vafs_stream_write(size=%u)\n", size);

    if (stream == NULL || buffer == NULL || size == 0) {
        errno = EINVAL;
        return -1;
    }

    // write the data to stream, taking care of block boundaries
    while (bytesToWrite) {
        size_t byteCount;
        size_t bytesLeftInBlock;

        bytesLeftInBlock = stream->Header.BlockSize - (stream->BlockBufferOffset % stream->Header.BlockSize);
        byteCount        = MIN(bytesToWrite, bytesLeftInBlock);

        memcpy(stream->BlockBuffer + stream->BlockBufferOffset, data, byteCount);
        
        stream->BlockBufferOffset += (uint32_t)byteCount;
        data                      += byteCount;
        bytesToWrite              -= byteCount;

        if (stream->BlockBufferOffset == stream->Header.BlockSize) {
            if (__flush_block(stream)) {
                VAFS_ERROR("vafs_stream_write: failed to flush block\n");
                return -1;
            }
        }
    }

    return 0;
}

int vafs_stream_read(
    struct VaFsStream* stream,
    void*              buffer,
    size_t             size)
{
    uint8_t* data        = (uint8_t*)buffer;
    size_t   bytesToRead = size;
    size_t   bytesLeftInBlock;
    VAFS_DEBUG("vafs_stream_read(size=%u)\n", size);

    if (stream == NULL || buffer == NULL || size == 0) {
        errno = EINVAL;
        return -1;
    }

    // read the data from stream, taking care of block boundaries
    while (bytesToRead) {
        size_t byteCount;

        bytesLeftInBlock = stream->Header.BlockSize - stream->BlockBufferOffset;
        byteCount = MIN(bytesToRead, bytesLeftInBlock);

        VAFS_DEBUG("vafs_stream_read: reading %u bytes from block %u, offset %u\n",
            byteCount, stream->BlockBufferIndex, stream->BlockBufferOffset);
        memcpy(data, stream->BlockBuffer + stream->BlockBufferOffset, byteCount);
        
        stream->BlockBufferOffset += (uint32_t)byteCount;
        data                      += byteCount;
        bytesToRead               -= byteCount;

        if (stream->BlockBufferOffset == stream->Header.BlockSize) {
            VAFS_DEBUG("vafs_stream_read: loading block %u\n", stream->BlockBufferIndex);
            if (__load_blockbuffer(stream, stream->BlockBufferIndex + 1)) {
                VAFS_ERROR("vafs_stream_read: failed to load block\n");
                return -1;
            }

            stream->BlockBufferIndex++;
            stream->BlockBufferOffset = 0;
        }
    }

    return 0;
}

static int __write_block_headers(
    struct VaFsStream* stream)
{
    size_t written;
    int    status;
    long   offset;
    VAFS_DEBUG("__write_index_mapping()\n");

    // get current offset
    offset = vafs_streamdevice_seek(stream->Device, 0, SEEK_CUR);
    status = vafs_streamdevice_write(
        stream->Device,
        stream->BlockHeaders.Headers,
        stream->BlockHeaders.Count * sizeof(struct BlockHeader),
        &written
    );
    if (status) {
        VAFS_ERROR("__write_index_mapping: failed to write index mapping\n");
        return status;
    }

    VAFS_DEBUG("__write_index_mapping: written %u bytes\n", written);
    VAFS_DEBUG("__write_index_mapping: BlockHeadersOffset %ld\n", offset - stream->DeviceOffset);
    VAFS_DEBUG("__write_index_mapping: BlockHeadersCount %i\n", stream->BlockHeaders.Count);

    // update the header
    stream->Header.BlockHeadersOffset = offset - stream->DeviceOffset;
    stream->Header.BlockHeadersCount  = stream->BlockHeaders.Count;
    return 0;
}

static int __update_stream_header(
    struct VaFsStream* stream)
{
    size_t written;
    int    status;
    long   original;
    long   position;
    VAFS_DEBUG("__update_stream_header()\n");

    original = vafs_streamdevice_seek(stream->Device, 0, SEEK_CUR);
    position = (int)vafs_streamdevice_seek(stream->Device, stream->DeviceOffset, SEEK_SET);
    if (position == -1) {
        VAFS_ERROR("__update_stream_header: failed to seek to stream header\n");
        return -1;
    }

    status = vafs_streamdevice_write(
        stream->Device,
        &stream->Header,
        sizeof(struct VaFsStreamHeader),
        &written
    );
    if (status) {
        VAFS_ERROR("__update_stream_header: failed to write stream header\n");
        return status;
    }

    VAFS_DEBUG("__update_stream_header: written %u bytes at %lu\n", written, position);

    vafs_streamdevice_seek(stream->Device, original, SEEK_SET);
    return 0;
}

int vafs_stream_finish(
    struct VaFsStream* stream)
{
    int status;

    VAFS_DEBUG("vafs_stream_finish()\n");
    if (!stream) {
        errno = EINVAL;
        return -1;
    }

    status = __flush_block(stream);
    if (status) {
        VAFS_ERROR("vafs_stream_finish: failed to flush block\n");
        return status;
    }

    status = __write_block_headers(stream);
    if (status) {
        VAFS_ERROR("vafs_stream_close: failed to write block headers\n");
        return status;
    }

    status = __update_stream_header(stream);
    if (status) {
        VAFS_ERROR("vafs_stream_close: failed to update stream header\n");
        return status;
    }
    return 0;
}

int vafs_stream_close(
    struct VaFsStream* stream)
{
    VAFS_DEBUG("vafs_stream_close()\n");
    if (!stream) {
        errno = EINVAL;
        return -1;
    }

    vafs_cache_destroy(stream->BlockCache);
    free(stream->BlockHeaders.Headers);
    free(stream->BlockBuffer);
    free(stream);
    return 0;
}

int vafs_stream_lock(
    struct VaFsStream* stream)
{
    if (!stream) {
        errno = EINVAL;
        return -1;
    }

    return vafs_streamdevice_lock(stream->Device);
}

int vafs_stream_unlock(
    struct VaFsStream* stream)
{
    if (!stream) {
        errno = EINVAL;
        return -1;
    }

    return vafs_streamdevice_unlock(stream->Device);
}
