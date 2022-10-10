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

#include <errno.h>
#include "private.h"
#include <stdlib.h>
#include <string.h>
#include <vafs/file.h>

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
    struct VaFsDirectoryEntry* entries;
    const char*                remainingPath = path;
    char                       token[VAFS_NAME_MAX + 1];

    if (vafs == NULL || path == NULL || handleOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (__vafs_is_root_path(path)) {
        errno = EISDIR;
        return -1;
    }

    entries = __vafs_directory_entries(vafs->RootDirectory);
    do {
        int charsConsumed = __vafs_pathtoken(remainingPath, token, sizeof(token));
        if (!charsConsumed) {
            break;
        }
        remainingPath += charsConsumed;

        // find the name in the directory
        while (entries != NULL) {
            if (!strcmp(__vafs_directory_entry_name(entries), token)) {
                if (entries->Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
                    // If we encounter a directory in this case, we must not
                    // be at the end of the path
                    if (remainingPath[0] == '\0') {
                        errno = EISDIR;
                        return -1;
                    }

                    // fall through this entire if/else
                } else if (entries->Type == VA_FS_DESCRIPTOR_TYPE_SYMLINK) {
                    // If we encounter a symlink in this case, we must be at the end of the path
                    if (remainingPath[0] != '\0') {
                        errno = ENOTDIR;
                        return -1;
                    }

                    *handleOut = __symlink_handle_new(entries->Symlink);
                    return 0;
                } else {
                    errno = ENOENT;
                    return -1;
                }

                entries = __vafs_directory_entries(entries->Directory);
                break;
            }
            entries = entries->Link;
        }
    } while (1);
    return -1;
}

void vafs_symlink_destroy(
    struct VaFsSymlink* symlink)
{
    if (symlink == NULL) {
        return;
    }

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
