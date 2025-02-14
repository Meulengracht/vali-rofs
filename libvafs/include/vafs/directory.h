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

/**
 * @brief 
 * 
 * @param handle
 * @param path 
 * @param handleOut 
 * @return int 
 */
extern int vafs_directory_open(
    struct VaFs*                 vafs,
    const char*                  path,
    struct VaFsDirectoryHandle** handleOut);

/**
 * @brief 
 * 
 * @param handle 
 * @return int 
 */
extern int vafs_directory_close(
    struct VaFsDirectoryHandle* handle);

/**
 * @brief 
 * 
 * @param handle 
 * @return uint32_t 
 */
extern uint32_t vafs_directory_permissions(
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
 * @brief 
 * 
 * @param handle 
 * @param name 
 * @param handleOut 
 * @return int 
 */
extern int vafs_directory_open_directory(
    struct VaFsDirectoryHandle*  handle,
    const char*                  name,
    struct VaFsDirectoryHandle** handleOut);

/**
 * @brief 
 * 
 * @param handle 
 * @param name 
 * @param permissions
 * @param handleOut 
 * @return int 
 */
extern int vafs_directory_create_directory(
    struct VaFsDirectoryHandle*  handle,
    const char*                  name,
    uint32_t                     permissions,
    struct VaFsDirectoryHandle** handleOut);

/**
 * @brief 
 * 
 * @param handle 
 * @param name 
 * @param target 
 * @return int 
 */
extern int vafs_directory_create_symlink(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    const char*                 target);

/**
 * @brief 
 * 
 * @param handle 
 * @param name 
 * @param targetOut 
 * @return int 
 */
extern int vafs_directory_read_symlink(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    const char**                targetOut);

/**
 * @brief 
 * 
 * @param handle 
 * @param name 
 * @param handleOut
 * @return int 
 */
extern int vafs_directory_open_file(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    struct VaFsFileHandle**     handleOut);

/**
 * @brief 
 * 
 * @param handle 
 * @param name 
 * @param permissions
 * @param handleOut
 * @return int 
 */
extern int vafs_directory_create_file(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    uint32_t                    permissions,
    struct VaFsFileHandle**     handleOut);

#endif //!__VAFS_DIRECTORY_H__
