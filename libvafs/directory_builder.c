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

struct VaFsObjectBuilder {
    struct VaFs*     VaFs;
    VaFsDescriptor_t Descriptor;
};

struct VaFsFileHandle;

extern size_t vafs_file_write(
    struct VaFsFileHandle* handle,
    void*                  buffer,
    size_t                 size);

extern int vafs_file_close(
    struct VaFsFileHandle* handle);

static struct VaFsDirectoryBuilder* __create_directory_builder_handle(
    struct VaFsDirectory* directory)
{
    struct VaFsDirectoryBuilder* handle;

    handle = malloc(sizeof(struct VaFsDirectoryBuilder));
    if (handle == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    handle->Directory = directory;
    handle->Index = 0;
    return handle;
}

static struct VaFsObjectBuilder* __create_object_builder_handle(
    void* object)
{
    return (struct VaFsObjectBuilder*)object;
}

static enum VaFsEntryType __object_builder_entry_type(
    struct VaFsObjectBuilder* handle)
{
    if (handle == NULL) {
        return VaFsEntryType_Unknown;
    }

    switch (handle->Descriptor.Type) {
        case VA_FS_DESCRIPTOR_TYPE_FILE:
            return VaFsEntryType_File;
        case VA_FS_DESCRIPTOR_TYPE_DIRECTORY:
            return VaFsEntryType_Directory;
        case VA_FS_DESCRIPTOR_TYPE_SYMLINK:
            return VaFsEntryType_Symlink;
        case VA_FS_DESCRIPTOR_TYPE_SPECIAL:
            return (enum VaFsEntryType)((struct VaFsSpecial*)handle)->Descriptor.EntryType;
        default:
            return VaFsEntryType_Unknown;
    }
}

static struct VaFsMetadata* __object_builder_metadata(
    struct VaFsObjectBuilder* handle)
{
    if (handle == NULL) {
        errno = EINVAL;
        return NULL;
    }

    switch (handle->Descriptor.Type) {
        case VA_FS_DESCRIPTOR_TYPE_FILE:
            return &((struct VaFsFile*)handle)->Stat;
        case VA_FS_DESCRIPTOR_TYPE_DIRECTORY:
            return &((struct VaFsDirectory*)handle)->Stat;
        case VA_FS_DESCRIPTOR_TYPE_SYMLINK:
            return &((struct VaFsSymlink*)handle)->Stat;
        case VA_FS_DESCRIPTOR_TYPE_SPECIAL:
            return &((struct VaFsSpecial*)handle)->Stat;
        default:
            errno = EINVAL;
            return NULL;
    }
}

static struct VaFsXattrSet** __object_builder_xattr_slot(
    struct VaFsObjectBuilder* handle)
{
    if (handle == NULL) {
        errno = EINVAL;
        return NULL;
    }

    switch (handle->Descriptor.Type) {
        case VA_FS_DESCRIPTOR_TYPE_FILE:
            return &((struct VaFsFile*)handle)->Xattrs;
        case VA_FS_DESCRIPTOR_TYPE_DIRECTORY:
            return &((struct VaFsDirectory*)handle)->Xattrs;
        case VA_FS_DESCRIPTOR_TYPE_SYMLINK:
            return &((struct VaFsSymlink*)handle)->Xattrs;
        case VA_FS_DESCRIPTOR_TYPE_SPECIAL:
            return &((struct VaFsSpecial*)handle)->Xattrs;
        default:
            errno = EINVAL;
            return NULL;
    }
}

static int* __object_builder_xattrs_loaded_slot(
    struct VaFsObjectBuilder* handle)
{
    if (handle == NULL) {
        errno = EINVAL;
        return NULL;
    }

