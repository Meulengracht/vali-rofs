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

#include <vafs/builder.h>
#include "private.h"

struct VaFsDirectoryBuilder {
    struct VaFsDirectory* Directory;
    int                   Index;
};

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

static int __prepare_metadata_for_create(
    const struct VaFsMetadata* metadata,
    enum VaFsEntryType         expectedType,
    uint64_t                   size,
    struct VaFsMetadata*       preparedOut)
{
    if (metadata == NULL || preparedOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if ((metadata->Mask & VaFsMetadataMask_Mode) == 0) {
        errno = EINVAL;
        return -1;
    }

    if ((metadata->Mask & VaFsMetadataMask_Type) != 0 &&
        metadata->Type != VaFsEntryType_Unknown &&
        metadata->Type != expectedType) {
        errno = EINVAL;
        return -1;
    }

    // The create verb still decides the concrete entry kind, but once that is
    // known we preserve the caller's richer metadata instead of collapsing it
    // back to permission bits before serialization.
    *preparedOut = *metadata;
    vafs_metadata_set_mode(preparedOut, expectedType, preparedOut->Mode & 07777u);
    __finalize_entry_metadata(preparedOut, expectedType, size);
    return 0;
}

static int __prepare_special_metadata_for_create(
    const struct VaFsMetadata* metadata,
    struct VaFsMetadata*       preparedOut)
{
    enum VaFsEntryType entryType = VaFsEntryType_Unknown;

    if (metadata == NULL || preparedOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if ((metadata->Mask & VaFsMetadataMask_Mode) == 0) {
        errno = EINVAL;
        return -1;
    }

    // Special creation accepts either an explicit entry type or a host-style
    // mode-only description, so normalize both forms into one concrete subtype first.
    if ((metadata->Mask & VaFsMetadataMask_Type) != 0 && metadata->Type != VaFsEntryType_Unknown) {
        entryType = metadata->Type;
    } else if (S_ISCHR(metadata->Mode)) {
        entryType = VaFsEntryType_CharacterDevice;
    } else if (S_ISBLK(metadata->Mode)) {
        entryType = VaFsEntryType_BlockDevice;
    } else if (S_ISFIFO(metadata->Mode)) {
        entryType = VaFsEntryType_Fifo;
    }

    if (!__is_special_entry_type(entryType)) {
        errno = EINVAL;
        return -1;
    }

    if ((entryType == VaFsEntryType_CharacterDevice || entryType == VaFsEntryType_BlockDevice) &&
        (metadata->Mask & VaFsMetadataMask_Device) == 0) {
        errno = EINVAL;
        return -1;
    }

    *preparedOut = *metadata;
    vafs_metadata_set_mode(preparedOut, entryType, preparedOut->Mode & 07777u);
    __finalize_entry_metadata(preparedOut, entryType, 0);
    return 0;
}


static int __add_file_entry(
    struct VaFsDirectoryWriter* writer,
    struct VaFsFile*            entry)
{
    struct VaFsDirectoryEntry* newEntry;

    newEntry = (struct VaFsDirectoryEntry*)calloc(1, sizeof(struct VaFsDirectoryEntry));
    if (!newEntry) {
        return -1;
    }

    newEntry->Type = VA_FS_DESCRIPTOR_TYPE_FILE;
    newEntry->File = entry;
    newEntry->Link = writer->Entries;
    writer->Entries = newEntry;
    writer->EntryCount++;
    writer->IndexDirty = 1;
    return 0;
}

static int __create_file_entry(
    struct VaFsDirectoryWriter* writer,
    const char*                 name,
    const struct VaFsMetadata*  metadata)
{
    struct VaFsFile* entry;
    int              status;

    entry = (struct VaFsFile*)calloc(1, sizeof(struct VaFsFile));
    if (!entry) {
        return -1;
    }

    // Build the full file object before publishing it into the directory list
    // so failed allocations cannot leave behind a half-initialized entry.
    entry->VaFs = writer->Base.VaFs;
    entry->Name = strdup(name);
    if (!entry->Name) {
        free(entry);
        return -1;
    }

    entry->Stat = *metadata;
    entry->StatCached = 1;
    __initialize_file_descriptor(&entry->Descriptor, metadata);
    status = __add_file_entry(writer, entry);
    if (status) {
        free((void*)entry->Name);
        free(entry);
        return status;
    }

    writer->Base.VaFs->Overview.Counts.Files++;
    return 0;
}

static int __add_symlink_entry(
    struct VaFsDirectoryWriter* writer,
    struct VaFsSymlink*         entry)
{
    struct VaFsDirectoryEntry* newEntry;

