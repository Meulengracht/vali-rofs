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

#ifndef __VAFS_FILE_H__
#define __VAFS_FILE_H__

#include <vafs/vafs.h>

/**
 * @brief 
 * 
 * @param vafs 
 * @param path 
 * @param handleOut 
 * @return int 
 */
extern int vafs_file_open(
    struct VaFs*            vafs,
    const char*             path,
    struct VaFsFileHandle** handleOut);

/**
 * @brief 
 * 
 * @param handle 
 * @return int 
 */
extern int vafs_file_close(
    struct VaFsFileHandle* handle);

/**
 * @brief 
 * 
 * @param handle 
 * @return size_t 
 */
extern size_t vafs_file_length(
    struct VaFsFileHandle* handle);

/**
 * @brief 
 * 
 * @param handle 
 * @return uint32_t 
 */
extern uint32_t vafs_file_permissions(
    struct VaFsFileHandle* handle);

/**
 * @brief 
 * 
 * @param handle 
 * @param offset 
 * @param whence 
 * @return int 
 */
extern int vafs_file_seek(
    struct VaFsFileHandle* handle,
    long                   offset,
    int                    whence);

/**
 * @brief 
 * 
 * @param handle 
 * @param buffer 
 * @param size 
 * @return size_t 
 */
extern size_t vafs_file_read(
    struct VaFsFileHandle* handle,
    void*                  buffer,
    size_t                 size);

/**
 * @brief 
 * 
 * @param handle 
 * @param buffer 
 * @param size 
 * @return size_t 
 */
extern size_t vafs_file_write(
    struct VaFsFileHandle* handle,
    void*                  buffer,
    size_t                 size);

#endif //!__VAFS_FILE_H__
