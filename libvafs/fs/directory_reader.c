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
 * Vali Container Filesystem
 * - Contains the implementation of the Vali Container Filesystem.
 *   This filesystem is used to store the initrd of the kernel.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vafs/reader.h>
#include "../core/core.h"
#include "../stream/stream.h"
#include "directory.h"
#include "object.h"
#include "path.h"


struct VaFsDirectoryReaderHandle {
    struct VaFsDirectory* Directory;
    int                   Index;
    char*                 Path;
};

struct VaFsObjectReader {
    struct VaFsDirectoryEntry Entry;
    struct VaFsStreamReader*  Reader;
    uint64_t                  Position;
    char*                     Path;
    int                       FollowLinks;
};

void vafs_object_reader_close(
    struct VaFsObjectReader* reader);

static char* __join_reader_child_path(
    const char* parent,
    const char* name)
{
    char*  path;
    size_t parentLength;
    size_t nameLength;
    size_t separatorLength;

    if (parent == NULL || name == NULL) {
        errno = EINVAL;
        return NULL;
    }

    parentLength = strlen(parent);
    nameLength = strlen(name);
    separatorLength = (parentLength == 1 && parent[0] == '/') ? 0 : 1;
    if (parentLength + separatorLength + nameLength + 1 > VAFS_PATH_MAX) {
        errno = ENAMETOOLONG;
        return NULL;
    }

    path = malloc(parentLength + separatorLength + nameLength + 1);
    if (path == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    memcpy(path, parent, parentLength);
    if (separatorLength != 0) {
        path[parentLength] = '/';
    }
    memcpy(path + parentLength + separatorLength, name, nameLength);
    path[parentLength + separatorLength + nameLength] = '\0';
    return path;
}

static void __directory_entry_destroy(struct VaFsDirectoryEntry* entry)
{
    // Entry teardown fans back out through the concrete object type because
    // only some entry kinds carry nested children or xattr ownership.
    switch (entry->Type) {
        case VA_FS_DESCRIPTOR_TYPE_DIRECTORY:
            vafs_directory_destroy(entry->Directory);
            break;
        case VA_FS_DESCRIPTOR_TYPE_FILE:
            vafs_file_destroy(entry->File);
            break;
        case VA_FS_DESCRIPTOR_TYPE_SYMLINK:
            vafs_symlink_destroy(entry->Symlink);
            break;
        case VA_FS_DESCRIPTOR_TYPE_SPECIAL:
            __vafs_xattr_set_destroy(entry->Special->Xattrs);
            free((void*)entry->Special->Name);
            free(entry->Special);
            break;
        case VA_FS_DESCRIPTOR_TYPE_HARDLINK:
            free((void*)entry->Hardlink->Name);
            free(entry->Hardlink);
            break;
    }
    free(entry);
}

static void __cleanup_directory_entries(struct VaFsDirectoryEntry* entries)
{
    struct VaFsDirectoryEntry* i = entries;
    while (i) {
        // Destruction frees the current node, so capture the next link first.
        struct VaFsDirectoryEntry* next = i->Link;
        __directory_entry_destroy(i);
        i = next;
    }
}

static void __directory_reader_destroy(struct VaFsDirectoryReader* reader)
{
    // Reader teardown releases fully materialized entries, any derived lookup
    // indexes, and finally the private descriptor-stream cursor.
    __cleanup_directory_entries(reader->Entries);
    __directory_reader_index_delete(reader);
    vafs_stream_reader_close(reader->Reader);
}

static void __directory_writer_destroy(struct VaFsDirectoryWriter* writer)
{
    // Writer teardown mirrors reader teardown, minus the descriptor-stream
    // cursor that only exists for lazily materialized read-mode directories.
    __cleanup_directory_entries(writer->Entries);
    __directory_writer_index_delete(writer);
}

struct VaFsDirectoryEntry* __vafs_directory_find_entry(
    struct VaFsDirectory* directory,
    const char*           token)
{
    struct VaFsDirectoryEntry* entry;

    if (directory == NULL || token == NULL) {
        errno = EINVAL;
        return NULL;
    }

    if (directory->VaFs->Mode == VaFsMode_Write) {
        // Writer-side lookups stay on the linked list so creation semantics are unchanged.
        entry = __vafs_directory_entries(directory);
        while (entry != NULL) {
            if (!strcmp(__vafs_directory_entry_name(entry), token)) {
                return entry;
            }
            entry = entry->Link;
        }

        errno = ENOENT;
        return NULL;
    }
    
    // Read-side lookups pay the one-time materialization cost first and then
    // stay on the indexed/cache-backed lookup path for the rest of the image lifetime.
    if (__vafs_directory_entries(directory) == NULL) {
        return NULL;
    }
    return __vafs_directory_get(directory, token);
}

static struct VaFsDirectoryEntry* __find_entry_by_object_id(
    struct VaFsDirectory* directory,
    uint64_t              objectId)
{
    struct VaFsDirectoryEntry* entry;
    struct VaFsMetadata        metadata;

    // Hardlink resolution stays tree-based on purpose so readers and writers
    // agree on one source of truth instead of maintaining a separate object-id
    // index that could drift from the materialized directory tree.
    entry = __vafs_directory_entries(directory);
    while (entry != NULL) {
        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
            // Recurse through real directories only; hardlink aliases are not
            // separate object-id owners and would just add duplicate paths.
            struct VaFsDirectoryEntry* nested = __find_entry_by_object_id(entry->Directory, objectId);
            if (nested != NULL) {
                return nested;
            }
        } else if (entry->Type != VA_FS_DESCRIPTOR_TYPE_HARDLINK) {
            if (__vafs_directory_entry_stat(entry, &metadata) == 0 &&
                (metadata.Mask & VaFsMetadataMask_ObjectId) != 0 &&
                metadata.ObjectId == objectId) {
                return entry;
            }
        }
        entry = entry->Link;
    }
    return NULL;
}