    newEntry = (struct VaFsDirectoryEntry*)calloc(1, sizeof(struct VaFsDirectoryEntry));
    if (!newEntry) {
        return -1;
    }

    newEntry->Type    = VA_FS_DESCRIPTOR_TYPE_SYMLINK;
    newEntry->Symlink = entry;
    newEntry->Link    = writer->Entries;

    writer->Entries = newEntry;
    writer->EntryCount++;
    writer->IndexDirty = 1;
    return 0;
}

static int __create_symlink_entry(
    struct VaFsDirectoryWriter* writer,
    const char*                 name,
    const char*                 target,
    const struct VaFsMetadata*  metadata)
{
    struct VaFsSymlink* entry;
    int                 status;

    entry = (struct VaFsSymlink*)calloc(1, sizeof(struct VaFsSymlink));
    if (!entry) {
        return -1;
    }

    // Symlink creation mirrors file creation, but it must allocate both the
    // visible name and the stored target before the entry becomes discoverable.
    entry->VaFs = writer->Base.VaFs;
    entry->Name = strdup(name);
    if (!entry->Name) {
        free(entry);
        return -1;
    }

    entry->Target = strdup(target);
    if (!entry->Target) {
        free((void*)entry->Name);
        free(entry);
        return -1;
    }

    entry->Stat = *metadata;
    entry->StatCached = 1;
    __initialize_symlink_descriptor(&entry->Descriptor, metadata);
    status = __add_symlink_entry(writer, entry);
    if (status) {
        free((void*)entry->Target);
        free((void*)entry->Name);
        free(entry);
        return status;
    }

    writer->Base.VaFs->Overview.Counts.Symlinks++;
    return 0;
}

static int __add_special_entry(
    struct VaFsDirectoryWriter* writer,
    struct VaFsSpecial*         entry)
{
    struct VaFsDirectoryEntry* newEntry;

    newEntry = (struct VaFsDirectoryEntry*)calloc(1, sizeof(struct VaFsDirectoryEntry));
    if (!newEntry) {
        return -1;
    }

    newEntry->Type = VA_FS_DESCRIPTOR_TYPE_SPECIAL;
    newEntry->Special = entry;
    newEntry->Link = writer->Entries;
    writer->Entries = newEntry;
    writer->EntryCount++;
    writer->IndexDirty = 1;
    return 0;
}

static int __create_special_entry(
    struct VaFsDirectoryWriter* writer,
    const char*                 name,
    const struct VaFsMetadata*  metadata)
{
    struct VaFsSpecial* entry;
    int                 status;

    entry = (struct VaFsSpecial*)calloc(1, sizeof(struct VaFsSpecial));
    if (!entry) {
        return -1;
    }

    // Special entries publish only after their explicit subtype-bearing
    // descriptor has been initialized from validated metadata.
    entry->VaFs = writer->Base.VaFs;
    entry->Name = strdup(name);
    if (!entry->Name) {
        free(entry);
        return -1;
    }

    entry->Stat = *metadata;
    entry->StatCached = 1;
    __initialize_special_descriptor(&entry->Descriptor, metadata);
    status = __add_special_entry(writer, entry);
    if (status) {
        free((void*)entry->Name);
        free(entry);
        return status;
    }
    return 0;
}

static int __add_hardlink_entry(
    struct VaFsDirectoryWriter* writer,
    struct VaFsHardlink*        entry)
{
    struct VaFsDirectoryEntry* newEntry;

    newEntry = (struct VaFsDirectoryEntry*)calloc(1, sizeof(struct VaFsDirectoryEntry));
    if (!newEntry) {
        return -1;
    }

