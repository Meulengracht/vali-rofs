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
#include <stdlib.h>
#include <string.h>

#include "private.h"

struct VaFsSymlinkHandle {
    struct VaFsSymlink* Symlink;
};

struct VaFsSymlinkHandle* __symlink_handle_new(
        struct VaFsSymlink* symlinkEntry)
{
    struct VaFsSymlinkHandle* handle;

    handle = (struct VaFsSymlinkHandle*)malloc(sizeof(struct VaFsSymlinkHandle));
    if (!handle) {
        errno = ENOMEM;
        return NULL;
    }

    handle->Symlink = symlinkEntry;
    return handle;
}

int vafs_symlink_open(
        struct VaFs*               vafs,
        const char*                path,
        struct VaFsSymlinkHandle** handleOut)
{
    struct VaFsDirectory*      currentDirectory;
    struct VaFsDirectoryEntry* entry;
    const char*                remainingPath = path;
    char                       token[VAFS_NAME_MAX + 1];

    if (vafs == NULL || path == NULL || handleOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (__vafs_ensure_root_open(vafs) != 0) {
        return -1;
    }

    if (__vafs_is_root_path(path)) {
        errno = EISDIR;
        return -1;
    }

    currentDirectory = vafs->RootDirectory;
    do {
        int charsConsumed = __vafs_pathtoken(remainingPath, token, sizeof(token));
        if (!charsConsumed) {
            break;
        }
        remainingPath += charsConsumed;

        entry = __vafs_directory_find_entry(currentDirectory, token);
        if (entry == NULL) {
            return -1;
        }

        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
            if (remainingPath[0] == '\0') {
                errno = EISDIR;
                return -1;
            }

            currentDirectory = entry->Directory;
            continue;
        }

        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_SYMLINK) {
            if (remainingPath[0] != '\0') {
                errno = ENOTDIR;
                return -1;
            }

            *handleOut = __symlink_handle_new(entry->Symlink);
            return 0;
        }

        errno = ENOENT;
        return -1;
    } while (1);
    errno = ENOENT;
    return -1;
}

void vafs_symlink_destroy(
    struct VaFsSymlink* symlink)
{
    if (symlink == NULL) {
        return;
    }

    __vafs_xattr_set_destroy(symlink->Xattrs);
    free((void*)symlink->Name);
    free((void*)symlink->Target);
    free(symlink);
}

int vafs_symlink_close(
        struct VaFsSymlinkHandle* handle)
{
    if (!handle) {
        errno = EINVAL;
        return -1;
    }

    free(handle);
    return 0;
}

int vafs_symlink_target(
        struct VaFsSymlinkHandle* handle,
        void*                     buffer,
        size_t                    size)
{
    if (handle == NULL || buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    strncpy(buffer, handle->Symlink->Target, size);
    return 0;
}

int vafs_symlink_stat(
        struct VaFsSymlinkHandle* handle,
        struct VaFsMetadata*      metadata)
{
    if (handle == NULL || metadata == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (!handle->Symlink->StatCached) {
        vafs_metadata_initialize(&handle->Symlink->Stat);
    }

    handle->Symlink->Stat.Type = VaFsEntryType_Symlink;
    handle->Symlink->Stat.Size = strlen(handle->Symlink->Target);
    handle->Symlink->Stat.Mask |= VaFsMetadataMask_Type | VaFsMetadataMask_Size;
    if ((handle->Symlink->Stat.Mask & VaFsMetadataMask_LinkCount) == 0) {
        handle->Symlink->Stat.LinkCount = 1;
        handle->Symlink->Stat.Mask |= VaFsMetadataMask_LinkCount;
    }

    handle->Symlink->StatCached = 1;
    *metadata = handle->Symlink->Stat;
    return 0;
}