struct VaFsDirectoryEntry* __vafs_resolve_hardlink(
    struct VaFs*            vafs,
    struct VaFsDirectoryEntry* entry)
{
    if (vafs == NULL || entry == NULL) {
        errno = EINVAL;
        return NULL;
    }

    if (entry->Type != VA_FS_DESCRIPTOR_TYPE_HARDLINK) {
        return entry;
    }

    if (entry->Hardlink->Descriptor.ObjectId == 0) {
        errno = ENOENT;
        return NULL;
    }

    // Resolve through the live directory tree each time so aliases always see
    // the canonical entry even after later metadata mutations during image build.
    entry = __find_entry_by_object_id(vafs->RootDirectory, entry->Hardlink->Descriptor.ObjectId);
    if (entry == NULL) {
        errno = ENOENT;
        return NULL;
    }
    return entry;
}

void vafs_directory_destroy(struct VaFsDirectory* directory)
{
    if (directory == NULL) {
        return;
    }

    // Reader and writer directories own different supporting state, so teardown
    // follows the active mode instead of forcing both sides to carry the same
    // lifetime rules.
    if (directory->VaFs->Mode == VaFsMode_Read) {
        __directory_reader_destroy((struct VaFsDirectoryReader*)directory);
    } else if (directory->VaFs->Mode == VaFsMode_Write) {
        __directory_writer_destroy((struct VaFsDirectoryWriter*)directory);
    }

    __vafs_xattr_set_destroy(directory->Xattrs);

    // free common resources
    free((void*)directory->Name);
    free(directory);
}

const char* __vafs_directory_entry_name(
    struct VaFsDirectoryEntry* entry)
{
    if (entry->Type == VA_FS_DESCRIPTOR_TYPE_FILE) {
        return entry->File->Name;
    } else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
        return entry->Directory->Name;
    } else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_SYMLINK) {
        return entry->Symlink->Name;
    } else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_SPECIAL) {
        return entry->Special->Name;
    } else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_HARDLINK) {
        return entry->Hardlink->Name;
    }
    return NULL;
}

