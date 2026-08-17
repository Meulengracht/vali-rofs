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

enum VaFsFileState {
    VaFsFileState_Open,
    VaFsFileState_Read,
    VaFsFileState_Write
};

struct VaFsFileHandle {
    struct VaFsFile*   File;
    enum VaFsFileState State;
    struct VaFsStreamReader* Reader;
    uint32_t           Position;
};

static int __ensure_file_reader(
    struct VaFsFileHandle* handle)
{
    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (handle->Reader != NULL) {
        return 0;
    }

    // Read handles defer their private stream cursor until the first data
    // access so open/close-heavy callers do not pay for a block buffer they
    // never touch.
    return vafs_stream_reader_open(handle->File->VaFs->DataStream, &handle->Reader);
}


int __vafs_file_open_internal(
    struct VaFs*            vafs,
    const char*             path,
    struct VaFsFileHandle** handleOut,
    int                     symlinkDepth)
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

    // Resolve the path one token at a time until it terminates in a file.
    // Directory edges advance traversal, symlinks recurse with a depth budget,
    // and regular files must be the final component.

    // Check symlink depth limit
    if (symlinkDepth > VAFS_SYMLINK_MAX_DEPTH) {
        VAFS_ERROR("__vafs_file_open_internal: symlink depth limit exceeded (depth=%d, max=%d)\n",
            symlinkDepth, VAFS_SYMLINK_MAX_DEPTH);
        errno = ELOOP;
        return -1;
    }

    if (__vafs_is_root_path(path)) {
        errno = EISDIR;
        return -1;
    }

    // Path resolution walks one directory boundary at a time. Each iteration
    // resolves a single token, then either descends, follows a symlink, or
    // terminates on the final file object. Keeping these phases explicit makes
    // the hardlink/symlink semantics easier to review than a single monolithic
    // block of nested conditionals.
    currentDirectory = vafs->RootDirectory;
    do {
        const char* previousPath = remainingPath;
        int charsConsumed = __vafs_pathtoken(remainingPath, token, sizeof(token));
        if (!charsConsumed) {
            // Exhausting the token stream without returning means resolution
            // never landed on a concrete file entry.
            break;
        }
        remainingPath += charsConsumed;

        entry = __vafs_directory_find_entry(currentDirectory, token);
        if (entry == NULL) {
            // Path resolution stops on the first missing component.
            return -1;
        }

        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_HARDLINK) {
            // Resolve aliases before any type-specific branching so a hardlink
            // to a symlink inherits normal symlink traversal instead of acting
            // like a separate terminal file type.
            entry = __vafs_resolve_hardlink(vafs, entry);
            if (entry == NULL) {
                return -1;
            }
        }

        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
            if (remainingPath[0] == '\0') {
                // Opening a directory through the file API is always an error,
                // even if the path resolved successfully.
                errno = EISDIR;
                return -1;
            }

            // Intermediate directory tokens simply move the traversal root.
            currentDirectory = entry->Directory;
            continue;
        }

        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_SYMLINK) {
            char* pathBuffer = malloc(VAFS_PATH_MAX);
            int   written;
            int   status;
            if (!pathBuffer) {
                VAFS_ERROR("__vafs_file_open_internal: failed to allocate path buffer\n");
                errno = ENOMEM;
                return -1;
            }

            // Rebuild the remaining path through the symlink target, then let
            // the same resolver continue with an incremented depth budget.
            written = __vafs_resolve_symlink(pathBuffer, VAFS_PATH_MAX, path, previousPath - path, entry->Symlink->Target);
            if (written < 0) {
                VAFS_ERROR("__vafs_file_open_internal: failed to resolve symlink %s\n", entry->Symlink->Target);
                free(pathBuffer);
                return -1;
            }

            status = __vafs_file_open_internal(vafs, pathBuffer, handleOut, symlinkDepth + 1);
            free(pathBuffer);
            return status;
        }

        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_FILE) {
            if (remainingPath[0] != '\0') {
                // Regular files terminate traversal; callers cannot descend
                // through a file into more path components.
                errno = ENOTDIR;
                return -1;
            }

            *handleOut = vafs_file_create_handle(entry->File);
            return 0;
        }

        // Unknown descriptor types are treated as missing entries so callers do
        // not continue through malformed metadata.
        errno = ENOENT;
        return -1;
    } while (1);
    errno = ENOENT;
    return -1;
}

int vafs_file_open(
    struct VaFs*            vafs,
    const char*             path,
    struct VaFsFileHandle** handleOut)
{
    return __vafs_file_open_internal(vafs, path, handleOut, 0);
}

struct VaFsFileHandle* vafs_file_create_handle(
    struct VaFsFile* fileEntry)
{
    struct VaFsFileHandle* handle;

    // Read handles still use a private stream cursor, but they create it on
    // first data access so open/close-heavy workloads avoid a block-buffer
    // allocation when the handle never actually reads.
    
    handle = (struct VaFsFileHandle*)malloc(sizeof(struct VaFsFileHandle));
    if (!handle) {
        errno = ENOMEM;
        return NULL;
    }
    
