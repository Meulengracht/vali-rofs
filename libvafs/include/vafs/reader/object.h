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
 * Vali Initrd Filesystem
 * - Contains the implementation of the Vali Initrd Filesystem.
 *   This filesystem is used to store the initrd of the kernel.
 */

#ifndef __VAFS_OBJECT_READER_H__
#define __VAFS_OBJECT_READER_H__

#include <vafs/vafs.h>
#include <vafs/stat.h>

struct VaFsObjectReader;

/**
 * @brief Opens a child directory by name from an already opened directory.
 *
 * This helper only works on filesystem handles opened in read mode. The name must identify a
 * single path component within handle.
 *
 * @param handle    Parent directory handle.
 * @param name      Name of the child directory to open.
 * @param flags     Path resolution flags applied to the child lookup.
 * @param handleOut Receives the opened child directory handle.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_object_reader_open(
    struct VaFs*              vafs,
    const char*               path,
    enum VaFsLookupFlags      flags,
    struct VaFsObjectReader** readerOut);

/**
 * @brief Closes an object reader handle.
 * 
 * @param handle Object reader handle to close.
 */
extern void vafs_object_reader_close(
    struct VaFsObjectReader* reader);

/**
 * @brief Retrieves the logical length of an object in bytes. 
 * 
 * @return uint64_t For files this is the length of the file data.
 *                  For directories this is the number of entries in the directory.
 *                  For symlinks this is the length of the symlink target string.
 *                  For special entries this is 0.
 */
extern uint64_t vafs_object_reader_length(
    struct VaFsObjectReader* reader);

/**
 * @brief Reads bytes from the current object position and advances it.
 * 
 * @param handle      Object reader handle to read from.
 * @param buffer      Buffer to receive the read data.
 * @param length      Number of bytes to read.
 * @return uint64_t Returns the number of bytes actually read, or -1 on failure.
 */
extern uint64_t vafs_object_reader_read(
    struct VaFsObjectReader* reader,
    void*                    buffer,
    uint64_t                 length);

/**
 * @brief Repositions the logical read cursor for an object reader.
 * 
 * @param handle Object reader handle to reposition.
 * @param offset Offset interpreted relative to `whence`.
 * @param whence One of SEEK_SET, SEEK_CUR, or SEEK_END.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_object_reader_seek(
    struct VaFsObjectReader* reader,
    int64_t                  offset,
    int                      whence);

/**
 * @brief Retrieves metadata for an already opened object.
 * 
 * @param reader Object reader handle to retrieve metadata from.
 * @param metadataOut The object pointer to store the resulting metadata.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_object_reader_stat(
    struct VaFsObjectReader* reader,
    struct VaFsMetadata*     metadataOut);

/**
 * @brief Lists extended attribute names for an already opened object.
 *
 * Names are returned as a packed sequence of null-terminated strings. Callers may pass `buffer == NULL`
 * with `bufferSize == 0` to query the required buffer size first.
 *
 * @param handle            Object reader handle to query.
 * @param buffer            Optional destination buffer for packed names.
 * @param bufferSize        Size of `buffer` in bytes.
 * @param bytesWrittenOut   Receives the required or written byte count.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_object_reader_listxattr(
    struct VaFsObjectReader* handle,
    char*                    buffer,
    size_t                   bufferSize,
    size_t*                  bytesWrittenOut);

/**
 * @brief Reads one extended attribute value from an already opened object.
 *
 * Callers may pass `value == NULL` with `valueSize == 0` to query the required buffer size first.
 *
 * @param handle            Object reader handle to query.
 * @param name              Null-terminated attribute name.
 * @param value             Optional destination buffer.
 * @param valueSize         Size of `value` in bytes.
 * @param bytesWrittenOut   Receives the required or written byte count.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_object_reader_getxattr(
    struct VaFsObjectReader* handle,
    const char*              name,
    void*                    value,
    size_t                   valueSize,
    size_t*                  bytesWrittenOut);

#endif //!__VAFS_OBJECT_READER_H__
