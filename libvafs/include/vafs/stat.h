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

#ifndef __VAFS_STAT_H__
#define __VAFS_STAT_H__

#include <vafs/vafs.h>
#include <vafs/platform.h>

struct vafs_stat {
    uint32_t mode;
    size_t   size;
};

/**
 * @brief Retrieves POSIX-like metadata for a filesystem entry.
 *
 * The returned mode field contains both the entry type bits and the stored permission bits. When
 * followLinks is non-zero, symbolic links in the path are resolved up to the library's symlink
 * depth limit; otherwise the metadata of the link itself is returned.
 *
 * @param vafs        Filesystem handle to query.
 * @param path        Absolute path of the entry.
 * @param followLinks Non-zero to follow symbolic links, 0 to stat the link itself.
 * @param stat        Receives the resulting metadata.
 * @return int Returns 0 on success, -1 on failure. See errno for details such as EINVAL,
 *             ENOENT, ENOTDIR, or ELOOP.
 */
extern int vafs_path_stat(
    struct VaFs*      vafs,
    const char*       path,
    int               followLinks,
    struct vafs_stat* stat);

#endif //!__VAFS_STAT_H__
