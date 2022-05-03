/**
 * Copyright 2022, Philip Meulengracht
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
 * @brief 
 * 
 * @param vafs 
 * @param path 
 * @param stat 
 * @return int 
 */
extern int vafs_path_stat(
    struct VaFs*      vafs,
    const char*       path,
    struct vafs_stat* stat);

#endif //!__VAFS_STAT_H__
