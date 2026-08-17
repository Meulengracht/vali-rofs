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

#include <vafs/reader.h>
#include "private.h"

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

static struct VaFsDirectoryHandle* __create_handle(
    struct VaFsDirectory* directory)
{
    struct VaFsDirectoryHandle* handle;

    handle = malloc(sizeof(struct VaFsDirectoryHandle));
    if (!handle) {
        return NULL;
    }

    handle->Directory = directory;
    handle->Index     = 0;
    return handle;
}

int __vafs_directory_open_internal(
    struct VaFs*                 vafs,
    const char*                  path,
    struct VaFsDirectoryHandle** handleOut,
    int                          symlinkDepth)
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

    // Check symlink depth limit
    if (symlinkDepth > VAFS_SYMLINK_MAX_DEPTH) {
        VAFS_ERROR("__vafs_directory_open_internal: symlink depth limit exceeded (depth=%d, max=%d)\n",
            symlinkDepth, VAFS_SYMLINK_MAX_DEPTH);
        errno = ELOOP;
        return -1;
    }

    if (__vafs_is_root_path(path)) {
        *handleOut = __create_handle(vafs->RootDirectory);
        return 0;
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
            if (!pathBuffer) {
                VAFS_ERROR("__vafs_directory_open_internal: failed to allocate path buffer\n");
                errno = ENOMEM;
                return -1;
            }

            written = __vafs_resolve_symlink(pathBuffer, VAFS_PATH_MAX, path, previousPath - path, entry->Symlink->Target);
            if (written < 0) {
                VAFS_ERROR("__vafs_directory_open_internal: failed to resolve symlink %s\n", entry->Symlink->Target);
                free(pathBuffer);
                return -1;
            }

            // Restart resolution from the expanded target path so recursive
            // symlink chains share one depth limit and one normalization path.
            status = __vafs_directory_open_internal(vafs, pathBuffer, handleOut, symlinkDepth + 1);
            free(pathBuffer);
            return status;
        }

        if (entry->Type != VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
            errno = ENOTDIR;
            return -1;
        }

        if (remainingPath[0] == '\0') {
            // we found the directory
            *handleOut = __create_handle(entry->Directory);
            return 0;
        }

        currentDirectory = entry->Directory;
    } while (1);
    return -1;
}

int vafs_directory_open(
    struct VaFs*                 vafs,
    const char*                  path,
    struct VaFsDirectoryHandle** handleOut)
{
    return __vafs_directory_open_internal(vafs, path, handleOut, 0);
}

int vafs_directory_stat(
    struct VaFsDirectoryHandle* handle,
    struct VaFsMetadata*        metadata)
{
    if (handle == NULL || metadata == NULL) {
        errno = EINVAL;
        return -1;
    }

    return __vafs_directory_entry_stat(
        &(struct VaFsDirectoryEntry) {
            .Type = VA_FS_DESCRIPTOR_TYPE_DIRECTORY,
            .Directory = handle->Directory
        },
        metadata
    );
}

static size_t __directory_entry_count(
    struct VaFsDirectory* directory)
{
    if (directory->VaFs->Mode == VaFsMode_Read) {
        return ((struct VaFsDirectoryReader*)directory)->EntryCount;
    }
    return ((struct VaFsDirectoryWriter*)directory)->EntryCount;
}

int vafs_directory_read(
    struct VaFsDirectoryHandle* handle,
    struct VaFsEntry*           entryOut)
{
    struct VaFsDirectoryEntry* entry;
    struct VaFsMetadata        metadata;
    size_t                     count;
    size_t                     i;
    VAFS_INFO("vafs_directory_read(handle=%p)\n", handle);