static struct VaFsDirectoryReader* __create_directory_reader_handle(
    struct VaFsDirectory* directory,
    const char*           path)
{
    struct VaFsDirectoryReaderHandle* handle;

    handle = malloc(sizeof(struct VaFsDirectoryReaderHandle));
    if (handle == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    handle->Directory = directory;
    handle->Index = 0;
    if (path != NULL) {
        handle->Path = strdup(path);
        if (handle->Path == NULL) {
            free(handle);
            errno = ENOMEM;
            return NULL;
        }
    }
    return (struct VaFsDirectoryReader*)handle;
}

static struct VaFsObjectReader* __create_object_reader_handle(
    struct VaFsDirectoryEntry* entry,
    const char*                path,
    int                        followLinks)
{
    struct VaFsObjectReader* reader;

    if (entry == NULL) {
        errno = EINVAL;
        return NULL;
    }

    reader = calloc(1, sizeof(struct VaFsObjectReader));
    if (reader == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    reader->Entry = *entry;
    reader->FollowLinks = followLinks;
    if (path != NULL) {
        reader->Path = strdup(path);
        if (reader->Path == NULL) {
            free(reader);
            errno = ENOMEM;
            return NULL;
        }
    }
    return reader;
}

static struct VaFs* __object_reader_vafs(
    struct VaFsObjectReader* reader)
{
    if (reader == NULL) {
        errno = EINVAL;
        return NULL;
    }

    switch (reader->Entry.Type) {
        case VA_FS_DESCRIPTOR_TYPE_FILE:
            return reader->Entry.File->VaFs;
        case VA_FS_DESCRIPTOR_TYPE_DIRECTORY:
            return reader->Entry.Directory->VaFs;
        case VA_FS_DESCRIPTOR_TYPE_SYMLINK:
            return reader->Entry.Symlink->VaFs;
        case VA_FS_DESCRIPTOR_TYPE_SPECIAL:
            return reader->Entry.Special->VaFs;
        default:
            errno = EINVAL;
            return NULL;
    }
}

static int __vafs_object_reader_open_internal(
    struct VaFs*              vafs,
    const char*               path,
    enum VaFsLookupFlags      flags,
    struct VaFsObjectReader** readerOut,
    int                       symlinkDepth)
{
    struct VaFsDirectory*      currentDirectory;
    struct VaFsDirectoryEntry* entry;
    const char*                remainingPath = path;
    char                       token[VAFS_NAME_MAX + 1];

    if (vafs == NULL || path == NULL || readerOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (symlinkDepth > VAFS_SYMLINK_MAX_DEPTH) {
        errno = ELOOP;
        return -1;
    }

    if (__vafs_is_root_path(path)) {
        struct VaFsDirectoryEntry rootEntry;

        memset(&rootEntry, 0, sizeof(rootEntry));
        rootEntry.Type = VA_FS_DESCRIPTOR_TYPE_DIRECTORY;
        rootEntry.Directory = vafs->RootDirectory;
        *readerOut = __create_object_reader_handle(&rootEntry, "/", 1);
        return (*readerOut == NULL) ? -1 : 0;
    }

    currentDirectory = vafs->RootDirectory;
    do {
        const char* previousPath = remainingPath;
        int         charsConsumed = __vafs_pathtoken(remainingPath, token, sizeof(token));

        if (!charsConsumed) {
            break;
        }
        remainingPath += charsConsumed;

        entry = __vafs_directory_find_entry(currentDirectory, token);
        if (entry == NULL) {
            return -1;
        }

        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_HARDLINK) {
            entry = __vafs_resolve_hardlink(vafs, entry);
            if (entry == NULL) {
                return -1;
            }
        }

        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
            if (remainingPath[0] == '\0') {
                *readerOut = __create_object_reader_handle(entry, path, 1);
                return (*readerOut == NULL) ? -1 : 0;
            }

            currentDirectory = entry->Directory;
            continue;
        }

        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_SYMLINK) {
            char* pathBuffer;
            int   written;
            int   status;

            if ((flags & VaFsLookup_NoFollow) != 0 && remainingPath[0] == '\0') {
                *readerOut = __create_object_reader_handle(entry, path, 0);
                return (*readerOut == NULL) ? -1 : 0;
            }

            pathBuffer = malloc(VAFS_PATH_MAX);
            if (pathBuffer == NULL) {
                errno = ENOMEM;
                return -1;
            }

            written = __vafs_resolve_symlink(pathBuffer, VAFS_PATH_MAX, path, previousPath - path, entry->Symlink->Target);
            if (written < 0) {
                free(pathBuffer);
                return -1;
            }

            status = __vafs_object_reader_open_internal(vafs, pathBuffer, flags, readerOut, symlinkDepth + 1);
            free(pathBuffer);
            return status;
        }

        if (remainingPath[0] != '\0') {
            errno = ENOTDIR;
            return -1;
        }

        *readerOut = __create_object_reader_handle(entry, path, 1);
        return (*readerOut == NULL) ? -1 : 0;
    } while (1);

    errno = ENOENT;
    return -1;
}

