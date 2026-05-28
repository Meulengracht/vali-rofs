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

#ifndef __VAFS_XATTR_H__
#define __VAFS_XATTR_H__

#include <vafs/vafs.h>

/**
 * @brief Lists extended attribute names for a filesystem entry.
 *
 * Symbolic links in the path are resolved using the public follow-links policy, so a symlink path
 * reports the xattrs of its resolved target. Names are returned as a sequence of null-terminated
 * strings packed back-to-back.
 *
 * @param vafs         Filesystem handle to query.
 * @param path         Absolute path of the entry.
 * @param buffer       Optional output buffer for the packed name list. Pass NULL to query the
 *                     required size first.
 * @param bufferSize   Size of buffer in bytes.
 * @param bytesWritten Receives the required or written byte count.
 * @return int Returns 0 on success, -1 on failure. See errno for details such as EINVAL,
 *             ENOENT, ENOTDIR, ELOOP, or ERANGE.
 */
extern int vafs_path_listxattr(
    struct VaFs* vafs,
    const char*  path,
    char*        buffer,
    size_t       bufferSize,
    size_t*      bytesWritten);

/**
 * @brief Retrieves one extended attribute value for a filesystem entry.
 *
 * Symbolic links in the path are resolved using the public follow-links policy, so a symlink path
 * reads the xattrs of its resolved target. Callers may pass value as NULL to query the required
 * value size before allocating a buffer.
 *
 * @param vafs         Filesystem handle to query.
 * @param path         Absolute path of the entry.
 * @param name         Null-terminated xattr name to fetch.
 * @param value        Optional buffer that receives the xattr value.
 * @param valueSize    Size of value in bytes.
 * @param bytesWritten Receives the required or written byte count.
 * @return int Returns 0 on success, -1 on failure. See errno for details such as EINVAL,
 *             ENOENT, ENOTDIR, ELOOP, ENODATA, or ERANGE.
 */
extern int vafs_path_getxattr(
    struct VaFs* vafs,
    const char*  path,
    const char*  name,
    void*        value,
    size_t       valueSize,
    size_t*      bytesWritten);

/**
 * @brief Sets or replaces one extended attribute on a filesystem entry.
 *
 * This API is intended for images opened in write mode. Symbolic links in the path are resolved
 * using the public follow-links policy, so setting xattrs through a symlink path updates the
 * resolved target instead of the link object itself.
 *
 * @param vafs      Filesystem handle opened in write mode.
 * @param path      Absolute path of the entry.
 * @param name      Null-terminated xattr name to store.
 * @param value     Optional value buffer. May be NULL only when valueSize is zero.
 * @param valueSize Size of value in bytes.
 * @return int Returns 0 on success, -1 on failure. See errno for details such as EINVAL,
 *             ENOENT, ENOTDIR, ELOOP, or ENOMEM.
 */
extern int vafs_path_setxattr(
    struct VaFs* vafs,
    const char*  path,
    const char*  name,
    const void*  value,
    size_t       valueSize);

#endif //!__VAFS_XATTR_H__