    if (handle == NULL || entryOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    // make sure directory entries are loaded and indexed
    (void)__vafs_directory_entries(handle->Directory);

    count = __directory_entry_count(handle->Directory);
    VAFS_DEBUG("vafs_directory_read: locate index %i\n", handle->Index);
    if (count == 0 || (size_t)handle->Index >= count) {
        VAFS_INFO("vafs_directory_read: end of directory\n");
        errno = ENOENT;
        return -1;
    }

    // Directory iteration preserves the stored list order for compatibility.
    entry = __vafs_directory_entries(handle->Directory);
    if (entry == NULL) {
        return -1;
    }

    for (i = 0; i < (size_t)handle->Index; i++) {
        if (entry == NULL) {
            errno = ENOENT;
            return -1;
        }
        entry = entry->Link;
    }

    VAFS_DEBUG("vafs_directory_read: found entry %s\n",
        __vafs_directory_entry_name(entry));

    // we found an entry, move to next
    handle->Index++;

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

int vafs_directory_open_directory(
    struct VaFsDirectoryHandle*  handle,
    const char*                  name,
    struct VaFsDirectoryHandle** handleOut)
{
    struct VaFsDirectoryEntry* entry;
    char                       token[VAFS_NAME_MAX + 1];
    VAFS_DEBUG("vafs_directory_open_directory(handle=%p, name=%s, handleOut=%p)\n", handle, name, handleOut);

    if (handle == NULL || name == NULL || handleOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (handle->Directory->VaFs->Mode != VaFsMode_Read) {
        errno = EACCES;
        return -1;
    }

    // do this to verify the incoming name
    if (!__vafs_pathtoken(name, token, sizeof(token))) {
        errno = ENOENT;
        return -1;
    }

    // find the name in the directory
    entry = __vafs_directory_find_entry(handle->Directory, token);
    if (entry == NULL) {
        return -1;
    }

    if (entry->Type == VA_FS_DESCRIPTOR_TYPE_HARDLINK) {
        entry = __vafs_resolve_hardlink(handle->Directory->VaFs, entry);
        if (entry == NULL) {
            return -1;
        }
    }

    if (entry->Type != VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
        errno = ENOTDIR;
        return -1;
    }

    *handleOut = __create_handle(entry->Directory);
    return 0;
}

int vafs_directory_open_file(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    struct VaFsFileHandle**     handleOut)
{
    struct VaFsDirectoryEntry* entry;
    char                       token[VAFS_NAME_MAX + 1];
    VAFS_DEBUG("vafs_directory_open_file(name=%s)\n", name);

    if (handle == NULL || name == NULL || handleOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    // verify read mode
    if (handle->Directory->VaFs->Mode != VaFsMode_Read) {
        errno = EACCES;
        return -1;
    }

    // do this to verify the incoming name
    if (!__vafs_pathtoken(name, token, sizeof(token))) {
        errno = ENOENT;
        return -1;
    }

    // find the name in the directory
    entry = __vafs_directory_find_entry(handle->Directory, token);
    if (entry == NULL) {
        return -1;
    }

    if (entry->Type == VA_FS_DESCRIPTOR_TYPE_HARDLINK) {
        // File opens resolve alias entries before the file-type check so hardlinks
        // remain transparent to callers that just want file semantics.
        entry = __vafs_resolve_hardlink(handle->Directory->VaFs, entry);
        if (entry == NULL) {
            return -1;
        }
    }

    if (entry->Type != VA_FS_DESCRIPTOR_TYPE_FILE) {
        errno = ENFILE;
        return -1;
    }

    *handleOut = vafs_file_create_handle(entry->File);
    return 0;
}

int vafs_directory_read_symlink(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    const char**                targetOut)
{
    struct VaFsDirectoryEntry* entry;
    char                       token[VAFS_NAME_MAX + 1];
    VAFS_DEBUG("vafs_directory_read_symlink(name=%s)\n", name);

    if (handle == NULL || name == NULL || targetOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (handle->Directory->VaFs->Mode != VaFsMode_Read) {
        errno = EACCES;
        return -1;
    }

    // do this to verify the incoming name
    if (!__vafs_pathtoken(name, token, sizeof(token))) {
        errno = ENOENT;
        return -1;
    }

    // find the name in the directory
    VAFS_DEBUG("vafs_directory_read_symlink: locating %s\n", token);
    entry = __vafs_directory_find_entry(handle->Directory, token);
    if (entry == NULL) {
        return -1;
    }

    if (entry->Type == VA_FS_DESCRIPTOR_TYPE_HARDLINK) {
        // A hardlink may alias a symlink object, so resolve first and then apply
        // the symlink-type check to the canonical entry.
        entry = __vafs_resolve_hardlink(handle->Directory->VaFs, entry);
        if (entry == NULL) {
            return -1;
        }
    }
    
    if (entry->Type != VA_FS_DESCRIPTOR_TYPE_SYMLINK) {
        errno = EINVAL;
        return -1;
    }

    *targetOut = entry->Symlink->Target;
    return 0;
}
