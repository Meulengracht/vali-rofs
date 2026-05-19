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
 */

#include "utils.h"

#include <errno.h>
#include <vafs/stat.h>

#if defined(_WIN32) || defined(_WIN64)
#include "dirent_win32.h"
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#if defined(__linux__)
#include <sys/sysmacros.h>
#endif
#endif

static enum VaFsEntryType __platform_fs_mode_to_entry_type(uint32_t mode)
{
    if (platform_fs_mode_is_directory(mode)) {
        return VaFsEntryType_Directory;
    }
    if (platform_fs_mode_is_symlink(mode)) {
        return VaFsEntryType_Symlink;
    }
    if (platform_fs_mode_is_character_device(mode)) {
        return VaFsEntryType_CharacterDevice;
    }
    if (platform_fs_mode_is_block_device(mode)) {
        return VaFsEntryType_BlockDevice;
    }
    if (platform_fs_mode_is_fifo(mode)) {
        return VaFsEntryType_Fifo;
    }
    if (platform_fs_mode_is_file(mode)) {
        return VaFsEntryType_File;
    }
    return VaFsEntryType_Unknown;
}

int platform_fs_mode_is_file(uint32_t mode)
{
    return S_ISREG(mode);
}

int platform_fs_mode_is_symlink(uint32_t mode)
{
    return S_ISLNK(mode);
}

int platform_fs_mode_is_directory(uint32_t mode)
{
    return S_ISDIR(mode);
}

int platform_fs_mode_is_character_device(uint32_t mode)
{
    return S_ISCHR(mode);
}

int platform_fs_mode_is_block_device(uint32_t mode)
{
#if defined(_WIN32) || defined(_WIN64)
    (void)mode;
    return 0;
#else
    return S_ISBLK(mode);
#endif
}

int platform_fs_mode_is_fifo(uint32_t mode)
{
    return S_ISFIFO(mode);
}

int platform_fs_mode_is_special(uint32_t mode)
{
    return platform_fs_mode_is_character_device(mode) ||
        platform_fs_mode_is_block_device(mode) ||
        platform_fs_mode_is_fifo(mode);
}

uint32_t platform_fs_mode_permissions(uint32_t mode)
{
    return mode & 07777u;
}

int platform_fs_read_metadata(const char* path, struct VaFsMetadata* metadata)
{
    struct stat        st;
    enum VaFsEntryType type;

    if (path == NULL || metadata == NULL) {
        errno = EINVAL;
        return -1;
    }

#if defined(_WIN32) || defined(_WIN64)
    if (stat(path, &st) != 0) {
        return -1;
    }
#else
    // The builder needs lstat-style metadata here so symlinks and device nodes
    // keep their own type instead of inheriting the host target's type.
    if (lstat(path, &st) != 0) {
        return -1;
    }
#endif

    type = __platform_fs_mode_to_entry_type((uint32_t)st.st_mode);
    if (type == VaFsEntryType_Unknown) {
        errno = EINVAL;
        return -1;
    }

    vafs_metadata_initialize(metadata);
    vafs_metadata_set_mode(metadata, type, platform_fs_mode_permissions((uint32_t)st.st_mode));

#if !defined(_WIN32) && !defined(_WIN64)
    if (type == VaFsEntryType_CharacterDevice || type == VaFsEntryType_BlockDevice) {
        metadata->Device.Major = (uint32_t)major(st.st_rdev);
        metadata->Device.Minor = (uint32_t)minor(st.st_rdev);
        metadata->Mask |= VaFsMetadataMask_Device;
    }
#endif
    return 0;
}

int platform_fs_directory_exists(const char* path)
{
    struct stat st;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (stat(path, &st)) {
        if (errno == ENOENT) {
            return 0;
        }
        return -1;
    }
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

int platform_fs_create_directory(const char* path, uint32_t permissions)
{
    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

#if defined(_WIN32) || defined(_WIN64)
    (void)permissions;
    return _mkdir(path);
#else
    return mkdir(path, (mode_t)permissions);
#endif
}

int platform_fs_create_special(const char* path, const struct VaFsMetadata* metadata)
{
    if (path == NULL || metadata == NULL) {
        errno = EINVAL;
        return -1;
    }

#if defined(_WIN32) || defined(_WIN64)
    (void)path;
    (void)metadata;
    errno = ENOTSUP;
    return -1;
#else
    int    status;
    mode_t permissions;

    permissions = (mode_t)(metadata->Mode & 07777u);
    switch (metadata->Type) {
        case VaFsEntryType_CharacterDevice:
            if ((metadata->Mask & VaFsMetadataMask_Device) == 0) {
                errno = EINVAL;
                return -1;
            }
            status = mknod(
                path,
                S_IFCHR | permissions,
                makedev(metadata->Device.Major, metadata->Device.Minor)
            );
            break;
        case VaFsEntryType_BlockDevice:
            if ((metadata->Mask & VaFsMetadataMask_Device) == 0) {
                errno = EINVAL;
                return -1;
            }
            status = mknod(
                path,
                S_IFBLK | permissions,
                makedev(metadata->Device.Major, metadata->Device.Minor)
            );
            break;
        case VaFsEntryType_Fifo:
            status = mkfifo(path, permissions);
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    if (status != 0 && errno != EEXIST) {
        return -1;
    }
    return platform_fs_chmod(path, metadata->Mode & 07777u);
#endif
}

int platform_fs_chmod(const char* path, uint32_t permissions)
{
    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

#if defined(_WIN32) || defined(_WIN64)
    return _chmod(path, (int)permissions);
#else
    return chmod(path, (mode_t)permissions);
#endif
}