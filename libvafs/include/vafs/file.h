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

#ifndef __VAFS_FILE_H__
#define __VAFS_FILE_H__

#include <vafs/vafs.h>
#include <vafs/stat.h>

/**
 * @brief Opens a file by absolute path for reading from an image opened in read mode.
 *
 * Symbolic links in the path are resolved up to the library's symlink depth limit.
 * The final path component must resolve to a file.
 *
 * @param vafs      Filesystem handle to search in.
 * @param path      Absolute path of the file to open.
 * @param handleOut Receives the opened file handle on success.
 * @return int Returns 0 on success, -1 on failure. See errno for details such as
 *             EINVAL, ENOENT, EISDIR, or ELOOP.
 */
extern int vafs_file_open(
    struct VaFs*            vafs,
    const char*             path,
    struct VaFsFileHandle** handleOut);

/**
 * @brief Closes a file handle previously returned by the VaFs file APIs.
 *
 * Closing a writable file handle also releases the underlying data stream lock.
 *
 * @param handle File handle to close.
 * @return int Returns 0 on success, -1 if handle is invalid.
 */
extern int vafs_file_close(
    struct VaFsFileHandle* handle);

/**
 * @brief Returns the logical length of the file in bytes.
 *
 * @param handle File handle to query.
 * @return size_t File length in bytes, or (size_t)-1 if handle is invalid.
 */
extern size_t vafs_file_length(
    struct VaFsFileHandle* handle);

/**
 * @brief Moves the current read position within an opened file.
 *
 * Seeking is only supported for file handles backed by a filesystem opened in read mode.
 * The resulting position is clamped to the range [0, file_length].
 *
 * @param handle File handle to reposition.
 * @param offset Offset interpreted relative to whence.
 * @param whence One of SEEK_SET, SEEK_CUR, or SEEK_END.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_file_seek(
    struct VaFsFileHandle* handle,
    long                   offset,
    int                    whence);

/**
 * @brief Reads bytes from the current file position and advances it.
 *
 * Reads are capped to the remaining file length. A return value of 0 can mean either end of file
 * or an error, so inspect errno when you need to distinguish those cases.
 *
 * @param[In] handle File handle to read from.
 * @param[In] buffer Destination buffer.
 * @param[In] size   Maximum number of bytes to read.
 * @return size_t Number of bytes read, or 0 if no bytes were produced. Errors are reported through
 *                errno, for example EINVAL, EBUSY, or ENOTSUP.
 */
extern size_t vafs_file_read(
    struct VaFsFileHandle* handle,
    void*                  buffer,
    size_t                 size);

/**
 * @brief Appends data to a file while building an image opened in write mode.
 *
 * The file's logical length grows by size bytes on success.
 *
 * @param handle File handle to write to.
 * @param buffer Source buffer containing the bytes to write.
 * @param size   Number of bytes to append.
 * @return size_t Returns 0 on success, or (size_t)-1 on failure.
 */
extern size_t vafs_file_write(
    struct VaFsFileHandle* handle,
    void*                  buffer,
    size_t                 size);

#endif //!__VAFS_FILE_H__
