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

#ifndef __VAFS_DIRECTORY_BUILDER_H__
#define __VAFS_DIRECTORY_BUILDER_H__

#include <vafs/vafs.h>
#include <vafs/stat.h>

struct VaFsDirectoryBuilder;
struct VaFsFileBuilder;
struct VaFsObjectBuilder;

/**
 * @brief Closes a directory builder handle.
 *
 * Closing a directory builder releases the traversal handle only. The underlying directory object
 * remains part of the image being built.
 *
 * @param handle Directory builder handle to close.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_directory_builder_close(
    struct VaFsDirectoryBuilder* handle);

/**
 * @brief Creates a child directory.
 *
 * The builder does not implement open-or-create semantics. If the name already exists, the call
 * fails with EEXIST instead of returning an existing child handle. `builderOut` receives an owned
 * child directory builder handle. `objectOut` receives a borrowed object handle for the created
 * directory when non-NULL.
 *
 * @param builder     Parent directory builder handle.
 * @param name        Single path component naming the child directory.
 * @param metadata    Metadata to store for the new directory object.
 * @param builderOut  Receives the opened child directory builder handle.
 * @param objectOut   Optional borrowed object handle for the created directory.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_directory_builder_create_directory(
    struct VaFsDirectoryBuilder*  builder,
    const char*                   name,
    const struct VaFsMetadata*    metadata,
    struct VaFsDirectoryBuilder** builderOut,
    struct VaFsObjectBuilder**    objectOut);

/**
 * @brief Creates a child file.
 *
 * `handleOut` receives an owned file builder handle. `objectOut` receives a borrowed object handle
 * for the created file when non-NULL.
 *
 * @param handle      Parent directory builder handle.
 * @param name        Single path component naming the child file.
 * @param metadata    Metadata to store for the new file object.
 * @param handleOut   Receives the opened file builder handle.
 * @param objectOut   Optional borrowed object handle for the created file.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_directory_builder_create_file(
    struct VaFsDirectoryBuilder* handle,
    const char*                  name,
    const struct VaFsMetadata*   metadata,
    struct VaFsFileBuilder**     handleOut,
    struct VaFsObjectBuilder**   objectOut);

/**
 * @brief Creates a child symbolic link.
 *
 * `objectOut` receives a borrowed object handle for the created symlink when non-NULL.
 *
 * @param handle      Parent directory builder handle.
 * @param name        Single path component naming the child symlink.
 * @param target      Target path stored in the symlink object.
 * @param metadata    Metadata to store for the new symlink object.
 * @param objectOut   Optional borrowed object handle for the created symlink.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_directory_builder_create_symlink(
    struct VaFsDirectoryBuilder* handle,
    const char*                  name,
    const char*                  target,
    const struct VaFsMetadata*   metadata,
    struct VaFsObjectBuilder**   objectOut);

/**
 * @brief Creates a child special file.
 *
 * `type` must be one of VaFsEntryType_CharacterDevice, VaFsEntryType_BlockDevice, or
 * VaFsEntryType_Fifo. `objectOut` receives a borrowed object handle for the created special object
 * when non-NULL.
 *
 * @param handle      Parent directory builder handle.
 * @param name        Single path component naming the special file.
 * @param type        Special-file node type to create.
 * @param metadata    Metadata to store for the new object.
 * @param device      Device number for character and block devices. May be NULL for fifos.
 * @param objectOut   Optional borrowed object handle for the created special object.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_directory_builder_create_special(
    struct VaFsDirectoryBuilder*   handle,
    const char*                    name,
    enum VaFsEntryType             type,
    const struct VaFsMetadata*     metadata,
    const struct VaFsDeviceNumber* device,
    struct VaFsObjectBuilder**     objectOut);

/**
 * @brief Creates another directory entry that references an existing non-directory object.
 *
 * `target` must refer to a previously created non-directory object owned by the same builder.
 *
 * @param handle   Parent directory builder handle.
 * @param name     Single path component naming the new entry.
 * @param target   Borrowed object handle naming the existing target object.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_directory_builder_link(
    struct VaFsDirectoryBuilder* handle,
    const char*                  name,
    struct VaFsObjectBuilder*    target);

/**
 * @brief Appends bytes to a file builder.
 *
 * @param handle            File builder handle to write to.
 * @param buffer            Source bytes to append.
 * @param length            Number of bytes to append.
 * @param bytesWrittenOut   Receives the number of bytes accepted.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_file_builder_write(
    struct VaFsFileBuilder* handle,
    const void*             buffer,
    size_t                  length,
    size_t*                 bytesWrittenOut);

/**
 * @brief Closes a file builder handle.
 *
 * Closing the file builder finalizes the file payload extent in the in-progress image, but the
 * created file object itself remains part of the builder state.
 *
 * @param handle File builder handle to close.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_file_builder_close(
    struct VaFsFileBuilder* handle);

#endif //!__VAFS_DIRECTORY_BUILDER_H__
