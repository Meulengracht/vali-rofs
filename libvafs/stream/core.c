/**
 * Copyright, Philip Meulengracht
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
 * Vali Container Filesystem
 * - Contains the implementation of the Vali Container Filesystem.
 *   This filesystem is used to store the initrd of the kernel.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <vafs/platform.h>
#include <vafs/codec.h>

#include "../cache/blockcache.h"
#include "../core/crc.h"
#include "../core/core.h"
#include "device.h"
#include "stream.h"

#define STREAM_TYPE_FILE   0
#define STREAM_TYPE_MEMORY 1

#define STREAM_CACHE_SIZE  32

// Stored blocks keep their raw bytes on disk, are emitted by __flush_block()
// when filtering would not shrink the payload, and are recognized by
// __load_blockbuffer() so the read path can skip decode work entirely.
#define BLOCK_FLAG_STORED  0x0001

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

struct VaFsStream {
    VaFsStreamLayout_t            Layout;
    struct VaFsStreamDevice*      Device;
    mtx_t                         Lock;
    struct VaFsCodec              Codec;
    struct VaFsBlockCache*        BlockCache;
    struct VaFsStreamBlockHeaders BlockHeaders;

    // Writable streams stage logical bytes here until a full block is ready
    // to be flushed to the backing device.
    char*       BlockBuffer;
    vafsblock_t BlockBufferIndex;
    uint32_t    BlockBufferOffset;
};

struct VaFsStreamReader {
    struct VaFsStream* Stream;
    char*              BlockBuffer;
    uint32_t           BlockBufferLength;
    vafsblock_t        BlockBufferIndex;
    uint32_t           BlockBufferOffset;
    int                BlockBufferValid;
};

static int __new_stream(
    struct VaFsStreamDevice* device,
    uint64_t                 dataOffset,
    struct VaFsStream**      streamOut)
{
    struct VaFsStream* stream;

    VAFS_DEBUG("__new_stream(dataOffset=%llu)\n", (unsigned long long)dataOffset);
    // Build the shared stream shell first. Read mode attaches private cursors
    // later, while write mode keeps its staging buffer directly on the stream.
    
    stream = (struct VaFsStream*)malloc(sizeof(struct VaFsStream));
    if (!stream) {
        errno = ENOMEM;
        return -1;
    }

    memset(stream, 0, sizeof(struct VaFsStream));

    stream->Device            = device;
    stream->Layout.DataOffset = (uint32_t)dataOffset;
    mtx_init(&stream->Lock, mtx_plain);
    
    *streamOut = stream;
    return 0;
}

static int __allocate_blockbuffer(
    struct VaFsStream* stream)
{
    VAFS_DEBUG("__allocate_blockbuffer()\n");
    
    stream->BlockBuffer = malloc(stream->Layout.BlockSize);
    if (!stream->BlockBuffer) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static int __allocate_reader_blockbuffer(
    struct VaFsStreamReader* reader)
{
    // Readers stage logical bytes privately so concurrent callers do not share
    // offsets or block contents.
    reader->BlockBuffer = malloc(reader->Stream->Layout.BlockSize);
    if (!reader->BlockBuffer) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

int vafs_stream_create(
    struct VaFsStreamDevice* device,
    uint32_t                 blockSize,
    struct VaFsStream**      streamOut)
{
    struct VaFsStream* stream;
    int                status;
    uint64_t           deviceSize;

    if (device == NULL || streamOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (blockSize < VA_FS_DATA_MIN_BLOCKSIZE || blockSize > VA_FS_DATA_MAX_BLOCKSIZE) {
        errno = EINVAL;
        return -1;
    }

    status = vafs_streamdevice_size(device, &deviceSize);
    if (status) {
        return status;
    }

    if (deviceSize > UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }

    // Creation records the current append position as the stream's data base.
    // We intentionally do not reserve any placeholder bytes here: the caller is
    // responsible for appending actual logical blocks and then finalizing the
    // layout once the index has been written. This keeps the stream lifetime
    // aligned with the image writer's "build then commit" flow.
    status = __new_stream(device, deviceSize, &stream);
    if (status != 0) {
        return -1;
    }

    stream->Layout.BlockSize = blockSize;

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

static long __get_block_headers_offset(
    struct VaFsStream* stream)
{
    return stream->Layout.IndexOffset;
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

static int __verify_layout(
    const VaFsStreamLayout_t* layout)
{
    if (layout == NULL) {
        errno = EINVAL;
        return -1;
    }

    // The outer image header is the source of truth for stream placement.
    // Validate every field before it can drive block-buffer allocations or
    // positioned reads.
    if (layout->BlockSize < VA_FS_DATA_MIN_BLOCKSIZE || layout->BlockSize > VA_FS_DATA_MAX_BLOCKSIZE) {
        VAFS_ERROR("__verify_layout: invalid block size: %u\n", layout->BlockSize);
        return -1;
    }

    if (layout->IndexOffset < layout->DataOffset) {
        VAFS_ERROR("__verify_layout: index offset %u precedes data offset %u\n",
            layout->IndexOffset, layout->DataOffset);
        return -1;
    }

    if (layout->Reserved != 0) {
        VAFS_ERROR("__verify_layout: reserved field must be zero\n");
        return -1;
    }

    VAFS_DEBUG("__verify_layout: block size: %u\n", layout->BlockSize);
    VAFS_DEBUG("__verify_layout: data offset: %u\n", layout->DataOffset);
    VAFS_DEBUG("__verify_layout: data length: %u\n", layout->DataLength);
    VAFS_DEBUG("__verify_layout: index offset: %u\n", layout->IndexOffset);
    VAFS_DEBUG("__verify_layout: index count: %u\n", layout->IndexCount);

    return 0;
}

static int __load_block_headers(
    struct VaFsStream* stream)
{
    int    status;
    size_t read;
    long   blockHeadersOffset;
    size_t totalHeaderSize;

    VAFS_DEBUG("__load_block_headers()\n");

    // Validate the table shape before allocating memory or seeking based on
    // untrusted metadata from disk. The stream index is treated as a trusted
    // boundary only after the header count, size, and offset relationships are
    // proven valid, otherwise reads could walk past the end of the block table.

    // Validate block headers count is reasonable
    #define MAX_BLOCK_HEADERS 1000000
    if (stream->Layout.IndexCount > MAX_BLOCK_HEADERS) {
        VAFS_ERROR("__load_block_headers: block headers count %u exceeds maximum %d\n",
            stream->Layout.IndexCount, MAX_BLOCK_HEADERS);
        errno = EINVAL;
        return -1;
    }

    // Check for integer overflow in total header size calculation
    totalHeaderSize = (size_t)stream->Layout.IndexCount * sizeof(struct BlockHeader);
    if (stream->Layout.IndexCount > 0 && totalHeaderSize / stream->Layout.IndexCount != sizeof(struct BlockHeader)) {
        VAFS_ERROR("__load_block_headers: integer overflow in header size calculation\n");
        errno = EINVAL;
        return -1;
    }

    blockHeadersOffset = __get_block_headers_offset(stream);

    if (stream->Layout.IndexCount == 0) {
        // Metadata-only streams legitimately have no payload blocks, so an
        // empty block-header table is a valid terminal state rather than an
        // I/O failure.
        stream->BlockHeaders.Count = 0;
        stream->BlockHeaders.Capacity = 0;
        stream->BlockHeaders.Headers = NULL;
        return 0;
    }

    // allocate the block headers
    stream->BlockHeaders.Count    = stream->Layout.IndexCount;
    stream->BlockHeaders.Capacity = stream->Layout.IndexCount;
    stream->BlockHeaders.Headers  = (struct BlockHeader*)malloc(totalHeaderSize);
    if (!stream->BlockHeaders.Headers) {
        errno = ENOMEM;
        return -1;
    }

    VAFS_DEBUG("__load_block_headers: reading block headers at %ld\n", blockHeadersOffset);
    status = vafs_streamdevice_read_at(
        stream->Device,
        blockHeadersOffset,
        stream->BlockHeaders.Headers,
        totalHeaderSize,
        &read
    );
    if (status != 0 || read != totalHeaderSize) {
        VAFS_ERROR("__load_block_headers: failed to read block headers: %i\n", status);
        return status;
    }
    return 0;
}

static int __load_metadata(
    struct VaFsStream* stream)
{
    int    status;

    VAFS_DEBUG("__load_metadata()\n");

    // Stream open validates the outer-owned layout and then reads the block
    // index it points to. The stream itself has no mutable on-disk header.
    status = __verify_layout(&stream->Layout);
    if (status != 0) {
        return -1;
    }
    return __load_block_headers(stream);
}

int vafs_stream_open(
    struct VaFsStreamDevice* device,
    const VaFsStreamLayout_t* layout,
    struct VaFsStream**      streamOut)
{
    struct VaFsStream* stream;
    int                status;
    VAFS_DEBUG("vafs_stream_open(dataOffset=%u)\n", layout ? layout->DataOffset : 0);

    if (device == NULL || layout == NULL || streamOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Reconstruct metadata before creating the shared block cache. Individual
    // readers own their own staged blocks and logical positions.
    status = __new_stream(device, layout->DataOffset, &stream);
    if (status != 0) {
        return -1;
    }
    stream->Layout = *layout;

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

    *streamOut = stream;
    return 0;
}

int vafs_stream_set_codec(
    struct VaFsStream* stream,
    struct VaFsCodec*  codec)
{
    if (stream == NULL || codec == NULL) {
        errno = EINVAL;
        return -1;
    }

    stream->Codec = *codec;
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

int vafs_stream_reader_open(
    struct VaFsStream*        stream,
    struct VaFsStreamReader** readerOut)
{
    struct VaFsStreamReader* reader;

    if (stream == NULL || readerOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Allocate one cursor per caller so sequential reuse stays local to that
    // handle instead of being serialized through the shared stream object.
    reader = malloc(sizeof(struct VaFsStreamReader));
    if (reader == NULL) {
        errno = ENOMEM;
        return -1;
    }

    memset(reader, 0, sizeof(struct VaFsStreamReader));
    reader->Stream = stream;

    if (__allocate_reader_blockbuffer(reader) != 0) {
        free(reader);
        return -1;
    }

    *readerOut = reader;
    return 0;
}

void vafs_stream_reader_close(
    struct VaFsStreamReader* reader)
{
    if (reader == NULL) {
        return;
    }

    // Readers only own their private staging buffer plus the cursor wrapper.
    free(reader->BlockBuffer);
    free(reader);
}

uint32_t vafs_stream_block_size(
    struct VaFsStream* stream)
{
    if (stream == NULL) {
        return 0;
    }
    return stream->Layout.BlockSize;
}

static uint32_t __get_buffer_crc(
    const void* buffer,
    size_t      length)
{
    return crc_calculate(
        CRC_BEGIN, 
        (uint8_t*)buffer,
        length
    );
}

static int __load_blockbuffer(
    struct VaFsStreamReader* reader,
    vafsblock_t        blockIndex)
{
    struct VaFsStream*  stream = reader->Stream;
    struct BlockHeader* blockHeader;
    size_t              blockSize;
    size_t              read;
    uint32_t            crc;
    int                 status;
    VAFS_DEBUG("__load_blockbuffer(block=%u)\n", blockIndex);

    // Materialize one logical block into the reader-local staging buffer.
    // The order is cache copy, device read, optional decode, CRC validation,
    // and finally best-effort cache refill.

    // Reads always prefer cached blocks because cache entries already hold the
    // final logical bytes for the block.
    status = vafs_cache_get(
        stream->BlockCache,
        blockIndex,
        reader->BlockBuffer,
        stream->Layout.BlockSize,
        &blockSize
    );
    if (status == 0) {
        // Cache reads copy into the reader buffer so callers never observe a
        // cache-owned allocation that another reader could evict underneath.
        reader->BlockBufferLength = (uint32_t)blockSize;
        reader->BlockBufferValid = 1;
        return 0;
    }

    // Cache miss falls back to the persisted block table for the physical
    // location and storage format of this logical block.
    blockHeader = __get_block_header(stream, blockIndex);
    if (!blockHeader) {
        VAFS_ERROR("__load_blockbuffer: invalid block index: %u\n", blockIndex);
        return -1;
    }

    VAFS_DEBUG("__load_blockbuffer: block offset: %u\n", blockHeader->Offset);
    VAFS_DEBUG("__load_blockbuffer: block size: %u\n", blockHeader->LengthOnDisk);

    blockSize = blockHeader->LengthOnDisk;
    void* blockData = malloc(blockSize);
    if (!blockData) {
        errno = ENOMEM;
        return -1;
    }

    status = vafs_streamdevice_read_at(stream->Device, stream->Layout.DataOffset + blockHeader->Offset, blockData, blockSize, &read);
    if (status || read != blockSize) {
        // Positioned reads must return the entire persisted block; partial
        // reads would make decode and CRC verification ambiguous.
        VAFS_ERROR("__load_blockbuffer: failed to read block: %u\n", blockIndex);
        free(blockData);
        return status;
    }

    // Only decode blocks that were actually written in filtered form. Blocks
    // marked BLOCK_FLAG_STORED were persisted raw specifically to skip decode.
    if ((blockHeader->Flags & BLOCK_FLAG_STORED) == 0 && stream->Codec.Decode) {
        size_t bytesDecoded = 0;

        VAFS_DEBUG("__load_blockbuffer decoding buffer of size %zu\n", blockSize);
        status = stream->Codec.Decode(
            blockData, blockSize,
            reader->BlockBuffer, stream->Layout.BlockSize, 
            stream->Codec.UserData, &bytesDecoded
        );
        if (status) {
            VAFS_ERROR("__load_blockbuffer: failed to decode block, %i\n", errno);
            free(blockData);
            return status;
        }
        VAFS_DEBUG("__load_blockbuffer decoded buffer size %zu\n", bytesDecoded);

        // TODO we should keep a current length of block
        // verify the length of the block is correct, the actual size
        // of the decoded data is now in blockSize
        blockSize = bytesDecoded;
    }
    else {
        if (blockHeader->LengthOnDisk > stream->Layout.BlockSize) {
            VAFS_ERROR("__load_blockbuffer: stored block size %u exceeds stream block size %u\n",
            blockHeader->LengthOnDisk, stream->Layout.BlockSize);
            free(blockData);
            errno = EINVAL;
            return -1;
        }
        // Stored blocks and unfiltered streams already contain the final bytes.
        memcpy(reader->BlockBuffer, blockData, blockSize);
    }
    free(blockData);

    // CRC is always computed over the logical block bytes, regardless of
    // whether the block was decoded or copied raw.
    crc = __get_buffer_crc(reader->BlockBuffer, blockSize);
    if (crc != blockHeader->Crc) {
        VAFS_WARN("__load_blockbuffer: CRC mismatch: %u != %u\n", crc, blockHeader->Crc);
        errno = EIO;
        return -1;
    }

    // Cache population is best-effort; a miss here only hurts reuse, not
    // correctness of the read that already succeeded.
    status = vafs_cache_set(stream->BlockCache, blockIndex, reader->BlockBuffer, blockSize);
    if (status) {
        VAFS_WARN("__load_blockbuffer: failed to cache block %u\n", blockIndex);
    }

    reader->BlockBufferLength = (uint32_t)blockSize;
    reader->BlockBufferValid = 1;
    return 0;
}

int vafs_stream_reader_seek(
    struct VaFsStreamReader* reader,
    vafsblock_t        blockIndex,
    uint32_t           blockOffset)
{
    struct VaFsStream*  stream;
    struct BlockHeader* blockHeader;
    int                 status;
    vafsblock_t         targetBlock  = blockIndex;
    uint32_t            targetOffset = blockOffset;
    vafsblock_t         i            = blockIndex;
    VAFS_DEBUG("vafs_stream_seek(blockIndex=%u, blockOffset=%u)\n",
        blockIndex, blockOffset);

    if (reader == NULL || reader->Stream == NULL) {
        errno = EINVAL;
        return -1;
    }

    stream = reader->Stream;

    // Normalize the caller's logical file offset into one concrete block plus
    // an in-block offset, then reuse or restage bytes for this reader only.

    // Resolve the caller's logical position before deciding whether the staged
    // block can satisfy it directly.
    // Validate block index against block count
    if (blockIndex >= stream->BlockHeaders.Count) {
        VAFS_ERROR("vafs_stream_seek: block index %u exceeds block count %u\n",
            blockIndex, stream->BlockHeaders.Count);
        errno = EINVAL;
        return -1;
    }

    // Fold oversized offsets across later blocks until the target offset lands
    // inside one concrete block.
    while (1) {
        blockHeader = __get_block_header(stream, i);
        if (!blockHeader) {
            errno = EINVAL;
            return -1;
        }

        // have we reached the target block, and does it contain our index?
        if (i == targetBlock) {
            // is the offset inside the current block?
            if (targetOffset < stream->Layout.BlockSize) {
                break; // yep, we are done here
            }

            // nope, reduce offset, switch to next block
            targetOffset -= stream->Layout.BlockSize;
            targetBlock++;

            // Check for overflow: if targetBlock wrapped around, we have an overflow
            if (targetBlock < i) {
                VAFS_ERROR("vafs_stream_seek: block index overflow\n");
                errno = EINVAL;
                return -1;
            }

            // Also validate targetBlock doesn't exceed block count
            if (targetBlock >= stream->BlockHeaders.Count) {
                VAFS_ERROR("vafs_stream_seek: computed block index %u exceeds block count %u\n",
                    targetBlock, stream->BlockHeaders.Count);
                errno = EINVAL;
                return -1;
            }
        }
        i++;
    }

    // Sequential reads often leave the target block staged already, so avoid
    // another load when the buffered bytes still cover the requested offset.
    if (reader->BlockBufferValid &&
        reader->BlockBufferIndex == targetBlock &&
        targetOffset < reader->BlockBufferLength) {
        reader->BlockBufferOffset = targetOffset;
        return 0;
    }

    status = __load_blockbuffer(reader, targetBlock);
    if (status) {
        VAFS_ERROR("vafs_stream_seek: load blockbuffer failed: %i\n", status);
        return status;
    }

    if (targetOffset >= reader->BlockBufferLength) {
        // The tail block can be shorter than the stream block size, so reject
        // seeks that land past the logical bytes actually materialized.
        errno = EINVAL;
        return -1;
    }

    reader->BlockBufferIndex  = targetBlock;
    reader->BlockBufferOffset = targetOffset;
    return 0;
}

static int __add_block_header(
    struct VaFsStream* stream,
    uint32_t           blockLength,
    uint16_t           blockFlags)
{
    uint64_t offset;
    uint32_t crc;

    if (vafs_streamdevice_size(stream->Device, &offset)) {
        return -1;
    }
    if (offset < stream->Layout.DataOffset || offset - stream->Layout.DataOffset > UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }

    // Record where the just-flushed logical block ended up on disk so readers
    // can map logical block numbers back to physical offsets later.
    VAFS_DEBUG("__add_block_header: adding block mapping %u => %llu\n",
        stream->BlockBufferIndex, (unsigned long long)(offset - stream->Layout.DataOffset));
    VAFS_DEBUG("__add_block_header: block length %u\n", blockLength);

    // Persist CRC over the logical bytes so validation stays identical whether
    // the payload was stored raw or filtered before writing.
    // perform the CRC on the uncompressed data
    crc = __get_buffer_crc(stream->BlockBuffer, stream->BlockBufferOffset);

    if (stream->BlockHeaders.Count == stream->BlockHeaders.Capacity) {
        struct BlockHeader* newHeaders;
        uint32_t            newCapacity;

        // Grow the in-memory table geometrically so block header appends stay
        // amortized while the write path streams forward.
        newCapacity = stream->BlockHeaders.Capacity * 2;
        if (newCapacity == 0) {
            newCapacity = 8;
        }

        newHeaders = realloc(stream->BlockHeaders.Headers, newCapacity * sizeof(struct BlockHeader));
        if (!newHeaders) {
            errno = ENOMEM;
            return -1;
        }
        memset(
            &newHeaders[stream->BlockHeaders.Capacity],
            0, 
            (newCapacity - stream->BlockHeaders.Capacity) * sizeof(struct BlockHeader)
        );

        stream->BlockHeaders.Headers  = newHeaders;
        stream->BlockHeaders.Capacity = newCapacity;
    }

    stream->BlockHeaders.Headers[stream->BlockHeaders.Count].LengthOnDisk = blockLength;
    stream->BlockHeaders.Headers[stream->BlockHeaders.Count].Offset       = (uint32_t)(offset - stream->Layout.DataOffset);
    stream->BlockHeaders.Headers[stream->BlockHeaders.Count].Crc          = crc;
    stream->BlockHeaders.Headers[stream->BlockHeaders.Count].Flags        = blockFlags;
    stream->BlockHeaders.Count++;
    return 0;
}

static int __flush_block(
    struct VaFsStream* stream)
{
    void*    compressedData = NULL;
    void*    blockData      = stream->BlockBuffer;
    uint32_t blockFlags     = 0;
    uint32_t blockLength    = stream->BlockBufferOffset;
    size_t   written;
    int      status;
    VAFS_DEBUG("__flush_block(blockLength=%u)\n", stream->BlockBufferOffset);

    // Finalize one staged block: optionally filter it, keep the filtered bytes
    // only when they are smaller, then record how the block was stored.

    if (!stream->BlockBufferOffset) {
        // empty block, ignore it
        return 0;
    }
    
    // Per-stream filtering is optional. When enabled, avoid paying future
    // decode cost unless the filtered form actually shrinks the payload.
    if (stream->Codec.Encode) {
        size_t compressedSize;

        status = stream->Codec.Encode(
            stream->BlockBuffer, stream->BlockBufferOffset, 
            &compressedData, &compressedSize,
            stream->Codec.UserData
        );
        if (status) {
            return status;
        }

        // This is a strict per-block size policy.
        // Example for an 8192-byte logical block:
        // - 4096-byte filtered output: keep the filtered bytes.
        // - 8191-byte filtered output: still keep it because it is smaller.
        // - 8192-byte or 9000-byte filtered output: discard it, store the raw
        //   bytes instead, and mark the block BLOCK_FLAG_STORED.
        if (compressedSize < stream->BlockBufferOffset) {
            // Smaller filtered output is worth keeping because it reduces the
            // on-disk bytes the read path has to fetch.
            blockData = compressedData;
            if (compressedSize > UINT32_MAX) {
                free(compressedData);
                errno = EOVERFLOW;
                return -1;
            }
            blockLength = (uint32_t)compressedSize;
            VAFS_DEBUG("__flush_block compressed buffer size %zu\n", compressedSize);
        } else {
            // Larger or equal filtered output would only force unnecessary
            // decode work, so persist the raw bytes and mark them stored.
            blockFlags |= BLOCK_FLAG_STORED;
            free(compressedData);
            compressedData = NULL;
            VAFS_DEBUG("__flush_block storing raw block of size %u\n", blockLength);
        }
    }

    // Persist the storage decision before writing so __load_blockbuffer() knows
    // whether BLOCK_FLAG_STORED should bypass runtime decode.
    // add index mapping
    status = __add_block_header(stream, blockLength, (uint16_t)blockFlags);
    if (status) {
        VAFS_ERROR("__flush_block: failed to add block header\n");
        free(compressedData);
        return status;
    }

    status = vafs_streamdevice_write(stream->Device, blockData, blockLength, &written);
    if (status) {
        VAFS_ERROR("__flush_block: failed to write block data\n");
        free(compressedData);
        return status;
    }

    free(compressedData);

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

    // Accumulate into the staging buffer until a full logical block is ready
    // to be finalized by __flush_block().
    // write the data to stream, taking care of block boundaries
    while (bytesToWrite) {
        size_t byteCount;
        size_t bytesLeftInBlock;

        bytesLeftInBlock = stream->Layout.BlockSize - (stream->BlockBufferOffset % stream->Layout.BlockSize);
        byteCount        = MIN(bytesToWrite, bytesLeftInBlock);

        memcpy(stream->BlockBuffer + stream->BlockBufferOffset, data, byteCount);
        
        stream->BlockBufferOffset += (uint32_t)byteCount;
        data                      += byteCount;
        bytesToWrite              -= byteCount;

        if (stream->BlockBufferOffset == stream->Layout.BlockSize) {
            // Flush full blocks immediately so subsequent writes start with a
            // fresh staging buffer and the block index advances in order.
            if (__flush_block(stream)) {
                VAFS_ERROR("vafs_stream_write: failed to flush block\n");
                return -1;
            }
        }
    }

    return 0;
}

int vafs_stream_reader_read(
    struct VaFsStreamReader* reader,
    void*                    buffer,
    size_t                   size,
    size_t*                  bytesRead)
{
    struct VaFsStream* stream;
    uint8_t*           data        = (uint8_t*)buffer;
    size_t             bytesToRead = size;
    size_t             bytesLeftInBlock;
    VAFS_DEBUG("vafs_stream_read(size=%u)\n", size);

    if (reader == NULL || reader->Stream == NULL || buffer == NULL || size == 0 || bytesRead == NULL) {
        errno = EINVAL;
        return -1;
    }

    stream = reader->Stream;

    // Drain the reader-local staged block first and only fetch a new block
    // when this cursor reaches the end of the bytes it already owns.

    // Drain the currently staged block first and only load the next block when
    // the caller crosses a block boundary.
    // read the data from stream, taking care of block boundaries
    while (bytesToRead) {
        size_t byteCount;

        if (!reader->BlockBufferValid) {
            // Reads must be preceded by a seek that stages the initial block.
            *bytesRead = size - bytesToRead;
            errno = ENODATA;
            return -1;
        }

        if (reader->BlockBufferOffset >= reader->BlockBufferLength) {
            if (reader->BlockBufferIndex + 1 >= stream->BlockHeaders.Count) {
                // The caller asked for bytes beyond the staged tail block and
                // the stream has no successor block to advance into.
                *bytesRead = size - bytesToRead;
                errno = ENODATA;
                return -1;
            }

            if (__load_blockbuffer(reader, reader->BlockBufferIndex + 1)) {
                // Once sequential read-ahead fails, report the bytes already
                // copied and stop rather than silently skipping corrupted data.
                VAFS_ERROR("vafs_stream_read: failed to load block\n");
                *bytesRead = size - bytesToRead;
                errno = ENODATA;
                return -1;
            }

            reader->BlockBufferIndex++;
            reader->BlockBufferOffset = 0;
        }

        bytesLeftInBlock = reader->BlockBufferLength - reader->BlockBufferOffset;
        byteCount = MIN(bytesToRead, bytesLeftInBlock);

        VAFS_DEBUG("vafs_stream_read: reading %u bytes from block %u, offset %u\n",
            byteCount, reader->BlockBufferIndex, reader->BlockBufferOffset);
        memcpy(data, reader->BlockBuffer + reader->BlockBufferOffset, byteCount);
        
        reader->BlockBufferOffset += (uint32_t)byteCount;
        data                      += byteCount;
        bytesToRead               -= byteCount;

        if (bytesToRead != 0 &&
            reader->BlockBufferOffset == reader->BlockBufferLength &&
            reader->BlockBufferIndex + 1 < stream->BlockHeaders.Count) {
            // Crossing the boundary stages the next block so sequential reads
            // continue forward without an explicit seek from the caller, but
            // only while the current request still needs more bytes.
            VAFS_DEBUG("vafs_stream_read: loading block %u\n", reader->BlockBufferIndex);
            if (__load_blockbuffer(reader, reader->BlockBufferIndex + 1)) {
                VAFS_ERROR("vafs_stream_read: failed to load block\n");
                *bytesRead = (size - bytesToRead);
                errno = ENODATA;
                return -1;
            }

            reader->BlockBufferIndex++;
            reader->BlockBufferOffset = 0;
        }
    }

    *bytesRead = (size - bytesToRead);
    return 0;
}

static int __write_block_headers(
    struct VaFsStream* stream)
{
    size_t   written;
    int      status;
    uint64_t offset;
    VAFS_DEBUG("__write_index_mapping()\n");

    // Append the accumulated block table at the device tail and record its
    // final position in the outer-owned layout.

    status = vafs_streamdevice_size(stream->Device, &offset);
    if (status) {
        return status;
    }
    if (offset < stream->Layout.DataOffset || offset > UINT32_MAX || offset - stream->Layout.DataOffset > UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }

    stream->Layout.DataLength = (uint32_t)(offset - stream->Layout.DataOffset);
    stream->Layout.IndexOffset = (uint32_t)offset;
    stream->Layout.IndexCount = stream->BlockHeaders.Count;

    if (stream->BlockHeaders.Count == 0) {
        return 0;
    }

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
    VAFS_DEBUG("__write_index_mapping: IndexOffset %u\n", stream->Layout.IndexOffset);
    VAFS_DEBUG("__write_index_mapping: BlockHeadersCount %i\n", stream->BlockHeaders.Count);
    return 0;
}

int vafs_stream_finish(
    struct VaFsStream* stream,
    VaFsStreamLayout_t* layoutOut)
{
    int status;

    VAFS_DEBUG("vafs_stream_finish()\n");
    if (!stream || !layoutOut) {
        errno = EINVAL;
        return -1;
    }

    // Finalization is append-only: flush the tail block, append the block
    // table, then return the layout the outer image header will persist.
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

    *layoutOut = stream->Layout;
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
    mtx_destroy(&stream->Lock);
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

    if (mtx_trylock(&stream->Lock) != thrd_success) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}

int vafs_stream_unlock(
    struct VaFsStream* stream)
{
    if (!stream) {
        errno = EINVAL;
        return -1;
    }

    if (mtx_unlock(&stream->Lock) != thrd_success) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}