    newEntry->Type = VA_FS_DESCRIPTOR_TYPE_HARDLINK;
    newEntry->Hardlink = entry;
    newEntry->Link = writer->Entries;
    writer->Entries = newEntry;
    writer->EntryCount++;
    writer->IndexDirty = 1;
    return 0;
}

static int __directory_entry_increment_link_count(
    struct VaFsDirectoryEntry* entry)
{
    // Writers bump the canonical entry immediately so every later stat or
    // enumeration sees the final shared link count before serialization.
    switch (entry->Type) {
        case VA_FS_DESCRIPTOR_TYPE_FILE:
            entry->File->Stat.LinkCount++;
            entry->File->Stat.Mask |= VaFsMetadataMask_LinkCount;
            return 0;
        case VA_FS_DESCRIPTOR_TYPE_SYMLINK:
            entry->Symlink->Stat.LinkCount++;
            entry->Symlink->Stat.Mask |= VaFsMetadataMask_LinkCount;
            return 0;
        case VA_FS_DESCRIPTOR_TYPE_SPECIAL:
            entry->Special->Stat.LinkCount++;
            entry->Special->Stat.Mask |= VaFsMetadataMask_LinkCount;
            return 0;
        default:
            errno = EINVAL;
            return -1;
    }
}

static int __create_hardlink_entry(
    struct VaFsDirectoryWriter* writer,
    const char*                 name,
    uint64_t                    objectId)
{
    struct VaFsHardlink* entry;
    int                  status;

    entry = (struct VaFsHardlink*)calloc(1, sizeof(struct VaFsHardlink));
    if (!entry) {
        return -1;
    }

    // Hardlinks carry only alias-local state, so finish the thin descriptor up
    // front and publish it only after the alias name has been fully allocated.
    entry->VaFs = writer->Base.VaFs;
    entry->Name = strdup(name);
    if (!entry->Name) {
        free(entry);
        return -1;
    }

    entry->Descriptor.Base.Type = VA_FS_DESCRIPTOR_TYPE_HARDLINK;
    entry->Descriptor.Base.Length = sizeof(VaFsHardlinkDescriptor_t);
    entry->Descriptor.NameLength = (uint16_t)strlen(name);
    entry->Descriptor.ObjectId = objectId;
    status = __add_hardlink_entry(writer, entry);
    if (status != 0) {
        free((void*)entry->Name);
        free(entry);
        return status;
    }
    return 0;
}

static int __add_directory_entry(
    struct VaFsDirectoryWriter* writer,
    struct VaFsDirectory*       entry)
{
    struct VaFsDirectoryEntry* newEntry;

    newEntry = (struct VaFsDirectoryEntry*)calloc(1, sizeof(struct VaFsDirectoryEntry));
    if (!newEntry) {
        return -1;
    }

    newEntry->Type = VA_FS_DESCRIPTOR_TYPE_DIRECTORY;
    newEntry->Directory = entry;
    newEntry->Link = writer->Entries;
    writer->Entries = newEntry;
    writer->EntryCount++;
    writer->IndexDirty = 1;
    return 0;
}

static int __create_directory_entry(
    struct VaFsDirectoryWriter* writer,
    const char*                 name,
    const struct VaFsMetadata*  metadata)
{
    struct VaFsDirectoryWriter* entry;
    int                         status;
    VAFS_DEBUG("__create_directory_entry(name=%s)\n", name);

    entry = (struct VaFsDirectoryWriter*)calloc(1, sizeof(struct VaFsDirectoryWriter));
    if (!entry) {
        errno = ENOMEM;
        return -1;
    }

    // New child directories start life as writer-side directory objects so any
    // later nested creates can attach directly without another mode conversion.
    entry->IndexDirty = 1;
    entry->Base.VaFs = writer->Base.VaFs;
    entry->Base.Name = strdup(name);
    if (!entry->Base.Name) {
        free(entry);
        return -1;
    }

    entry->Base.Stat = *metadata;
    entry->Base.StatCached = 1;
    __initialize_directory_descriptor(&entry->Base.Descriptor, metadata);
    status = __add_directory_entry(writer, &entry->Base);
    if (status) {
        free((void*)entry->Base.Name);
        free(entry);
        return status;
    }

    writer->Base.VaFs->Overview.Counts.Directories++;
    return 0;
}

int vafs_directory_builder_create_directory(
    struct VaFsDirectoryBuilder* handle,
    const char*                  name,
    const struct VaFsMetadata*   metadata,
    struct VaFsDirectoryHandle** handleOut)
{

