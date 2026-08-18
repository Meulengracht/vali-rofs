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

#ifndef __VAFS_STREAM_STREAM_H_
#define __VAFS_STREAM_STREAM_H_

#include <stdint.h>
#include <stddef.h>

#include <vafs/codec.h>

#include "../format/format.h"

// Forward declarations
struct VaFsStream;
struct VaFsStreamReader;

/**
 * @brief Creates a writable block stream on top of a stream device.
 *
 * The created stream appends block payloads directly to the device and stages
 * only the in-memory block index until the stream is finished.
 *
 * @param[In]  device       Backing device used to store the stream contents.
 * @param[In]  blockSize    Block size used for staging and on-disk layout.
 * @param[Out] streamOut    Receives the created stream instance on success.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_stream_create(
    struct VaFsStreamDevice* device,
    uint32_t                 blockSize,
    struct VaFsStream**      streamOut);

/**
 * @brief Open a new stream for reading from the provided stream device.
 * 
 * @param[In]  device       The stream device to read from.
 * @param[In]  layout       Outer-image stream layout describing this stream.
 * @param[Out] streamOut    A pointer to where to store the handle of the stream.
 * @return int 0 if the stream was valid and successfully opened, otherwise -1.
 */
extern int vafs_stream_open(
    struct VaFsStreamDevice*  device,
    const VaFsStreamLayout_t* layout,
    struct VaFsStream**       streamOut);

/**
 * @brief Installs encode and decode callbacks for a stream.
 *
 * These callbacks are used when blocks are written to or read from the
 * backing device.
 *
 * @param[In] stream The stream to update.
 * @param[In] encode Optional block encoder used on writes.
 * @param[In] decode Optional block decoder used on reads.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_stream_set_filter(
    struct VaFsStream*   stream,
    VaFsCodecEncodeFunc  encode,
    VaFsCodecDecodeFunc  decode);

/**
 * @brief Retrieves the current logical write position inside a stream.
 *
 * @param[In]  stream    The stream to query.
 * @param[Out] blockOut  Receives the current block index.
 * @param[Out] offsetOut Receives the current offset within that block.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_stream_position(
    struct VaFsStream* stream, 
    vafsblock_t*       blockOut,
    uint32_t*          offsetOut);

/**
 * @brief Creates a read cursor for a stream.
 *
 * Each reader owns its own staged block buffer and logical position so
 * independent callers can read the same stream concurrently.
 *
 * @param[In]  stream     Stream instance to read from.
 * @param[Out] readerOut  Receives the allocated reader.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_stream_reader_open(
    struct VaFsStream*        stream,
    struct VaFsStreamReader** readerOut);

/**
 * @brief Destroys a previously created stream reader.
 *
 * @param[In] reader Reader to destroy. NULL is ignored.
 */
extern void vafs_stream_reader_close(
    struct VaFsStreamReader* reader);

/**
 * @brief Retrieves the configured block size for a stream.
 *
 * @param[In] stream Stream instance to query.
 * @return Stream block size in bytes, or 0 if stream is invalid.
 */
extern uint32_t vafs_stream_block_size(
    struct VaFsStream* stream);

/**
 * @brief Seeks to a logical block and offset within a stream.
 *
 * @param[In] stream      The stream to reposition.
 * @param[In] blockIndex  Destination block index.
 * @param[In] blockOffset Destination byte offset within the block.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_stream_reader_seek(
    struct VaFsStreamReader* reader,
    vafsblock_t              blockIndex,
    uint32_t                 blockOffset);

/**
 * @brief Writes bytes into a writable stream.
 *
 * Data is appended at the current logical stream position and staged into the
 * stream's block buffer.
 *
 * @param[In] stream The stream to write to.
 * @param[In] buffer Source buffer containing the bytes to write.
 * @param[In] size   Number of bytes to write.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_stream_write(
    struct VaFsStream* stream,
    const void*        buffer,
    size_t             size);

/**
 * @brief Reads bytes from a stream at the current logical position.
 *
 * @param[In]  stream    The stream to read from.
 * @param[Out] buffer    Destination buffer for the bytes read.
 * @param[In]  size      Maximum number of bytes to read.
 * @param[Out] bytesRead Receives the number of bytes actually read.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_stream_reader_read(
    struct VaFsStreamReader* reader,
    void*                    buffer,
    size_t                   size,
    size_t*                  bytesRead);

/**
 * @brief Finalizes a writable stream and flushes its metadata.
 *
 * This writes any pending block data, serializes the block header table, and
 * returns the final stream layout for the outer image header.
 *
 * @param[In]  stream    The stream to finish.
 * @param[Out] layoutOut Receives the completed stream layout.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_stream_finish(
    struct VaFsStream*  stream,
    VaFsStreamLayout_t* layoutOut);

/**
 * @brief Closes a stream and frees its in-memory state.
 *
 * This does not implicitly finish a writable stream; callers are expected to
 * call `vafs_stream_finish()` first when they need the final metadata written.
 *
 * @param[In] stream The stream to close.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_stream_close(
    struct VaFsStream* stream);

/**
 * @brief Locks a specific stream for exclusive access, this is neccessary while
 * writing data to the stream, to avoid any concurrent access to those streams, or
 * the user deciding to write two files at once. For read access this is not as neccessary
 * but could still be done.
 * 
 * @param[In] stream The stream that should be locked.
 * @return int Returns -1 if the stream is already locked, 0 on success.
 */
extern int vafs_stream_lock(
    struct VaFsStream* stream);

/**
 * @brief Unlocks a previously locked stream.
 * 
 * @param[In] stream The stream that should be unlocked.
 * @return int Returns -1 if the stream was not locked, 0 on success.
 */
extern int vafs_stream_unlock(
    struct VaFsStream* stream);

#endif // __VAFS_STREAM_STREAM_H_
