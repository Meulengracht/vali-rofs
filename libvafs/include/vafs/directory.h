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

#ifndef __VAFS_DIRECTORY_H__
#define __VAFS_DIRECTORY_H__

#include <vafs/vafs.h>
#include <vafs/stat.h>

/**
 * @brief Opens a directory by absolute path.
 *
 * The root path "/" is valid and returns a handle to the filesystem root. Symbolic links in the
 * path are resolved up to the library's symlink depth limit.
 *
 * @param vafs      Filesystem handle to search in.
 * @param path      Absolute path of the directory to open.
 * @param handleOut Receives the opened directory handle on success.
 * @return int Returns 0 on success, -1 on failure. See errno for details such as EINVAL,
 *             ENOENT, ENOTDIR, or ELOOP.
 */
extern int vafs_directory_open(
    struct VaFs*                 vafs,
    const char*                  path,
    struct VaFsDirectoryHandle** handleOut);

/**
 * @brief Closes a directory handle.
 *
 * @param handle Directory handle to close.
 * @return int Returns 0 on success, -1 if handle is invalid.
 */
extern int vafs_directory_close(
    struct VaFsDirectoryHandle* handle);

/**
 * @brief Reads an entry from the directory handle.
 * 
 * @param[In]  handle The directory handle to read an entry from.
 * @param[Out] entry  A pointer to a struct VaFsEntry that is filled with information if an entry is available. 
 * @return int Returns -1 on error or if no more entries are available (errno is set accordingly), 0 on success
 */
extern int vafs_directory_read(
    struct VaFsDirectoryHandle* handle,
    struct VaFsEntry*           entry);

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
extern int vafs_directory_open_directory(
    struct VaFsDirectoryHandle*  handle,
    const char*                  name,
    struct VaFsDirectoryHandle** handleOut);

/**
 * @brief Creates a child directory while building an image.
 *
 * If the named directory already exists, a handle to that directory is returned instead of creating
 * a duplicate entry.
 *
 * @param handle      Parent directory handle opened in write mode.
 * @param name        Name of the child directory to create.
 * @param metadata    Metadata to store for the directory.
 * @param handleOut   Receives the created or existing child directory handle.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_directory_create_directory(
    struct VaFsDirectoryHandle*  handle,
    const char*                  name,
     const struct VaFsMetadata*   metadata,
    struct VaFsDirectoryHandle** handleOut);

/**
 * @brief Creates a symbolic link entry in a directory opened for writing.
 *
 * @param handle   Parent directory handle opened in write mode.
 * @param name     Name of the symbolic link entry.
 * @param target   Target path stored in the symbolic link.
 * @param metadata Metadata to store for the link entry.
 * @return int Returns 0 on success, -1 on failure. Returns EEXIST if name already exists.
 */
extern int vafs_directory_create_symlink(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
     const char*                 target,
     const struct VaFsMetadata*  metadata);

/**
 * @brief Looks up a child symbolic link and returns its stored target string.
 *
 * The returned pointer refers to data owned by the filesystem and remains valid only while the
 * underlying image stays open.
 *
 * @param handle    Parent directory handle opened in read mode.
 * @param name      Name of the symbolic link entry.
 * @param targetOut Receives a borrowed pointer to the symlink target string.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_directory_read_symlink(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    const char**                targetOut);

/**
 * @brief Opens a child file by name from an already opened directory.
 *
 * This helper only works on filesystem handles opened in read mode. The name must identify a
 * single file entry within handle.
 *
 * @param handle    Parent directory handle.
 * @param name      Name of the child file to open.
 * @param handleOut Receives the opened file handle.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_directory_open_file(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    struct VaFsFileHandle**     handleOut);

/**
 * @brief Creates a child file while building an image.
 *
 * @param handle      Parent directory handle opened in write mode.
 * @param name        Name of the child file to create.
 * @param metadata    Metadata to store for the file.
 * @param handleOut   Receives the opened file handle for the new entry.
 * @return int Returns 0 on success, -1 on failure. Returns EEXIST if name already exists.
 */
extern int vafs_directory_create_file(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    const struct VaFsMetadata*  metadata,
    struct VaFsFileHandle**     handleOut);

/**
 * @brief Creates a special file entry while building an image.
 *
 * The API surface needs to converge before the descriptor format does, so the
 * initial implementation may report `ENOTSUP` until special-file serialization
 * support lands in the image format.
 *
 * @param handle   Parent directory handle opened in write mode.
 * @param name     Name of the special-file entry.
 * @param metadata Metadata to store for the entry.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_directory_create_special(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    const struct VaFsMetadata*  metadata);

/**
 * @brief Creates a hardlink entry while building an image.
 *
 * @param handle   Parent directory handle opened in write mode.
 * @param name     Name of the hardlink entry.
 * @param objectId Stable object identifier of the link target.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_directory_create_hardlink(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    uint64_t                    objectId);

#endif //!__VAFS_DIRECTORY_H__