static int __vafs_directory_reader_open_internal(
    struct VaFs*                  vafs,
    const char*                   path,
    enum VaFsLookupFlags          flags,
    struct VaFsDirectoryReader**  readerOut,
    int                           symlinkDepth)
{
    struct VaFsDirectory*      currentDirectory;
    struct VaFsDirectoryEntry* entry;
    const char*                remainingPath = path;
    char                       token[VAFS_NAME_MAX + 1];

    if (vafs == NULL || path == NULL || readerOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Check symlink depth limit
    if (symlinkDepth > VAFS_SYMLINK_MAX_DEPTH) {
        VAFS_ERROR("__vafs_directory_reader_open_internal: symlink depth limit exceeded (depth=%d, max=%d)\n",
            symlinkDepth, VAFS_SYMLINK_MAX_DEPTH);
        errno = ELOOP;
        return -1;
    }

    if (__vafs_is_root_path(path)) {
        *readerOut = __create_directory_reader_handle(vafs->RootDirectory, "/");
        return (*readerOut == NULL) ? -1 : 0;
    }

    currentDirectory = vafs->RootDirectory;
    do {
        const char* previousPath = remainingPath;
        int charsConsumed = __vafs_pathtoken(remainingPath, token, sizeof(token));
        if (!charsConsumed) {
            break;
        }

        // Walk one component at a time so symlink expansion can restart from a
        // normalized intermediate path instead of trying to rewrite the whole traversal state.
        remainingPath += charsConsumed;

        entry = __vafs_directory_find_entry(currentDirectory, token);
        if (entry == NULL) {
            return -1;
        }

        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_HARDLINK) {
            // Directory traversal treats hardlinks as aliases immediately so later
            // type checks and symlink handling see the canonical backing entry.
            entry = __vafs_resolve_hardlink(vafs, entry);
            if (entry == NULL) {
                return -1;
            }
        }

        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_SYMLINK) {
            char* pathBuffer = malloc(VAFS_PATH_MAX);
            int   written;
            int   status;

            if ((flags & VaFsLookup_NoFollow) != 0 && remainingPath[0] == '\0') {
                errno = ENOTDIR;
                return -1;
            }

            if (!pathBuffer) {
                VAFS_ERROR("__vafs_directory_reader_open_internal: failed to allocate path buffer\n");
                errno = ENOMEM;
                return -1;
            }

            written = __vafs_resolve_symlink(pathBuffer, VAFS_PATH_MAX, path, previousPath - path, entry->Symlink->Target);
            if (written < 0) {
                VAFS_ERROR("__vafs_directory_reader_open_internal: failed to resolve symlink %s\n", entry->Symlink->Target);
                free(pathBuffer);
                return -1;
            }

            // Restart resolution from the expanded target path so recursive
            // symlink chains share one depth limit and one normalization path.
            status = __vafs_directory_reader_open_internal(vafs, pathBuffer, flags, readerOut, symlinkDepth + 1);
            free(pathBuffer);
            return status;
        }

        if (entry->Type != VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
            errno = ENOTDIR;
            return -1;
        }

        if (remainingPath[0] == '\0') {
            // we found the directory
            *readerOut = __create_directory_reader_handle(entry->Directory, path);
            return (*readerOut == NULL) ? -1 : 0;
        }

        currentDirectory = entry->Directory;
    } while (1);
    errno = ENOENT;
    return -1;
}