    handle->File = fileEntry;
    handle->Reader = NULL;
    handle->Position = 0;
    handle->State = VaFsFileState_Open;
    
    return handle;
}

void vafs_file_destroy(
    struct VaFsFile* file)
{
    if (file == NULL) {
        return;
    }

    __vafs_xattr_set_destroy(file->Xattrs);
    free((void*)file->Name);
    free(file);
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

    // Close whichever read path resources were actually needed. Handles that
    // never reached a data read stay allocation-free on the read side.
    if (handle->Reader != NULL) {
        vafs_stream_reader_close(handle->Reader);
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

int vafs_file_stat(
    struct VaFsFileHandle* handle,
    struct VaFsMetadata*   metadata)
{
    if (!handle || metadata == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (!handle->File->StatCached) {
        vafs_metadata_initialize(&handle->File->Stat);
    }

    handle->File->Stat.Type = VaFsEntryType_File;
    handle->File->Stat.Size = handle->File->Descriptor.FileLength;
    handle->File->Stat.Mask |= VaFsMetadataMask_Type | VaFsMetadataMask_Size;
    if ((handle->File->Stat.Mask & VaFsMetadataMask_LinkCount) == 0) {
        handle->File->Stat.LinkCount = 1;
        handle->File->Stat.Mask |= VaFsMetadataMask_LinkCount;
    }

    handle->File->StatCached = 1;
    *metadata = handle->File->Stat;
    return 0;
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

    // File seeks only update the logical file position. The private stream
    // reader is repositioned lazily on the next read.

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
    size_t   read;
    int      status;
    uint32_t bytesRemaining;
    uint32_t readOffset;

    if (!handle) {
        errno = EINVAL;
        return 0;
    }

    // Translate the file-relative position into the file's backing stream
    // extent, then drive this handle's private stream reader from that point.

    // this is not valid when writing files
    if (handle->File->VaFs->Mode == VaFsMode_Write) {
        errno = ENOTSUP;
        return 0;
    }

    // Validate file position doesn't exceed file length
    if (handle->Position > handle->File->Descriptor.FileLength) {
        VAFS_ERROR("vafs_file_read: position %u exceeds file length %u\n",
            handle->Position, handle->File->Descriptor.FileLength);
        errno = EINVAL;
        return 0;
    }

    // Calculate bytes remaining in file
    bytesRemaining = handle->File->Descriptor.FileLength - handle->Position;

    // Clamp read size to remaining bytes
    if (size > bytesRemaining) {
        size = bytesRemaining;
    }

    if (size == 0) {
        // Zero-length and EOF reads do not touch the reader cursor.
        return 0;
    }

    // Check for integer overflow in offset calculation
    readOffset = handle->File->Descriptor.Data.Offset + handle->Position;
    if (readOffset < handle->File->Descriptor.Data.Offset) {
        VAFS_ERROR("vafs_file_read: integer overflow in offset calculation\n");
        errno = EINVAL;
        return 0;
    }

    if (__ensure_file_reader(handle) != 0) {
        return 0;
    }

    status = vafs_stream_reader_seek(
        handle->Reader,
        handle->File->Descriptor.Data.Index,
        readOffset
    );
    if (status) {
        return 0;
    }

    status = vafs_stream_reader_read(handle->Reader, buffer, size, &read);

    if (status) {
        return 0;
    }

    handle->Position = handle->Position + (uint32_t)read;
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
        return (size_t)-1;
    }

    // Write mode keeps one ordered staging stream per image. The handle grabs
    // that shared append path on first write, snapshots its starting position,
    // and then keeps extending the same on-disk extent.

    // this is not valid when reading files
    if (handle->File->VaFs->Mode == VaFsMode_Read) {
        errno = ENOTSUP;
        return (size_t)-1;
    }

    if (handle->State != VaFsFileState_Write) {
        status = vafs_stream_lock(handle->File->VaFs->DataStream);
        if (status) {
            return (size_t)-1;
        }

        // Set current file state to writing, so the stream gets unlocked.
        handle->State = VaFsFileState_Write;
    }

    if (handle->File->Descriptor.Data.Offset == VA_FS_INVALID_OFFSET) {
        // First write captures the file's starting block position so later
        // reads know where this file begins inside the shared data stream.
        status = vafs_stream_position(handle->File->VaFs->DataStream, &block, &offset);
        if (status) {
            return (size_t)-1;
        }
        handle->File->Descriptor.Data.Index = block;
        handle->File->Descriptor.Data.Offset = offset;
    }

    if (size > (size_t)(UINT32_MAX - handle->File->Descriptor.FileLength)) {
        errno = EFBIG;
        return (size_t)-1;
    }

    status = vafs_stream_write(handle->File->VaFs->DataStream, buffer, size);
    if (status) {
        return (size_t)-1;
    }

    // add to filelength
    handle->File->Descriptor.FileLength += (uint32_t)size;
    handle->File->Stat.Size = handle->File->Descriptor.FileLength;
    handle->File->Stat.Mask |= VaFsMetadataMask_Size;

    // add to overview
    handle->File->VaFs->Overview.TotalSizeUncompressed += size;

    return 0;
}