    struct VaFsDirectoryWriter* writer;
    int                         status;
    struct VaFsDirectoryEntry*  entry;
    struct VaFsMetadata         preparedMetadata;
    char                        token[VAFS_NAME_MAX + 1];
    VAFS_DEBUG("vafs_directory_create_directory(handle=%p, name=%s, handleOut=%p)\n", handle, name, handleOut);

    if (handle == NULL || name == NULL || metadata == NULL || handleOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = __prepare_metadata_for_create(metadata, VaFsEntryType_Directory, 0, &preparedMetadata);
    if (status != 0) {
        return status;
    }

    if (handle->Directory->VaFs->Mode != VaFsMode_Write) {
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
    if (entry != NULL) {
        // Directory creation intentionally behaves like open-or-create so tree
        // import callers can descend without having to special-case preexisting parents.
        *handleOut = __create_handle(entry->Directory);
        return 0;
    }

    writer = (struct VaFsDirectoryWriter*)handle->Directory;
    status = __create_directory_entry(writer, token, &preparedMetadata);
    if (status != 0) {
        return status;
    }
    entry = __vafs_directory_find_entry(handle->Directory, token);

    *handleOut = __create_handle(entry->Directory);
    return 0;
}

int vafs_directory_builder_create_file(
    struct VaFsDirectoryBuilder* handle,
    const char*                  name,
    const struct VaFsMetadata*   metadata,
    struct VaFsFileBuilder**     handleOut,
    struct VaFsObjectBuilder**   objectOut)
{
    struct VaFsDirectoryWriter* writer;
    int                         status;
    struct VaFsDirectoryEntry*  entry;
    struct VaFsMetadata         preparedMetadata;
    char                        token[VAFS_NAME_MAX + 1];
    VAFS_DEBUG("vafs_directory_builder_create_file(name=%s)\n", name);

    if (handle == NULL || name == NULL || metadata == NULL || handleOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = __prepare_metadata_for_create(metadata, VaFsEntryType_File, 0, &preparedMetadata);
    if (status != 0) {
        return status;
    }

    // verify write mode
    if (handle->Directory->VaFs->Mode != VaFsMode_Write) {
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
    if (entry != NULL) {
        errno = EEXIST;
        return -1;
    }

    writer = (struct VaFsDirectoryWriter*)handle->Directory;
    status = __create_file_entry(writer, token, &preparedMetadata);
    if (status != 0) {
        return status;
    }

    // Reuse the ordinary lookup path after insertion so the returned handle is
    // wired to the canonical in-list entry rather than a temporary local pointer.
    entry = __vafs_directory_find_entry(handle->Directory, token);
    
    *handleOut = vafs_file_create_handle(entry->File);
    *objectOut = vafs_object_create_handle(entry->File);
    return 0;
}

int vafs_directory_builder_create_symlink(
    struct VaFsDirectoryBuilder* handle,
    const char*                  name,
    const char*                  target,
    const struct VaFsMetadata*   metadata,
    struct VaFsObjectBuilder**   objectOut)
{
    struct VaFsDirectoryEntry* entry;
    char                       token[VAFS_NAME_MAX + 1];
    struct VaFsMetadata        preparedMetadata;
    size_t                     targetLength;
    VAFS_DEBUG("vafs_directory_builder_create_symlink(name=%s, target=%s)\n", name, target);

    if (handle == NULL || name == NULL || target == NULL || metadata == NULL) {
        errno = EINVAL;
        return -1;
    }

    targetLength = strlen(target);
    if (targetLength > VAFS_PATH_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }

    if (__prepare_metadata_for_create(metadata, VaFsEntryType_Symlink, targetLength, &preparedMetadata) != 0) {
        return -1;
    }

    if (handle->Directory->VaFs->Mode != VaFsMode_Write) {
        errno = EACCES;
        return -1;
    }

    // do this to verify the incoming name
    if (!__vafs_pathtoken(name, token, sizeof(token))) {
        errno = ENOENT;
        return -1;
    }

    // find the name in the directory
    VAFS_DEBUG("vafs_directory_builder_create_symlink: locating %s\n", token);
    entry = __vafs_directory_find_entry(handle->Directory, token);
    if (entry == NULL) {
        struct VaFsDirectoryWriter* writer = (struct VaFsDirectoryWriter*)handle->Directory;
        // Publish the alias only once the normalized metadata and copied target
        // are both ready, so duplicate-name failures remain side-effect free.
        return __create_symlink_entry(writer, token, target, &preparedMetadata);
    }

    errno = EEXIST;
    return -1;
}

int vafs_directory_builder_create_special(
    struct VaFsDirectoryBuilder*   handle,
    const char*                    name,
    enum VaFsEntryType             type,
    const struct VaFsMetadata*     metadata,
    const struct VaFsDeviceNumber* device,
    struct VaFsObjectBuilder**     objectOut)
{
    struct VaFsDirectoryWriter* writer;
    struct VaFsDirectoryEntry*  entry;
    struct VaFsMetadata         preparedMetadata;
    char                        token[VAFS_NAME_MAX + 1];
    int                         status;

    if (handle == NULL || name == NULL || metadata == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = __prepare_special_metadata_for_create(metadata, &preparedMetadata);
    if (status != 0) {
        return status;
    }

    if (handle->Directory->VaFs->Mode != VaFsMode_Write) {
        errno = EACCES;
        return -1;
    }

    if (!__vafs_pathtoken(name, token, sizeof(token))) {
        errno = ENOENT;
        return -1;
    }

    entry = __vafs_directory_find_entry(handle->Directory, token);
    if (entry != NULL) {
        errno = EEXIST;
        return -1;
    }

    writer = (struct VaFsDirectoryWriter*)handle->Directory;
    // Validation already proved the special subtype is reconstructible, so the
    // remaining work is just to materialize and publish the writer-side entry.
    return __create_special_entry(writer, token, &preparedMetadata);
}

int vafs_directory_create_hardlink(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    uint64_t                    objectId)
{
    struct VaFsDirectoryWriter* writer;
    struct VaFsDirectoryEntry*  entry;
    struct VaFsDirectoryEntry*  targetEntry;
    char                        token[VAFS_NAME_MAX + 1];

    if (handle == NULL || name == NULL || objectId == 0) {
        errno = EINVAL;
        return -1;
    }

    if (handle->Directory->VaFs->Mode != VaFsMode_Write) {
        errno = EACCES;
        return -1;
    }

    if (!__vafs_pathtoken(name, token, sizeof(token))) {
        errno = ENOENT;
        return -1;
    }

    entry = __vafs_directory_find_entry(handle->Directory, token);
    if (entry != NULL) {
        errno = EEXIST;
        return -1;
    }

    targetEntry = __find_entry_by_object_id(handle->Directory->VaFs->RootDirectory, objectId);
    if (targetEntry == NULL) {
        errno = ENOENT;
        return -1;
    }

    // Only canonical non-directory entries may anchor aliases; allowing a
    // hardlink-to-hardlink chain or directory alias would complicate lookup
    // semantics without preserving any extra image information.
    if (targetEntry->Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY ||
        targetEntry->Type == VA_FS_DESCRIPTOR_TYPE_HARDLINK) {
        errno = EINVAL;
        return -1;
    }

    if (__directory_entry_increment_link_count(targetEntry) != 0) {
        return -1;
    }

    writer = (struct VaFsDirectoryWriter*)handle->Directory;
    // Only publish the alias after the canonical target has accepted the link-count
    // bump, so failed creates cannot leave shared metadata overstated.
    return __create_hardlink_entry(writer, token, objectId);
}

int vafs_directory_builder_close(
    struct VaFsDirectoryBuilder* handle)
{
    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }

    // free handle
    free(handle);
    return 0;
}
