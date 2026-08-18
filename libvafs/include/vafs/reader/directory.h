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

#ifndef __VAFS_DIRECTORY_READER_H__
#define __VAFS_DIRECTORY_READER_H__

#include <vafs/vafs.h>

struct VaFsDirectoryReader;
struct VaFsObjectReader;

/**
 * @brief Opens a directory by absolute path.
 *
 * The root path "/" is valid and returns a handle to the filesystem root. Symbolic links in the
 * path are resolved up to the library's symlink depth limit.
 *
 * @param vafs      Filesystem handle to search in.
 * @param path      Absolute path of the directory to open.
 * @param flags     Path resolution flags. See VaFsLookupFlags.
 * @param handleOut Receives the opened directory handle on success.
 * @return int Returns 0 on success, -1 on failure. See errno for details such as EINVAL,
 *             ENOENT, ENOTDIR, or ELOOP.
 */
extern int vafs_directory_reader_open(
    struct VaFs*                 vafs,
    const char*                  path,
    enum VaFsLookupFlags         flags,
    struct VaFsDirectoryReader** readerOut);

/**
 * @brief Opens a child directory by name from an already opened directory.
 * 
 * This helper only works on filesystem handles opened in read mode. The name must identify a
 * single path component within handle.
 * 
 * @param handle    Parent directory handle.
 * @param name      Name of the child directory to open.
 * @param handleOut Receives the opened child directory handle.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_directory_reader_open_object_in(
    struct VaFsDirectoryReader* reader,
    const char*                 name,
    struct VaFsObjectReader**   readerOut);
extern int vafs_directory_reader_open_directory_in(
    struct VaFsDirectoryReader*  reader,
    const char*                  name,
    struct VaFsDirectoryReader** readerOut);

/**
 * @brief Reads an entry from the directory handle.
 * 
 * @param[In]  handle The directory handle to read an entry from.
 * @param[Out] entry  A pointer to a struct VaFsEntry that is filled with information if an entry is available. 
 * @return int Returns -1 on error or if no more entries are available (errno is set accordingly), 0 on success
 */
extern int vafs_directory_reader_next(
    struct VaFsDirectoryReader* handle,
    struct VaFsEntry*           entry);

/**
 * @brief Closes a directory handle.
 *
 * @param handle Directory handle to close.
 * @return int Returns 0 on success, -1 if handle is invalid.
 */
extern int vafs_directory_reader_close(
    struct VaFsDirectoryReader* handle);

#endif //!__VAFS_DIRECTORY_READER_H__
