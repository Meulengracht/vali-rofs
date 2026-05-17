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

#include <errno.h>
#include <vafs/xattr.h>

int vafs_path_listxattr(
    struct VaFs* vafs,
    const char*  path,
    char*        buffer,
    size_t       bufferSize,
    size_t*      bytesWritten)
{
    (void)vafs;
    (void)path;
    (void)buffer;
    (void)bufferSize;
    (void)bytesWritten;

    errno = ENOTSUP;
    return -1;
}

int vafs_path_getxattr(
    struct VaFs* vafs,
    const char*  path,
    const char*  name,
    void*        value,
    size_t       valueSize,
    size_t*      bytesWritten)
{
    (void)vafs;
    (void)path;
    (void)name;
    (void)value;
    (void)valueSize;
    (void)bytesWritten;

    errno = ENOTSUP;
    return -1;
}

int vafs_path_setxattr(
    struct VaFs* vafs,
    const char*  path,
    const char*  name,
    const void*  value,
    size_t       valueSize)
{
    (void)vafs;
    (void)path;
    (void)name;
    (void)value;
    (void)valueSize;

    errno = ENOTSUP;
    return -1;
}