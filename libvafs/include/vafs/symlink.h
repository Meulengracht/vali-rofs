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

#ifndef __VAFS_SYMLINK_H__
#define __VAFS_SYMLINK_H__

#include <vafs/vafs.h>
#include <vafs/stat.h>

/**
 * @brief Opens a symbolic link by absolute path without following it.
 *
 * The final path component must resolve to a symbolic link entry.
 *
 * @param vafs      Filesystem handle to search in.
 * @param path      Absolute path of the symbolic link to open.
 * @param handleOut Receives the opened symbolic link handle on success.
 * @return int Returns 0 on success, -1 on failure. See errno for details such as EINVAL,
 *             ENOENT, EISDIR, or ENOTDIR.
 */
extern int vafs_symlink_open(
    struct VaFs*               vafs,
    const char*                path,
    struct VaFsSymlinkHandle** handleOut);

/**
 * @brief Closes a symbolic link handle.
 *
 * @param handle Symbolic link handle to close.
 * @return int Returns 0 on success, -1 if handle is invalid.
 */
extern int vafs_symlink_close(
    struct VaFsSymlinkHandle* handle);

/**
 * @brief Copies the symlink target into a caller-provided buffer.
 *
 * The target is copied with strncpy semantics, so the result is only null-terminated when the
 * destination buffer is large enough to hold the full target string.
 *
 * @param handle Symbolic link handle to read from.
 * @param buffer Destination buffer for the target path.
 * @param size   Size of buffer in bytes.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_symlink_target(
    struct VaFsSymlinkHandle* handle,
    void*                     buffer,
    size_t                    size);

#endif //!__VAFS_SYMLINK_H__
