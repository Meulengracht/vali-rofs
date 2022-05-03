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

enum VaFsFileState {
    VaFsFileState_Open,
    VaFsFileState_Read,
    VaFsFileState_Write
};

struct VaFsFileHandle {
    struct VaFsFile*   File;
    enum VaFsFileState State;
    uint32_t           Position;
};


int vafs_file_open(
    struct VaFs*            vafs,
    const char*             path,
    struct VaFsFileHandle** handleOut)
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
                    char* pathBuffer = malloc(VAFS_PATH_MAX);
                    int   written;
                    int   status;
                    if (!pathBuffer) {
                        VAFS_ERROR("vafs_directory_open: failed to allocate path buffer\n");
                        errno = ENOMEM;
                        return -1;
                    }

                    written = __vafs_resolve_symlink(pathBuffer, VAFS_PATH_MAX, path, remainingPath - path, entries->Symlink->Target);
                    if (written < 0) {
                        VAFS_ERROR("vafs_directory_open: failed to resolve symlink %s\n", entries->Symlink->Target);
                        free(pathBuffer);
                        return -1;
                    }

                    status = vafs_file_open(vafs, pathBuffer, handleOut);
                    free(pathBuffer);
                    return status;
                } else if (entries->Type == VA_FS_DESCRIPTOR_TYPE_FILE) {
                    // If we encounter a file in this case, we must be at the end of the path
                    if (remainingPath[0] != '\0') {
                        errno = EISDIR;
                        return -1;
                    }

                    *handleOut = vafs_file_create_handle(entries->File);
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

struct VaFsFileHandle* vafs_file_create_handle(
    struct VaFsFile* fileEntry)
{
    struct VaFsFileHandle* handle;
    
    handle = (struct VaFsFileHandle*)malloc(sizeof(struct VaFsFileHandle));
    if (!handle) {
        errno = ENOMEM;
        return NULL;
    }
    
    handle->File = fileEntry;
    handle->Position = 0;
    handle->State = VaFsFileState_Open;
    
    return handle;
}

int vafs_file_close(
    struct VaFsFileHandle* handle)
{
    if (!handle) {
        errno = EINVAL;
        return -1;
    }

    if (handle->State == VaFsFileState_Write) {
        vafs_stream_unlock(handle->File->VaFs->DataStream);
    }

    free(handle);
    return 0;
}

size_t vafs_file_length(
    struct VaFsFileHandle* handle)
{
    if (!handle) {
        errno = EINVAL;
        return (size_t)-1;
    }

    return handle->File->Descriptor.FileLength;
}

uint32_t vafs_file_permissions(
    struct VaFsFileHandle* handle)
{
    if (!handle) {
        errno = EINVAL;
        return (uint32_t)-1;
    }

    return handle->File->Descriptor.Permissions;
}

int vafs_file_seek(
    struct VaFsFileHandle* handle,
    long                   offset,
    int                    whence)
{
    if (!handle) {
        errno = EINVAL;
        return -1;
    }

    // this is not valid when writing files
    if (handle->File->VaFs->Mode == VaFsMode_Write) {
        errno = ENOTSUP;
        return -1;
    }

    switch (whence) {
        case SEEK_SET:
            handle->Position = offset;
            break;
        case SEEK_CUR:
            handle->Position += offset;
            break;
        case SEEK_END:
            handle->Position = handle->File->Descriptor.FileLength + offset;
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    handle->Position = MIN(MAX(handle->Position, 0), handle->File->Descriptor.FileLength);
    
    // reset the block buffer
    return 0;
}

size_t vafs_file_read(
    struct VaFsFileHandle* handle,
    void*                  buffer,
    size_t                 size)
{
    size_t read;
    int    status;

    if (!handle) {
        errno = EINVAL;
        return -1;
    }

    // this is not valid when writing files
    if (handle->File->VaFs->Mode == VaFsMode_Write) {
        errno = ENOTSUP;
        return -1;
    }

    status = vafs_stream_lock(handle->File->VaFs->DataStream);
    if (status) {
        return -1;
    }

    status = vafs_stream_seek(
        handle->File->VaFs->DataStream,
        handle->File->Descriptor.Data.Index,
        handle->File->Descriptor.Data.Offset + handle->Position
    );
    if (status) {
        vafs_stream_unlock(handle->File->VaFs->DataStream);
        return -1;
    }

    read = vafs_stream_read(handle->File->VaFs->DataStream, buffer, size);
    vafs_stream_unlock(handle->File->VaFs->DataStream);
    
    return read;
}

size_t vafs_file_write(
    struct VaFsFileHandle* handle,
    void*                  buffer,
    size_t                 size)
{
    vafsblock_t block;
    uint32_t    offset;
    int         status;

    if (!handle || !buffer || size == 0) {
        errno = EINVAL;
        return -1;
    }

    // this is not valid when reading files
    if (handle->File->VaFs->Mode == VaFsMode_Read) {
        errno = ENOTSUP;
        return -1;
    }

    if (handle->State != VaFsFileState_Write) {
        status = vafs_stream_lock(handle->File->VaFs->DataStream);
        if (status) {
            return -1;
        }

        // Set current file state to writing, so the stream gets unlocked.
        handle->State = VaFsFileState_Write;
    }

    if (handle->File->Descriptor.Data.Offset == VA_FS_INVALID_OFFSET) {
        status = vafs_stream_position(handle->File->VaFs->DataStream, &block, &offset);
        if (status) {
            return -1;
        }
        handle->File->Descriptor.Data.Index = block;
        handle->File->Descriptor.Data.Offset = offset;
    }

    status = vafs_stream_write(handle->File->VaFs->DataStream, buffer, size);
    if (status) {
        return -1;
    }

    // add to filelength
    handle->File->Descriptor.FileLength += size;

    // add to overview
    handle->File->VaFs->Overview.TotalSizeUncompressed += size;

    return 0;
}
