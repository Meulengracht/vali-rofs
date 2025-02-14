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

/**
 * @brief 
 * 
 * @param vafs 
 * @param path 
 * @param handleOut 
 * @return int 
 */
extern int vafs_symlink_open(
    struct VaFs*               vafs,
    const char*                path,
    struct VaFsSymlinkHandle** handleOut);

/**
 * @brief 
 * 
 * @param handle 
 * @return int 
 */
extern int vafs_symlink_close(
    struct VaFsSymlinkHandle* handle);

/**
 * @brief 
 * 
 * @param handle 
 * @param buffer 
 * @param size 
 * @return int
 */
extern int vafs_symlink_target(
    struct VaFsSymlinkHandle* handle,
    void*                     buffer,
    size_t                    size);

#endif //!__VAFS_SYMLINK_H__