int vafs_directory_reader_open(
    struct VaFs*                 vafs,
    const char*                  path,
    enum VaFsLookupFlags         flags,
    struct VaFsDirectoryReader** readerOut)
{
    return __vafs_directory_reader_open_internal(vafs, path, flags, readerOut, 0);
}

static size_t __directory_entry_count(
    struct VaFsDirectory* directory)
{
    if (directory->VaFs->Mode == VaFsMode_Read) {
        return ((struct VaFsDirectoryReader*)directory)->EntryCount;
    }
    return ((struct VaFsDirectoryWriter*)directory)->EntryCount;
}

int vafs_directory_reader_next(
    struct VaFsDirectoryReader* handle,
    struct VaFsEntry*           entryOut)
{
    struct VaFsDirectoryReaderHandle* readerHandle = (struct VaFsDirectoryReaderHandle*)handle;
    struct VaFsDirectoryEntry* entry;
    struct VaFsMetadata        metadata;
    size_t                     count;
    size_t                     i;
    VAFS_INFO("vafs_directory_reader_next(handle=%p)\n", handle);

    if (handle == NULL || entryOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    // make sure directory entries are loaded and indexed
    (void)__vafs_directory_entries(readerHandle->Directory);

    count = __directory_entry_count(readerHandle->Directory);
    VAFS_DEBUG("vafs_directory_reader_next: locate index %i\n", readerHandle->Index);
    if (count == 0 || (size_t)readerHandle->Index >= count) {
        VAFS_INFO("vafs_directory_reader_next: end of directory\n");
        errno = ENOENT;
        return -1;
    }

    // Directory iteration preserves the stored list order for compatibility.
    entry = __vafs_directory_entries(readerHandle->Directory);
    if (entry == NULL) {
        return -1;
    }

    for (i = 0; i < (size_t)readerHandle->Index; i++) {
        if (entry == NULL) {
            errno = ENOENT;
            return -1;
        }
        entry = entry->Link;
    }

    VAFS_DEBUG("vafs_directory_read: found entry %s\n",
        __vafs_directory_entry_name(entry));

    // we found an entry, move to next
    readerHandle->Index++;

    if (__vafs_directory_entry_stat(entry, &metadata) != 0) {
        return -1;
    }

    // initialize the entry structure
    entryOut->Name = __vafs_directory_entry_name(entry);
    // Enumeration still needs to expose that this name is an alias even though
    // stat/open semantics resolve to the shared target object.
    entryOut->Type = (entry->Type == VA_FS_DESCRIPTOR_TYPE_HARDLINK) ? VaFsEntryType_Hardlink : metadata.Type;
    entryOut->ObjectId = metadata.ObjectId;
    entryOut->MetadataMask = metadata.Mask;
    return 0;
}

int vafs_directory_reader_open_object_in(
    struct VaFsDirectoryReader* reader,
    const char*                 name,
    struct VaFsObjectReader**   readerOut)
{
    struct VaFsDirectoryReaderHandle* readerHandle = (struct VaFsDirectoryReaderHandle*)reader;
    struct VaFsDirectoryEntry* entry;
    char                       token[VAFS_NAME_MAX + 1];
    VAFS_DEBUG("vafs_directory_reader_open_object_in(reader=%p, name=%s, readerOut=%p)\n", reader, name, readerOut);