    switch (handle->Descriptor.Type) {
        case VA_FS_DESCRIPTOR_TYPE_FILE:
            return &((struct VaFsFile*)handle)->XattrsLoaded;
        case VA_FS_DESCRIPTOR_TYPE_DIRECTORY:
            return &((struct VaFsDirectory*)handle)->XattrsLoaded;
        case VA_FS_DESCRIPTOR_TYPE_SYMLINK:
            return &((struct VaFsSymlink*)handle)->XattrsLoaded;
        case VA_FS_DESCRIPTOR_TYPE_SPECIAL:
            return &((struct VaFsSpecial*)handle)->XattrsLoaded;
        default:
            errno = EINVAL;
            return NULL;
    }
}

static struct VaFsXattr* __object_builder_find_xattr(
    struct VaFsXattrSet* set,
    const char*          name)
{
    struct VaFsXattr* entry;

    if (set == NULL || name == NULL) {
        return NULL;
    }

    entry = set->Entries;
    while (entry != NULL) {
        if (strcmp(entry->Name, name) == 0) {
            return entry;
        }
        entry = entry->Link;
    }
    return NULL;
}

static int __object_builder_put_xattr(
    struct VaFsXattrSet* set,
    const char*          name,
    const void*          value,
    uint32_t             valueSize)
{
    struct VaFsXattr* entry;
    void*             valueCopy = NULL;

    if (set == NULL || name == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (valueSize != 0) {
        valueCopy = malloc(valueSize);
        if (valueCopy == NULL) {
            errno = ENOMEM;
            return -1;
        }
        memcpy(valueCopy, value, valueSize);
    }

    entry = __object_builder_find_xattr(set, name);
    if (entry != NULL) {
        free(entry->Value);
        entry->Value = valueCopy;
        entry->ValueLength = valueSize;
        return 0;
    }

    entry = calloc(1, sizeof(struct VaFsXattr));
    if (entry == NULL) {
        free(valueCopy);
        errno = ENOMEM;
        return -1;
    }

    entry->Name = strdup(name);
    if (entry->Name == NULL) {
        free(valueCopy);
        free(entry);
        errno = ENOMEM;
        return -1;
    }

    entry->Value = valueCopy;
    entry->ValueLength = valueSize;
    if (set->Entries == NULL) {
        set->Entries = entry;
    } else {
        struct VaFsXattr* tail = set->Entries;
        while (tail->Link != NULL) {
            tail = tail->Link;
        }
        tail->Link = entry;
    }

    set->Count++;
    return 0;
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
    enum VaFsEntryType             type,
    const struct VaFsMetadata*     metadata,
    const struct VaFsDeviceNumber* device,
    struct VaFsMetadata*           preparedOut)
{
    if (metadata == NULL || preparedOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if ((metadata->Mask & VaFsMetadataMask_Mode) == 0) {
        errno = EINVAL;
        return -1;
    }

    if (!__is_special_entry_type(type)) {
        errno = EINVAL;
        return -1;
    }

    if ((metadata->Mask & VaFsMetadataMask_Type) != 0 &&
        metadata->Type != VaFsEntryType_Unknown &&
        metadata->Type != type) {
        errno = EINVAL;
        return -1;
    }

    if ((type == VaFsEntryType_CharacterDevice || type == VaFsEntryType_BlockDevice) &&
        device == NULL) {
        errno = EINVAL;
        return -1;
    }

    *preparedOut = *metadata;
    vafs_metadata_set_mode(preparedOut, type, preparedOut->Mode & 07777u);
    if (device != NULL) {
        preparedOut->Device = *device;
        preparedOut->Mask |= VaFsMetadataMask_Device;
    }
    __finalize_entry_metadata(preparedOut, type, 0);
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

    // File creation is intentionally structured as a two-step publish: build a
    // fully initialized object first, then attach it to the directory. This
    // keeps half-formed nodes from being discoverable if an allocation fails.

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
    struct VaFsDirectoryBuilder*  builder,
    const char*                   name,
    const struct VaFsMetadata*    metadata,
    struct VaFsDirectoryBuilder** builderOut,
    struct VaFsObjectBuilder**    objectOut)
{

    struct VaFsDirectoryWriter* writer;
    int                         status;
    struct VaFsDirectoryEntry*  entry;
    struct VaFsMetadata         preparedMetadata;
    char                        token[VAFS_NAME_MAX + 1];
    VAFS_DEBUG("vafs_directory_builder_create_directory(builder=%p, name=%s, builderOut=%p)\n", builder, name, builderOut);

    if (builder == NULL || name == NULL || metadata == NULL || builderOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = __prepare_metadata_for_create(metadata, VaFsEntryType_Directory, 0, &preparedMetadata);
    if (status != 0) {
        return status;
    }

    if (builder->Directory->VaFs->Mode != VaFsMode_Write) {
        errno = EACCES;
        return -1;
    }

    // do this to verify the incoming name
    if (!__vafs_pathtoken(name, token, sizeof(token))) {
        errno = ENOENT;
        return -1;
    }

    // find the name in the directory
    entry = __vafs_directory_find_entry(builder->Directory, token);
    if (entry != NULL) {
        errno = EEXIST;
        return -1;
    }

    writer = (struct VaFsDirectoryWriter*)builder->Directory;
    status = __create_directory_entry(writer, token, &preparedMetadata);
    if (status != 0) {
        return status;
    }
    entry = __vafs_directory_find_entry(builder->Directory, token);

    *builderOut = __create_directory_builder_handle(entry->Directory);
    if (*builderOut == NULL) {
        return -1;
    }
    if (objectOut != NULL) {
        *objectOut = __create_object_builder_handle(entry->Directory);
    }
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
    
    *handleOut = (struct VaFsFileBuilder*)vafs_file_create_handle(entry->File);
    if (*handleOut == NULL) {
        return -1;
    }
    if (objectOut != NULL) {
        *objectOut = __create_object_builder_handle(entry->File);
    }
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
        if (__create_symlink_entry(writer, token, target, &preparedMetadata) != 0) {
            return -1;
        }
        entry = __vafs_directory_find_entry(handle->Directory, token);
        if (objectOut != NULL) {
            *objectOut = __create_object_builder_handle(entry->Symlink);
        }
        return 0;
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

    status = __prepare_special_metadata_for_create(type, metadata, device, &preparedMetadata);
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
    status = __create_special_entry(writer, token, &preparedMetadata);
    if (status != 0) {
        return status;
    }

    entry = __vafs_directory_find_entry(handle->Directory, token);
    if (objectOut != NULL) {
        *objectOut = __create_object_builder_handle(entry->Special);
    }
    return 0;
}

int vafs_directory_builder_link(
    struct VaFsDirectoryBuilder* handle,
    const char*                  name,
    struct VaFsObjectBuilder*    target)
{
    struct VaFsDirectoryWriter* writer;
    struct VaFsDirectoryEntry*  entry;
    struct VaFsDirectoryEntry*  targetEntry;
    struct VaFsMetadata*        targetMetadata;
    uint64_t                    objectId;
    char                        token[VAFS_NAME_MAX + 1];

    if (handle == NULL || name == NULL || target == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (handle->Directory->VaFs->Mode != VaFsMode_Write) {
        errno = EACCES;
        return -1;
    }

    if (target->VaFs != handle->Directory->VaFs ||
        __object_builder_entry_type(target) == VaFsEntryType_Directory) {
        errno = EINVAL;
        return -1;
    }

    targetMetadata = __object_builder_metadata(target);
    if (targetMetadata == NULL) {
        return -1;
    }
    if ((targetMetadata->Mask & VaFsMetadataMask_ObjectId) == 0 || targetMetadata->ObjectId == 0) {
        errno = EINVAL;
        return -1;
    }
    objectId = targetMetadata->ObjectId;

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

int vafs_file_builder_write(
    struct VaFsFileBuilder* handle,
    const void*             buffer,
    size_t                  length,
    size_t*                 bytesWrittenOut)
{
    size_t result;

    if (handle == NULL || bytesWrittenOut == NULL || (buffer == NULL && length != 0)) {
        errno = EINVAL;
        return -1;
    }

    if (length == 0) {
        *bytesWrittenOut = 0;
        return 0;
    }

    result = vafs_file_write((struct VaFsFileHandle*)handle, (void*)buffer, length);
    if (result == (size_t)-1) {
        return -1;
    }

    *bytesWrittenOut = length;
    return 0;
}

int vafs_file_builder_close(
    struct VaFsFileBuilder* handle)
{
    return vafs_file_close((struct VaFsFileHandle*)handle);
}

int vafs_object_builder_setxattr(
    struct VaFsObjectBuilder* handle,
    const char*               name,
    const void*               value,
    size_t                    valueSize)
{
    struct VaFsXattrSet** xattrSlot;
    struct VaFsMetadata*  metadata;
    int*                  loadedSlot;

    if (handle == NULL || name == NULL || name[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    if (valueSize != 0 && value == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (strlen(name) > UINT16_MAX || valueSize > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }

    if (handle->VaFs->Mode != VaFsMode_Write) {
        errno = EACCES;
        return -1;
    }

    xattrSlot = __object_builder_xattr_slot(handle);
    loadedSlot = __object_builder_xattrs_loaded_slot(handle);
    metadata = __object_builder_metadata(handle);
    if (xattrSlot == NULL || loadedSlot == NULL || metadata == NULL) {
        return -1;
    }

    if (*xattrSlot == NULL) {
        *xattrSlot = calloc(1, sizeof(struct VaFsXattrSet));
        if (*xattrSlot == NULL) {
            errno = ENOMEM;
            return -1;
        }
        (*xattrSlot)->Index = VA_FS_INVALID_XATTR_INDEX;
    }

    if (__object_builder_put_xattr(*xattrSlot, name, value, (uint32_t)valueSize) != 0) {
        return -1;
    }

    *loadedSlot = 1;
    metadata->XattrCount = (*xattrSlot)->Count;
    metadata->Mask |= VaFsMetadataMask_XattrCount;
    return 0;
}

int vafs_object_builder_removexattr(
    struct VaFsObjectBuilder* handle,
    const char*               name)
{
    struct VaFsXattrSet** xattrSlot;
    struct VaFsMetadata*  metadata;
    int*                  loadedSlot;
    struct VaFsXattr*     entry;
    struct VaFsXattr*     previous = NULL;

    if (handle == NULL || name == NULL || name[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    if (handle->VaFs->Mode != VaFsMode_Write) {
        errno = EACCES;
        return -1;
    }

    xattrSlot = __object_builder_xattr_slot(handle);
    loadedSlot = __object_builder_xattrs_loaded_slot(handle);
    metadata = __object_builder_metadata(handle);
    if (xattrSlot == NULL || loadedSlot == NULL || metadata == NULL) {
        return -1;
    }

    if (*xattrSlot == NULL) {
        errno = ENODATA;
        return -1;
    }

    entry = (*xattrSlot)->Entries;
    while (entry != NULL) {
        if (strcmp(entry->Name, name) == 0) {
            if (previous == NULL) {
                (*xattrSlot)->Entries = entry->Link;
            } else {
                previous->Link = entry->Link;
            }

            free(entry->Name);
            free(entry->Value);
            free(entry);
            (*xattrSlot)->Count--;
            *loadedSlot = 1;
            metadata->XattrCount = (*xattrSlot)->Count;
            metadata->Mask |= VaFsMetadataMask_XattrCount;

            if ((*xattrSlot)->Count == 0) {
                free(*xattrSlot);
                *xattrSlot = NULL;
            }
            return 0;
        }
        previous = entry;
        entry = entry->Link;
    }

    errno = ENODATA;
    return -1;
}