    if (reader == NULL || name == NULL || readerOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (readerHandle->Directory->VaFs->Mode != VaFsMode_Read) {
        errno = EACCES;
        return -1;
    }

    // do this to verify the incoming name
    if (!__vafs_pathtoken(name, token, sizeof(token))) {
        errno = ENOENT;
        return -1;
    }

    // find the name in the directory
    entry = __vafs_directory_find_entry(readerHandle->Directory, token);
    if (entry == NULL) {
        return -1;
    }

    if (entry->Type == VA_FS_DESCRIPTOR_TYPE_HARDLINK) {
        entry = __vafs_resolve_hardlink(readerHandle->Directory->VaFs, entry);
        if (entry == NULL) {
            return -1;
        }
    }

    char* childPath = __join_reader_child_path(readerHandle->Path, token);
    if (childPath == NULL) {
        return -1;
    }

    *readerOut = __create_object_reader_handle(entry, childPath, 0);
    free(childPath);
    return (*readerOut == NULL) ? -1 : 0;
}

int vafs_directory_reader_open_directory_in(
    struct VaFsDirectoryReader*  reader,
    const char*                  name,
    struct VaFsDirectoryReader** readerOut)
{
    struct VaFsObjectReader* objectReader;
    int                      status;

    if (reader == NULL || name == NULL || readerOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = vafs_directory_reader_open_object_in(reader, name, &objectReader);
    if (status != 0) {
        return status;
    }

    if (objectReader->Entry.Type != VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
        vafs_object_reader_close(objectReader);
        errno = ENOTDIR;
        return -1;
    }

    *readerOut = __create_directory_reader_handle(objectReader->Entry.Directory, objectReader->Path);
    vafs_object_reader_close(objectReader);
    return (*readerOut == NULL) ? -1 : 0;
}

int vafs_directory_reader_close(
    struct VaFsDirectoryReader* handle)
{
    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }

    free(((struct VaFsDirectoryReaderHandle*)handle)->Path);
    free(handle);
    return 0;
}

int vafs_object_reader_open(
    struct VaFs*              vafs,
    const char*               path,
    enum VaFsLookupFlags      flags,
    struct VaFsObjectReader** readerOut)
{
    return __vafs_object_reader_open_internal(vafs, path, flags, readerOut, 0);
}

void vafs_object_reader_close(
    struct VaFsObjectReader* reader)
{
    if (reader == NULL) {
        return;
    }

    if (reader->Reader != NULL) {
        vafs_stream_reader_close(reader->Reader);
    }
    free(reader->Path);
    free(reader);
}

uint64_t vafs_object_reader_length(
    struct VaFsObjectReader* reader)
{
    if (reader == NULL) {
        errno = EINVAL;
        return UINT64_MAX;
    }

    switch (reader->Entry.Type) {
        case VA_FS_DESCRIPTOR_TYPE_FILE:
            return reader->Entry.File->Descriptor.FileLength;
        case VA_FS_DESCRIPTOR_TYPE_DIRECTORY:
            (void)__vafs_directory_entries(reader->Entry.Directory);
            return __directory_entry_count(reader->Entry.Directory);
        case VA_FS_DESCRIPTOR_TYPE_SYMLINK:
            return strlen(reader->Entry.Symlink->Target);
        case VA_FS_DESCRIPTOR_TYPE_SPECIAL:
            return 0;
        default:
            errno = EINVAL;
            return UINT64_MAX;
    }
}

uint64_t vafs_object_reader_read(
    struct VaFsObjectReader* reader,
    void*                    buffer,
    uint64_t                 length)
{
    uint64_t objectLength;
    uint64_t bytesRemaining;
    size_t   bytesRead;

    if (reader == NULL || buffer == NULL) {
        errno = EINVAL;
        return UINT64_MAX;
    }

    objectLength = vafs_object_reader_length(reader);
    if (objectLength == UINT64_MAX) {
        return UINT64_MAX;
    }
    if (reader->Position > objectLength) {
        errno = EINVAL;
        return UINT64_MAX;
    }

    bytesRemaining = objectLength - reader->Position;
    if (length > bytesRemaining) {
        length = bytesRemaining;
    }
    if (length == 0) {
        return 0;
    }
    if (length > SIZE_MAX) {
        length = SIZE_MAX;
    }

    switch (reader->Entry.Type) {
        case VA_FS_DESCRIPTOR_TYPE_FILE:
            if (reader->Reader == NULL &&
                vafs_stream_reader_open(reader->Entry.File->VaFs->DataStream, &reader->Reader) != 0) {
                return UINT64_MAX;
            }

            if (vafs_stream_reader_seek(
                    reader->Reader,
                    reader->Entry.File->Descriptor.Data.Index,
                    reader->Entry.File->Descriptor.Data.Offset + (uint32_t)reader->Position
                ) != 0) {
                return UINT64_MAX;
            }

            if (vafs_stream_reader_read(reader->Reader, buffer, (size_t)length, &bytesRead) != 0) {
                return UINT64_MAX;
            }
            reader->Position += bytesRead;
            return bytesRead;
        case VA_FS_DESCRIPTOR_TYPE_SYMLINK:
            memcpy(buffer, reader->Entry.Symlink->Target + reader->Position, (size_t)length);
            reader->Position += length;
            return length;
        case VA_FS_DESCRIPTOR_TYPE_DIRECTORY:
            errno = EISDIR;
            return UINT64_MAX;
        case VA_FS_DESCRIPTOR_TYPE_SPECIAL:
            return 0;
        default:
            errno = EINVAL;
            return UINT64_MAX;
    }
}

int vafs_object_reader_seek(
    struct VaFsObjectReader* reader,
    int64_t                  offset,
    int                      whence)
{
    uint64_t objectLength;
    int64_t  base;
    int64_t  position;

    if (reader == NULL) {
        errno = EINVAL;
        return -1;
    }

    objectLength = vafs_object_reader_length(reader);
    if (objectLength == UINT64_MAX || objectLength > INT64_MAX || reader->Position > INT64_MAX) {
        errno = EINVAL;
        return -1;
    }

    switch (whence) {
        case SEEK_SET:
            base = 0;
            break;
        case SEEK_CUR:
            base = (int64_t)reader->Position;
            break;
        case SEEK_END:
            base = (int64_t)objectLength;
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    if ((offset > 0 && base > INT64_MAX - offset) ||
        (offset < 0 && base < INT64_MIN - offset)) {
        errno = EOVERFLOW;
        return -1;
    }

    position = base + offset;
    if (position < 0 || (uint64_t)position > objectLength) {
        errno = EINVAL;
        return -1;
    }

    reader->Position = (uint64_t)position;
    return 0;
}

int vafs_object_reader_stat(
    struct VaFsObjectReader* reader,
    struct VaFsMetadata*     metadataOut)
{
    if (reader == NULL || metadataOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    return __vafs_directory_entry_stat(&reader->Entry, metadataOut);
}

int vafs_object_reader_listxattr(
    struct VaFsObjectReader* handle,
    char*                    buffer,
    size_t                   bufferSize,
    size_t*                  bytesWrittenOut)
{
    struct VaFs* vafs;

    if (handle == NULL || handle->Path == NULL || bytesWrittenOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    vafs = __object_reader_vafs(handle);
    if (vafs == NULL) {
        return -1;
    }

    return __vafs_path_listxattr(
        vafs,
        handle->Path,
        handle->FollowLinks,
        buffer,
        bufferSize,
        bytesWrittenOut
    );
}

int vafs_object_reader_getxattr(
    struct VaFsObjectReader* handle,
    const char*              name,
    void*                    value,
    size_t                   valueSize,
    size_t*                  bytesWrittenOut)
{
    struct VaFs* vafs;

    if (handle == NULL || handle->Path == NULL || bytesWrittenOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    vafs = __object_reader_vafs(handle);
    if (vafs == NULL) {
        return -1;
    }

    return __vafs_path_getxattr(
        vafs,
        handle->Path,
        handle->FollowLinks,
        name,
        value,
        valueSize,
        bytesWrittenOut
    );
}
