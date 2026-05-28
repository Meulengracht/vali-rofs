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
#include "private.h"
#include <stdlib.h>
#include <string.h>
#include <vafs/directory.h>

struct VaFsDirectoryHandle {
    struct VaFsDirectory* Directory;
    int                   Index;
};

static void __copy_timestamp_to_descriptor(
    VaFsDescriptorTimestamp_t* destination,
    const struct VaFsTimestamp* source)
{
    destination->Seconds = source->Seconds;
    destination->Nanoseconds = source->Nanoseconds;
}

static void __copy_timestamp_from_descriptor(
    struct VaFsTimestamp*          destination,
    const VaFsDescriptorTimestamp_t* source)
{
    destination->Seconds = source->Seconds;
    destination->Nanoseconds = source->Nanoseconds;
}

static void __finalize_entry_metadata(
    struct VaFsMetadata* metadata,
    enum VaFsEntryType   type,
    uint64_t             size)
{
    metadata->Type = type;
    metadata->Size = size;
    metadata->Mask |= VaFsMetadataMask_Type | VaFsMetadataMask_Size;

    // Readers and writers both rely on a usable link count even when older or
    // minimal callers never filled one in explicitly.
    if ((metadata->Mask & VaFsMetadataMask_LinkCount) == 0) {
        metadata->LinkCount = 1;
        metadata->Mask |= VaFsMetadataMask_LinkCount;
    }
}

static void __descriptor_metadata_initialize(
    VaFsDescriptorMetadata_t*  descriptorMetadata,
    const struct VaFsMetadata* metadata)
{
    memset(descriptorMetadata, 0, sizeof(VaFsDescriptorMetadata_t));
    if (metadata == NULL) {
        return;
    }

    descriptorMetadata->Mask = metadata->Mask;
    descriptorMetadata->Mode = metadata->Mode;
    descriptorMetadata->Uid = metadata->Uid;
    descriptorMetadata->Gid = metadata->Gid;
    descriptorMetadata->LinkCount = metadata->LinkCount;
    descriptorMetadata->XattrCount = metadata->XattrCount;
    // Xattr indices are assigned only after the writer has deduplicated every
    // set that will live in the colder descriptor-stream xattr section.
    descriptorMetadata->XattrIndex = VA_FS_INVALID_XATTR_INDEX;
    descriptorMetadata->ObjectId = metadata->ObjectId;
    __copy_timestamp_to_descriptor(&descriptorMetadata->MTime, &metadata->MTime);
    __copy_timestamp_to_descriptor(&descriptorMetadata->ATime, &metadata->ATime);
    __copy_timestamp_to_descriptor(&descriptorMetadata->CTime, &metadata->CTime);
    __copy_timestamp_to_descriptor(&descriptorMetadata->BirthTime, &metadata->BirthTime);
    descriptorMetadata->DeviceMajor = metadata->Device.Major;
    descriptorMetadata->DeviceMinor = metadata->Device.Minor;
    descriptorMetadata->WindowsAttributes = metadata->WindowsAttributes;
}

static void __materialize_descriptor_metadata(
    const VaFsDescriptorMetadata_t* descriptorMetadata,
    enum VaFsEntryType              type,
    uint64_t                        size,
    struct VaFsMetadata*            metadata)
{
    vafs_metadata_initialize(metadata);
    if (descriptorMetadata != NULL) {
        metadata->Mask = descriptorMetadata->Mask;
        metadata->Mode = descriptorMetadata->Mode;
        metadata->Uid = descriptorMetadata->Uid;
        metadata->Gid = descriptorMetadata->Gid;
        metadata->LinkCount = descriptorMetadata->LinkCount;
        metadata->XattrCount = descriptorMetadata->XattrCount;
        metadata->ObjectId = descriptorMetadata->ObjectId;
        __copy_timestamp_from_descriptor(&metadata->MTime, &descriptorMetadata->MTime);
        __copy_timestamp_from_descriptor(&metadata->ATime, &descriptorMetadata->ATime);
        __copy_timestamp_from_descriptor(&metadata->CTime, &descriptorMetadata->CTime);
        __copy_timestamp_from_descriptor(&metadata->BirthTime, &descriptorMetadata->BirthTime);
        metadata->Device.Major = descriptorMetadata->DeviceMajor;
        metadata->Device.Minor = descriptorMetadata->DeviceMinor;
        metadata->WindowsAttributes = descriptorMetadata->WindowsAttributes;
    }

    __finalize_entry_metadata(metadata, type, size);
}

static void __initialize_root_metadata(
    struct VaFsMetadata* metadata)
{
    // New images persist root metadata in a real directory descriptor, but we
    // still keep one canonical default here for freshly created in-memory roots
    // and for older images whose header still points straight at the child list.
    vafs_metadata_initialize(metadata);
    vafs_metadata_set_mode(metadata, VaFsEntryType_Directory, 0775);
    metadata->LinkCount = 1;
    metadata->Size = 0;
    metadata->Mask |= VaFsMetadataMask_LinkCount | VaFsMetadataMask_Size;
}

static void __initialize_file_descriptor(
    VaFsFileDescriptor_t* descriptor,
    const struct VaFsMetadata* metadata)
{
    descriptor->Base.Type = VA_FS_DESCRIPTOR_TYPE_FILE;
    descriptor->Base.Length = sizeof(VaFsFileDescriptor_t);

    descriptor->Data.Index = VA_FS_INVALID_BLOCK;
    descriptor->Data.Offset = VA_FS_INVALID_OFFSET;
    descriptor->FileLength = (uint32_t)metadata->Size;
    __descriptor_metadata_initialize(&descriptor->Metadata, metadata);
}

static void __initialize_directory_descriptor(
    VaFsDirectoryDescriptor_t* descriptor,
    const struct VaFsMetadata* metadata)
{
    descriptor->Base.Type = VA_FS_DESCRIPTOR_TYPE_DIRECTORY;
    descriptor->Base.Length = sizeof(VaFsDirectoryDescriptor_t);

    descriptor->Descriptor.Index = VA_FS_INVALID_BLOCK;
    descriptor->Descriptor.Offset = VA_FS_INVALID_OFFSET;
    __descriptor_metadata_initialize(&descriptor->Metadata, metadata);
}

static void __initialize_symlink_descriptor(
    VaFsSymlinkDescriptor_t* descriptor,
    const struct VaFsMetadata* metadata)
{
    descriptor->Base.Type = VA_FS_DESCRIPTOR_TYPE_SYMLINK;
    descriptor->Base.Length = sizeof(VaFsSymlinkDescriptor_t);

    descriptor->NameLength = 0;
    descriptor->TargetLength = 0;
    __descriptor_metadata_initialize(&descriptor->Metadata, metadata);
}

static void __initialize_special_descriptor(
    VaFsSpecialDescriptor_t* descriptor,
    const struct VaFsMetadata* metadata)
{
    descriptor->Base.Type = VA_FS_DESCRIPTOR_TYPE_SPECIAL;
    descriptor->Base.Length = sizeof(VaFsSpecialDescriptor_t);
    descriptor->EntryType = (uint16_t)metadata->Type;
    descriptor->Reserved = 0;
    __descriptor_metadata_initialize(&descriptor->Metadata, metadata);
}

static int __is_special_entry_type(
    enum VaFsEntryType type)
{
    return type == VaFsEntryType_CharacterDevice ||
        type == VaFsEntryType_BlockDevice ||
        type == VaFsEntryType_Fifo;
}

static int __vafs_file_stat_internal(
    struct VaFsFile*     file,
    struct VaFsMetadata* metadata)
{
    if (!file->StatCached) {
        __materialize_descriptor_metadata(&file->Descriptor.Metadata, VaFsEntryType_File, file->Descriptor.FileLength, &file->Stat);
        file->StatCached = 1;
    }

    __finalize_entry_metadata(&file->Stat, VaFsEntryType_File, file->Descriptor.FileLength);
    *metadata = file->Stat;
    return 0;
}

static int __vafs_directory_stat_internal(
    struct VaFsDirectory*  directory,
    struct VaFsMetadata*   metadata)
{
    if (!directory->StatCached) {
        __materialize_descriptor_metadata(&directory->Descriptor.Metadata, VaFsEntryType_Directory, 0, &directory->Stat);
        directory->StatCached = 1;
    }

    __finalize_entry_metadata(&directory->Stat, VaFsEntryType_Directory, 0);
    *metadata = directory->Stat;
    return 0;
}

static int __vafs_symlink_stat_internal(
    struct VaFsSymlink*  symlink,
    struct VaFsMetadata* metadata)
{
    if (!symlink->StatCached) {
        __materialize_descriptor_metadata(&symlink->Descriptor.Metadata, VaFsEntryType_Symlink, strlen(symlink->Target), &symlink->Stat);
        symlink->StatCached = 1;
    }

    __finalize_entry_metadata(&symlink->Stat, VaFsEntryType_Symlink, strlen(symlink->Target));
    *metadata = symlink->Stat;
    return 0;
}

static int __vafs_special_stat_internal(
    struct VaFsSpecial*  special,
    struct VaFsMetadata* metadata)
{
    if (!special->StatCached) {
        __materialize_descriptor_metadata(
            &special->Descriptor.Metadata,
            (enum VaFsEntryType)special->Descriptor.EntryType,
            0,
            &special->Stat
        );
        special->StatCached = 1;
    }

    __finalize_entry_metadata(&special->Stat, (enum VaFsEntryType)special->Descriptor.EntryType, 0);
    *metadata = special->Stat;
    return 0;
}

int vafs_directory_create_root(
    struct VaFs*           vafs,
    struct VaFsDirectory** directoryOut)
{
    struct VaFsDirectoryWriter* directory;
    
    if (vafs == NULL || directoryOut == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    directory = malloc(sizeof(struct VaFsDirectoryWriter));
    if (!directory) {
        errno = ENOMEM;
        return -1;
    }
    memset(directory, 0, sizeof(struct VaFsDirectoryWriter));

    directory->Base.VaFs = vafs;
    directory->Base.Name = strdup("root");
    // The root descriptor position is only known after the whole root payload
    // has been flushed, because that standalone descriptor is emitted last.
    directory->Base.DescriptorPosition.Index = VA_FS_INVALID_BLOCK;
    directory->Base.DescriptorPosition.Offset = VA_FS_INVALID_OFFSET;
    directory->Index = NULL;
    directory->EntryCount = 0;
    directory->IndexDirty = 1;

    __initialize_root_metadata(&directory->Base.Stat);
    directory->Base.StatCached = 1;
    __initialize_directory_descriptor(&directory->Base.Descriptor, &directory->Base.Stat);

    *directoryOut = (struct VaFsDirectory*)directory;
    return 0;
}

static void __directory_entry_destroy(struct VaFsDirectoryEntry* entry)
{
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
    
    if (__vafs_directory_entries(directory) == NULL) {
        return NULL;
    }
    return __vafs_directory_get(directory, token);
}

static struct VaFsDirectoryEntry* __find_entry_by_object_id(
    struct VaFsDirectory* directory,
    uint64_t             objectId);

int __vafs_directory_entry_stat(
    struct VaFsDirectoryEntry* entry,
    struct VaFsMetadata*       metadata)
{
    if (entry == NULL || metadata == NULL) {
        errno = EINVAL;
        return -1;
    }

    switch (entry->Type) {
        case VA_FS_DESCRIPTOR_TYPE_FILE:
            return __vafs_file_stat_internal(entry->File, metadata);
        case VA_FS_DESCRIPTOR_TYPE_DIRECTORY:
            return __vafs_directory_stat_internal(entry->Directory, metadata);
        case VA_FS_DESCRIPTOR_TYPE_SYMLINK:
            return __vafs_symlink_stat_internal(entry->Symlink, metadata);
        case VA_FS_DESCRIPTOR_TYPE_SPECIAL:
            return __vafs_special_stat_internal(entry->Special, metadata);
        case VA_FS_DESCRIPTOR_TYPE_HARDLINK:
            // Hardlinks intentionally borrow the target object's metadata so
            // open/stat callers observe shared-object semantics instead of a
            // second independent inode snapshot.
            entry = __vafs_resolve_hardlink(entry->Hardlink->VaFs, entry);
            if (entry == NULL) {
                return -1;
            }
            return __vafs_directory_entry_stat(entry, metadata);
        default:
            errno = EINVAL;
            return -1;
    }
}

static struct VaFsDirectoryEntry* __find_entry_by_object_id(
    struct VaFsDirectory* directory,
    uint64_t             objectId)
{
    struct VaFsDirectoryEntry* entry;
    struct VaFsMetadata        metadata;

    // Hardlink resolution stays tree-based on purpose so readers and writers
    // agree on one source of truth instead of maintaining a separate object-id
    // index that could drift from the materialized directory tree.
    entry = __vafs_directory_entries(directory);
    while (entry != NULL) {
        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
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

    entry = __find_entry_by_object_id(vafs->RootDirectory, entry->Hardlink->Descriptor.ObjectId);
    if (entry == NULL) {
        errno = ENOENT;
        return NULL;
    }
    return entry;
}

static void __descriptor_metadata_set_xattrs(
    VaFsDescriptorMetadata_t* descriptorMetadata,
    const struct VaFsXattrSet* xattrs)
{
    // Xattrs stay in a colder descriptor-stream section; hot entry metadata
    // only carries the stable set index needed to reach them lazily.
    descriptorMetadata->XattrIndex = (xattrs != NULL && xattrs->Count != 0) ?
        xattrs->Index :
        VA_FS_INVALID_XATTR_INDEX;
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

static int __get_descriptor_size(
    int type)
{
    switch (type) {
        case VA_FS_DESCRIPTOR_TYPE_FILE:
            return sizeof(VaFsFileDescriptor_t);
        case VA_FS_DESCRIPTOR_TYPE_DIRECTORY:
            return sizeof(VaFsDirectoryDescriptor_t);
        case VA_FS_DESCRIPTOR_TYPE_SYMLINK:
            return sizeof(VaFsSymlinkDescriptor_t);
        case VA_FS_DESCRIPTOR_TYPE_SPECIAL:
            return sizeof(VaFsSpecialDescriptor_t);
        case VA_FS_DESCRIPTOR_TYPE_HARDLINK:
            return sizeof(VaFsHardlinkDescriptor_t);
        default:
            return 0;
    }
}

static int __validate_descriptor_length(
    VaFsDescriptor_t* descriptor,
    int               expectedSize)
{
    // Descriptor length must be at least the base descriptor size
    if (descriptor->Length < sizeof(VaFsDescriptor_t)) {
        VAFS_ERROR("__validate_descriptor_length: descriptor length %u is less than minimum %zu\n",
            descriptor->Length, sizeof(VaFsDescriptor_t));
        return -1;
    }

    // Descriptor length must be at least the expected size for this type
    if (descriptor->Length < (uint16_t)expectedSize) {
        VAFS_ERROR("__validate_descriptor_length: descriptor length %u is less than expected %d for type %u\n",
            descriptor->Length, expectedSize, descriptor->Type);
        return -1;
    }

    // Bound variable-sized payloads before allocating trailing storage from
    // image data. Symlinks legitimately carry a full path-sized target while
    // the other descriptor kinds only carry entry names.
    if (descriptor->Type == VA_FS_DESCRIPTOR_TYPE_SYMLINK) {
        if (descriptor->Length > (uint16_t)(expectedSize + VAFS_NAME_MAX + VAFS_PATH_MAX)) {
            VAFS_ERROR("__validate_descriptor_length: descriptor length %u exceeds maximum %d\n",
                descriptor->Length, expectedSize + VAFS_NAME_MAX + VAFS_PATH_MAX);
            return -1;
        }
    } else if (descriptor->Length > (uint16_t)(expectedSize + VAFS_NAME_MAX)) {
        VAFS_ERROR("__validate_descriptor_length: descriptor length %u exceeds maximum %d\n",
            descriptor->Length, expectedSize + VAFS_NAME_MAX);
        return -1;
    }

    return 0;
}

static int __validate_file_descriptor(
    VaFsFileDescriptor_t* descriptor,
    const char*           extendedData)
{
    size_t nameLength;

    // Validate the descriptor length
    if (__validate_descriptor_length(&descriptor->Base, sizeof(VaFsFileDescriptor_t)) != 0) {
        return -1;
    }

    // Calculate and validate name length
    nameLength = descriptor->Base.Length - sizeof(VaFsFileDescriptor_t);
    if (nameLength == 0) {
        VAFS_ERROR("__validate_file_descriptor: file has no name\n");
        return -1;
    }

    if (nameLength > VAFS_NAME_MAX) {
        VAFS_ERROR("__validate_file_descriptor: file name length %zu exceeds maximum %d\n",
            nameLength, VAFS_NAME_MAX);
        return -1;
    }

    // Validate block position values
    if (descriptor->Data.Index != VA_FS_INVALID_BLOCK && descriptor->Data.Offset == VA_FS_INVALID_OFFSET) {
        VAFS_ERROR("__validate_file_descriptor: invalid block position: index=%u, offset=%u\n",
            descriptor->Data.Index, descriptor->Data.Offset);
        return -1;
    }

    return 0;
}

static int __validate_directory_descriptor(
    VaFsDirectoryDescriptor_t* descriptor,
    const char*                extendedData)
{
    size_t nameLength;

    // Validate the descriptor length
    if (__validate_descriptor_length(&descriptor->Base, sizeof(VaFsDirectoryDescriptor_t)) != 0) {
        return -1;
    }

    // Calculate and validate name length
    nameLength = descriptor->Base.Length - sizeof(VaFsDirectoryDescriptor_t);
    if (nameLength == 0) {
        VAFS_ERROR("__validate_directory_descriptor: directory has no name\n");
        return -1;
    }

    if (nameLength > VAFS_NAME_MAX) {
        VAFS_ERROR("__validate_directory_descriptor: directory name length %zu exceeds maximum %d\n",
            nameLength, VAFS_NAME_MAX);
        return -1;
    }

    return 0;
}

static int __validate_symlink_descriptor(
    VaFsSymlinkDescriptor_t* descriptor,
    const char*              extendedData)
{
    size_t totalExtendedLength;
    size_t expectedLength;

    // Validate the descriptor length
    if (__validate_descriptor_length(&descriptor->Base, sizeof(VaFsSymlinkDescriptor_t)) != 0) {
        return -1;
    }

    // Validate name and target lengths are non-zero
    if (descriptor->NameLength == 0) {
        VAFS_ERROR("__validate_symlink_descriptor: symlink has no name\n");
        return -1;
    }

    // Validate name length
    if (descriptor->NameLength > VAFS_NAME_MAX) {
        VAFS_ERROR("__validate_symlink_descriptor: name length %u exceeds maximum %d\n",
            descriptor->NameLength, VAFS_NAME_MAX);
        return -1;
    }

    // Validate target length
    if (descriptor->TargetLength > VAFS_PATH_MAX) {
        VAFS_ERROR("__validate_symlink_descriptor: target length %u exceeds maximum %d\n",
            descriptor->TargetLength, VAFS_PATH_MAX);
        return -1;
    }

    // Check for integer overflow in addition
    if ((uint32_t)descriptor->NameLength + (uint32_t)descriptor->TargetLength < descriptor->NameLength) {
        VAFS_ERROR("__validate_symlink_descriptor: integer overflow in name+target length\n");
        return -1;
    }

    // Validate that descriptor length matches the sum of base size + name + target
    totalExtendedLength = (size_t)descriptor->NameLength + descriptor->TargetLength;
    expectedLength = sizeof(VaFsSymlinkDescriptor_t) + totalExtendedLength;

    if (descriptor->Base.Length != expectedLength) {
        VAFS_ERROR("__validate_symlink_descriptor: descriptor length %u does not match expected %zu (base=%zu + name=%u + target=%u)\n",
            descriptor->Base.Length, expectedLength, sizeof(VaFsSymlinkDescriptor_t),
            descriptor->NameLength, descriptor->TargetLength);
        return -1;
    }

    return 0;
}

static int __validate_special_descriptor(
    VaFsSpecialDescriptor_t* descriptor,
    const char*              extendedData)
{
    size_t nameLength;
    enum VaFsEntryType entryType;

    (void)extendedData;

    if (__validate_descriptor_length(&descriptor->Base, sizeof(VaFsSpecialDescriptor_t)) != 0) {
        return -1;
    }

    nameLength = descriptor->Base.Length - sizeof(VaFsSpecialDescriptor_t);
    if (nameLength == 0) {
        VAFS_ERROR("__validate_special_descriptor: special entry has no name\n");
        return -1;
    }

    if (nameLength > VAFS_NAME_MAX) {
        VAFS_ERROR("__validate_special_descriptor: name length %zu exceeds maximum %d\n",
            nameLength, VAFS_NAME_MAX);
        return -1;
    }

    entryType = (enum VaFsEntryType)descriptor->EntryType;
    if (!__is_special_entry_type(entryType)) {
        VAFS_ERROR("__validate_special_descriptor: invalid special entry type %u\n",
            descriptor->EntryType);
        return -1;
    }

    // Device nodes cannot be reconstructed from mode bits alone, so images
    // must reject descriptors that would lose their major/minor identity.
    if ((entryType == VaFsEntryType_CharacterDevice || entryType == VaFsEntryType_BlockDevice) &&
        (descriptor->Metadata.Mask & VaFsMetadataMask_Device) == 0) {
        VAFS_ERROR("__validate_special_descriptor: device nodes require persisted major/minor metadata\n");
        return -1;
    }
    return 0;
}

static int __validate_hardlink_descriptor(
    VaFsHardlinkDescriptor_t* descriptor,
    const char*               extendedData)
{
    size_t nameLength;

    (void)extendedData;

    if (__validate_descriptor_length(&descriptor->Base, sizeof(VaFsHardlinkDescriptor_t)) != 0) {
        return -1;
    }

    if (descriptor->ObjectId == 0) {
        VAFS_ERROR("__validate_hardlink_descriptor: hardlink has no target object id\n");
        return -1;
    }

    if (descriptor->NameLength == 0) {
        VAFS_ERROR("__validate_hardlink_descriptor: hardlink has no name\n");
        return -1;
    }

    if (descriptor->NameLength > VAFS_NAME_MAX) {
        VAFS_ERROR("__validate_hardlink_descriptor: name length %u exceeds maximum %d\n",
            descriptor->NameLength, VAFS_NAME_MAX);
        return -1;
    }

    nameLength = descriptor->Base.Length - sizeof(VaFsHardlinkDescriptor_t);
    if (nameLength != descriptor->NameLength) {
        VAFS_ERROR("__validate_hardlink_descriptor: descriptor length %u does not match name length %u\n",
            descriptor->Base.Length, descriptor->NameLength);
        return -1;
    }
    return 0;
}

static int __read_descriptor(
    struct VaFsDirectoryReader* reader,
    char*                       buffer,
    char**                      extendedBufferOut)
{
    VaFsDescriptor_t* base = (VaFsDescriptor_t*)buffer;
    char*             ext  = (char*)buffer + sizeof(VaFsDescriptor_t);
    int               status;
    int               size;
    size_t            read;

    if (buffer == NULL || extendedBufferOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Consume one descriptor record from the reader-local cursor: the generic
    // base header first, then the fixed type-specific body, then any trailing
    // variable-length payload such as names or symlink targets.

    status = vafs_stream_reader_read(
        reader->Reader,
        buffer, sizeof(VaFsDescriptor_t),
        &read
    );
    if (status) {
        VAFS_ERROR("__read_descriptor: failed to read base descriptor: %i\n", status);
        return status;
    }

    VAFS_INFO("__read_descriptor: desciptor found type=%u, length=%u\n",
        base->Type, base->Length);
    
    size = __get_descriptor_size(base->Type);
    if (base->Length < sizeof(VaFsDescriptor_t) || !size) {
        // The fixed descriptor body must be at least the generic base header,
        // and the type must resolve to a known in-memory descriptor layout.
        VAFS_ERROR("__read_descriptor: invalid descriptor size: %i for type %i\n", base->Length, base->Type);
        errno = EINVAL;
        return -1;
    }

    if (base->Length > sizeof(VaFsDescriptor_t)) {
        VAFS_DEBUG("__read_descriptor: read %u/%u descriptor bytes, reading rest\n", 
            sizeof(VaFsDescriptor_t), size);

        // Some descriptor types have a larger fixed header than the generic
        // base descriptor, so read that fixed body before any trailing payload.

        status = vafs_stream_reader_read(
            reader->Reader,
            ext, size - sizeof(VaFsDescriptor_t),
            &read
        );
        if (status) {
            VAFS_ERROR("__read_descriptor: failed to read extension descriptor: %i\n", status);
            return status;
        }

        if (base->Length > size) {
            VAFS_DEBUG("__read_descriptor: read %u/%u bytes, reading descriptor extension data\n",
                size, base->Length);

            // Names and symlink targets live beyond the fixed descriptor body,
            // so preserve the trailing bytes separately for the constructors.
            // read rest of descriptor
            char* extendedBuffer = (char*)malloc(base->Length - size);
            if (!extendedBuffer) {
                VAFS_ERROR("__read_descriptor: failed to allocate extended buffer: %i\n", status);
                errno = ENOMEM;
                return -1;
            }

            status = vafs_stream_reader_read(
                reader->Reader,
                extendedBuffer, base->Length - size,
                &read
            );
            if (status) {
                VAFS_ERROR("__read_descriptor: failed to read extended data: %i\n", status);
                free(extendedBuffer);
                return status;
            }
            *extendedBufferOut = extendedBuffer;
        }
    }

    return 0;
}

// Don't use strdup here, since there may be data beyond length
static const char* __read_extended_string(const char* buffer, size_t length)
{
    char* str = (char*)malloc(length + 1);
    if (!str) {
        VAFS_ERROR("__read_extended_string: failed to allocate string\n");
        errno = ENOMEM;
        return NULL;
    }

    memcpy(str, buffer, length);
    str[length] = '\0';
    return str;
}

static struct VaFsFile* __create_file_from_descriptor(
    struct VaFs*          vafs,
    VaFsFileDescriptor_t* descriptor,
    const char*           extendedData)
{
    struct VaFsFile* file;

    // Validate the file descriptor
    if (__validate_file_descriptor(descriptor, extendedData) != 0) {
        errno = EINVAL;
        return NULL;
    }

    file = (struct VaFsFile*)calloc(1, sizeof(struct VaFsFile));
    if (!file) {
        return NULL;
    }

    memcpy(&file->Descriptor, descriptor, sizeof(VaFsFileDescriptor_t));
    file->Name = __read_extended_string(extendedData, descriptor->Base.Length - sizeof(VaFsFileDescriptor_t));
    file->VaFs = vafs;
    __materialize_descriptor_metadata(&descriptor->Metadata, VaFsEntryType_File, descriptor->FileLength, &file->Stat);
    file->StatCached = 1;
    return file;
}

static struct VaFsDirectory* __create_directory_from_descriptor(
    struct VaFs*               vafs,
    VaFsDirectoryDescriptor_t* descriptor,
    const char*                extendedData)
{
    struct VaFsDirectoryReader* directory;

    // Read-mode directories stay lazy: keep the on-disk descriptor now and
    // defer allocating a descriptor reader or parsing child entries until the
    // first traversal actually needs them.

    // Validate the directory descriptor
    if (__validate_directory_descriptor(descriptor, extendedData) != 0) {
        errno = EINVAL;
        return NULL;
    }

    directory = (struct VaFsDirectoryReader*)calloc(1, sizeof(struct VaFsDirectoryReader));
    if (!directory) {
        return NULL;
    }

    memcpy(&directory->Base.Descriptor, descriptor, sizeof(VaFsDirectoryDescriptor_t));
    // Non-root directories are discovered through a parent entry, so they do
    // not need a second header-facing descriptor anchor once reopened.
    directory->Base.DescriptorPosition.Index = VA_FS_INVALID_BLOCK;
    directory->Base.DescriptorPosition.Offset = VA_FS_INVALID_OFFSET;

    directory->State     = VaFsDirectoryState_Open;
    directory->IndexDirty = 1;
    directory->Base.Name = __read_extended_string(extendedData, descriptor->Base.Length - sizeof(VaFsDirectoryDescriptor_t));
    directory->Base.VaFs = vafs;
    __materialize_descriptor_metadata(&descriptor->Metadata, VaFsEntryType_Directory, 0, &directory->Base.Stat);
    directory->Base.StatCached = 1;
    return &directory->Base;
}

static struct VaFsSymlink* __create_symlink_from_descriptor(
    struct VaFs*             vafs,
    VaFsSymlinkDescriptor_t* descriptor,
    const char*              extendedData)
{
    struct VaFsSymlink* symlink;

    // Validate the symlink descriptor
    if (__validate_symlink_descriptor(descriptor, extendedData) != 0) {
        errno = EINVAL;
        return NULL;
    }

    symlink = (struct VaFsSymlink*)calloc(1, sizeof(struct VaFsSymlink));
    if (!symlink) {
        return NULL;
    }

    memcpy(&symlink->Descriptor, descriptor, sizeof(VaFsSymlinkDescriptor_t));
    symlink->Name   = __read_extended_string(extendedData, descriptor->NameLength);
    symlink->Target = __read_extended_string(extendedData + descriptor->NameLength, descriptor->TargetLength);
    symlink->VaFs   = vafs;
    __materialize_descriptor_metadata(&descriptor->Metadata, VaFsEntryType_Symlink, descriptor->TargetLength, &symlink->Stat);
    symlink->StatCached = 1;
    return symlink;
}

static struct VaFsSpecial* __create_special_from_descriptor(
    struct VaFs*             vafs,
    VaFsSpecialDescriptor_t* descriptor,
    const char*              extendedData)
{
    struct VaFsSpecial* special;

    if (__validate_special_descriptor(descriptor, extendedData) != 0) {
        errno = EINVAL;
        return NULL;
    }

    special = (struct VaFsSpecial*)calloc(1, sizeof(struct VaFsSpecial));
    if (!special) {
        return NULL;
    }

    memcpy(&special->Descriptor, descriptor, sizeof(VaFsSpecialDescriptor_t));
    special->Name = __read_extended_string(extendedData, descriptor->Base.Length - sizeof(VaFsSpecialDescriptor_t));
    special->VaFs = vafs;
    // Special entries persist an explicit subtype so reload does not have to
    // guess character-vs-block-vs-fifo meaning from platform-specific mode
    // interpretations alone.
    __materialize_descriptor_metadata(
        &descriptor->Metadata,
        (enum VaFsEntryType)descriptor->EntryType,
        0,
        &special->Stat
        // Peek only at the first descriptor header so the reader can distinguish
        // the new "header -> root descriptor" layout from the older
        // "header -> root child list" layout without forking the whole loader.
    );
    special->StatCached = 1;
    return special;
}

static struct VaFsHardlink* __create_hardlink_from_descriptor(
    struct VaFs*              vafs,
    VaFsHardlinkDescriptor_t* descriptor,
    const char*               extendedData)
{
    struct VaFsHardlink* hardlink;

    if (__validate_hardlink_descriptor(descriptor, extendedData) != 0) {
        errno = EINVAL;
        return NULL;
    }

    hardlink = (struct VaFsHardlink*)calloc(1, sizeof(struct VaFsHardlink));
    if (!hardlink) {
        return NULL;
    }

    // Hardlink descriptors stay intentionally thin; resolving the target on
    // demand keeps every alias tied to one canonical metadata record.
    memcpy(&hardlink->Descriptor, descriptor, sizeof(VaFsHardlinkDescriptor_t));
    hardlink->Name = __read_extended_string(extendedData, descriptor->NameLength);
    hardlink->VaFs = vafs;
    return hardlink;
}

static struct VaFsDirectoryEntry* __create_entry_from_descriptor(
    struct VaFs*      vafs,
    VaFsDescriptor_t* descriptor,
    const char*       extendedData)
{
    struct VaFsDirectoryEntry* entry;
    
    entry = (struct VaFsDirectoryEntry*)calloc(1, sizeof(struct VaFsDirectoryEntry));
    if (!entry) {
        return NULL;
    }

    // Convert the raw descriptor into the matching in-memory entry wrapper so
    // later traversal can branch on one normalized entry type.
    entry->Type = descriptor->Type;
    if (entry->Type == VA_FS_DESCRIPTOR_TYPE_FILE) {
        entry->File = __create_file_from_descriptor(vafs, (VaFsFileDescriptor_t*)descriptor, extendedData);
    } else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
        entry->Directory = __create_directory_from_descriptor(vafs, (VaFsDirectoryDescriptor_t*)descriptor, extendedData);
    } else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_SYMLINK) {
        entry->Symlink = __create_symlink_from_descriptor(vafs, (VaFsSymlinkDescriptor_t*)descriptor, extendedData);
    } else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_SPECIAL) {
        entry->Special = __create_special_from_descriptor(vafs, (VaFsSpecialDescriptor_t*)descriptor, extendedData);
    } else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_HARDLINK) {
        entry->Hardlink = __create_hardlink_from_descriptor(vafs, (VaFsHardlinkDescriptor_t*)descriptor, extendedData);
    } else {
        free(entry);
        errno = EINVAL;
        return NULL;
    }
    return entry;
}

static int __load_directory(
    struct VaFsDirectoryReader* reader)
{
    VaFsDirectoryHeader_t header;
    int                   status;
    size_t                read;

    VAFS_DEBUG("__load_directory(directory=%s)\n", reader->Base.Name);

    // Materialize the directory once: create a private descriptor cursor if
    // needed, seek to the directory payload, stream entries in order, and then
    // build whichever lookup accelerator matches the directory size.

    // if the directory has no entries, we can skip loading it
    if (reader->Base.Descriptor.Descriptor.Index == VA_FS_INVALID_BLOCK) {
        // Empty directories have no persisted descriptor payload to parse.
        reader->State = VaFsDirectoryState_Loaded;
        return 0;
    }

    if (reader->Reader == NULL) {
        // The descriptor stream reader is allocated lazily so unopened
        // directories do not pay for a cursor or block buffer up front.
        status = vafs_stream_reader_open(reader->Base.VaFs->DescriptorStream, &reader->Reader);
        if (status != 0) {
            VAFS_ERROR("__load_directory: failed to create stream reader\n");
            return status;
        }
    }

    status = vafs_stream_reader_seek(
        reader->Reader,
        reader->Base.Descriptor.Descriptor.Index,
        reader->Base.Descriptor.Descriptor.Offset
    );
    if (status) {
        VAFS_ERROR("__load_directory: failed to seek to directory data\n");
        return status;
    }

    // read the directory descriptor
    status = vafs_stream_reader_read(
        reader->Reader,
        &header, sizeof(VaFsDirectoryHeader_t),
        &read
    );
    if (status) {
        VAFS_ERROR("__load_directory: failed to read directory header\n");
        return status;
    }

    // Validate directory entry count is reasonable
    if (header.Count > VAFS_MAX_DIRECTORY_ENTRIES) {
        VAFS_ERROR("__load_directory: directory entry count %u exceeds maximum %d\n",
            header.Count, VAFS_MAX_DIRECTORY_ENTRIES);
        errno = EINVAL;
        return -1;
    }

    // read the directory entries
    VAFS_INFO("__load_directory: reading %u entries\n", header.Count);
    for (uint32_t i = 0; i < header.Count; i++) {
        struct VaFsDirectoryEntry* entry;
        VaFsEntryDescriptorScratch_t descriptorScratch;
        char*                      extendedData = NULL;
        VAFS_INFO("__load_directory: reading entry %i/%u\n", i, header.Count);
        // Each loop iteration consumes one on-disk descriptor and immediately
        // converts it into the normalized in-memory entry type.
        
        status = __read_descriptor(reader, (char*)&descriptorScratch, &extendedData);
        if (status) {
            VAFS_ERROR("__load_directory: failed to read descriptor\n");
            return status;
        }

        // create a new entry
        entry = __create_entry_from_descriptor(reader->Base.VaFs, &descriptorScratch.Base, extendedData);
        free(extendedData);

        if (!entry) {
            VAFS_ERROR("__load_directory: failed to create entry\n");
            return -1;
        }

        // add the entry to the directory
        entry->Link = reader->Entries;
        reader->Entries = entry;
        reader->EntryCount++;
    }

    reader->State = VaFsDirectoryState_Loaded;
    reader->IndexDirty = 1;
    return __vafs_directory_index_build(reader);
}

int vafs_directory_open_root(
    struct VaFs*           vafs,
    VaFsBlockPosition_t*   position,
    struct VaFsDirectory** directoryOut)
{
    struct VaFsDirectoryReader  probe;
    struct VaFsDirectoryReader* reader;
    struct VaFsStreamReader*    streamReader;
    VaFsDescriptor_t            descriptorBase;
    VaFsEntryDescriptorScratch_t descriptorScratch;
    char*                       extendedData = NULL;
    int                         status;
    size_t                      read;

    // Root opens as a lazy read-mode wrapper around the persisted root
    // descriptor position stored in the filesystem header. New images point at
    // a real directory descriptor so root metadata can round-trip like every
    // other directory, but older images still point directly at the child list.
    
    if (vafs == NULL || position == NULL || directoryOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    VAFS_DEBUG("vafs_directory_open_root(pos=%u/%u)\n", position->Index, position->Offset);

    status = vafs_stream_reader_open(vafs->DescriptorStream, &streamReader);
    if (status != 0) {
        return status;
    }

    status = vafs_stream_reader_seek(streamReader, position->Index, position->Offset);
    if (status != 0) {
        vafs_stream_reader_close(streamReader);
        return status;
    }

    status = vafs_stream_reader_read(streamReader, &descriptorBase, sizeof(VaFsDescriptor_t), &read);
    if (status != 0 || read != sizeof(VaFsDescriptor_t)) {
        vafs_stream_reader_close(streamReader);
        errno = EINVAL;
        return -1;
    }

    if (descriptorBase.Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY &&
        descriptorBase.Length >= sizeof(VaFsDirectoryDescriptor_t)) {
        // New images persist root exactly like any other directory, so reuse
        // the normal directory-descriptor materialization path when possible.
        memset(&probe, 0, sizeof(struct VaFsDirectoryReader));
        probe.Base.VaFs = vafs;
        probe.Reader = streamReader;

        status = vafs_stream_reader_seek(streamReader, position->Index, position->Offset);
        if (status != 0) {
            vafs_stream_reader_close(streamReader);
            return status;
        }

        status = __read_descriptor(&probe, (char*)&descriptorScratch, &extendedData);
        vafs_stream_reader_close(streamReader);
        if (status != 0) {
            free(extendedData);
            return status;
        }

        *directoryOut = __create_directory_from_descriptor(
            vafs,
            &descriptorScratch.Directory,
            extendedData
        );
        free(extendedData);
        if (*directoryOut == NULL) {
            return -1;
        }

        (*directoryOut)->DescriptorPosition = *position;
        return 0;
    }

    vafs_stream_reader_close(streamReader);
    // Fall back to the synthetic root wrapper only for legacy images whose
    // header points straight at the child list. That preserves older layouts
    // without forcing the rest of the codebase to carry two root models.
    
    reader = (struct VaFsDirectoryReader*)calloc(1, sizeof(struct VaFsDirectoryReader));
    if (!reader) {
        VAFS_ERROR("vafs_directory_open_root: failed to allocate directory reader\n");
        return -1;
    }

    reader->Base.VaFs = vafs;
    reader->Base.Name = strdup("root");
    reader->State     = VaFsDirectoryState_Open;
    reader->IndexDirty = 1;
    memset(&reader->NameIndex, 0, sizeof(hashtable_t));
    reader->Base.DescriptorPosition.Index = VA_FS_INVALID_BLOCK;
    reader->Base.DescriptorPosition.Offset = VA_FS_INVALID_OFFSET;
    __initialize_root_metadata(&reader->Base.Stat);
    reader->Base.StatCached = 1;
    
    // Legacy fallback keeps the descriptor payload position aimed at the child
    // list because there is no standalone root descriptor to reopen.
    reader->Base.Descriptor.Base.Length = sizeof(VaFsDirectoryDescriptor_t);
    reader->Base.Descriptor.Base.Type   = VA_FS_DESCRIPTOR_TYPE_DIRECTORY;
    reader->Base.Descriptor.Descriptor.Index = position->Index;
    reader->Base.Descriptor.Descriptor.Offset = position->Offset;
    __descriptor_metadata_initialize(&reader->Base.Descriptor.Metadata, &reader->Base.Stat);

    *directoryOut = (struct VaFsDirectory*)reader;
    return 0;
}

struct VaFsDirectoryEntry* __vafs_directory_entries(
    struct VaFsDirectory* directory)
{
    VAFS_INFO("__vafs_directory_entries(directory=%s)\n", directory->Name);
    if (directory->VaFs->Mode == VaFsMode_Read) {
        struct VaFsDirectoryReader* reader = (struct VaFsDirectoryReader*)directory;
        if (reader->State != VaFsDirectoryState_Loaded) {
            if (__load_directory(reader)) {
                VAFS_ERROR("__vafs_directory_entries: directory not loaded\n");
                return NULL;
            }
        }
        return reader->Entries;
    } else {
        struct VaFsDirectoryWriter* writer = (struct VaFsDirectoryWriter*)directory;
        return writer->Entries;
    }
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

static int __write_directory_header(
    struct VaFsDirectoryWriter* writer,
    int                         count)
{
    VaFsDirectoryHeader_t header;
    VAFS_DEBUG("vafs_directory_write_header(count=%d)\n", count);

    header.Count = count;

    return vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        &header,
        sizeof(VaFsDirectoryHeader_t)
    );
}

static int __write_file_descriptor(
    struct VaFsDirectoryWriter* writer,
    struct VaFsDirectoryEntry*  entry)
{
    int status;
    VAFS_DEBUG("vafs_directory_write_file_descriptor(name=%s)\n",
        entry->File->Name);

    // Rebuild the hot descriptor as late as possible so deferred metadata such
    // as xattr counts and deduplicated xattr indices cannot drift from the file
    // state that is about to be serialized.
    __finalize_entry_metadata(&entry->File->Stat, VaFsEntryType_File, entry->File->Descriptor.FileLength);
    __descriptor_metadata_initialize(&entry->File->Descriptor.Metadata, &entry->File->Stat);
    __descriptor_metadata_set_xattrs(&entry->File->Descriptor.Metadata, entry->File->Xattrs);
    entry->File->Descriptor.Base.Length = (uint16_t)(sizeof(VaFsFileDescriptor_t) + strlen(entry->File->Name));

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        &entry->File->Descriptor,
        sizeof(VaFsFileDescriptor_t)
    );
    if (status != 0) {
        return status;
    }

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        entry->File->Name,
        strlen(entry->File->Name)
    );
    return status;
}

static int __write_directory_descriptor(
    struct VaFsDirectoryWriter* writer,
    struct VaFsDirectoryEntry*  entry)
{
    int status;
    VAFS_DEBUG("vafs_directory_write_directory_descriptor(name=%s)\n",
        entry->Directory->Name);
    
    // Directory metadata is refreshed at flush time for the same reason as
    // files: the hot descriptor must reflect the final cold xattr indexing.
    __finalize_entry_metadata(&entry->Directory->Stat, VaFsEntryType_Directory, 0);
    __descriptor_metadata_initialize(&entry->Directory->Descriptor.Metadata, &entry->Directory->Stat);
    __descriptor_metadata_set_xattrs(&entry->Directory->Descriptor.Metadata, entry->Directory->Xattrs);
    entry->Directory->Descriptor.Base.Length = (uint16_t)(sizeof(VaFsDirectoryDescriptor_t) + strlen(entry->Directory->Name));

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        &entry->Directory->Descriptor,
        sizeof(VaFsDirectoryDescriptor_t)
    );
    if (status != 0) {
        return status;
    }

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        entry->Directory->Name,
        strlen(entry->Directory->Name)
    );
    return status;
}

static int __write_symlink_descriptor(
    struct VaFsDirectoryWriter* writer,
    struct VaFsDirectoryEntry*  entry)
{
    int status;
    VAFS_DEBUG("__write_symlink_descriptor(name=%s)\n",
        entry->Symlink->Name);

    // Symlink payload lengths and xattr indices are both finalized here so the
    // descriptor stays a faithful snapshot of the exact target text being emitted.
    entry->Symlink->Descriptor.NameLength   = (uint16_t)strlen(entry->Symlink->Name);
    entry->Symlink->Descriptor.TargetLength = (uint16_t)strlen(entry->Symlink->Target);
    __finalize_entry_metadata(&entry->Symlink->Stat, VaFsEntryType_Symlink, entry->Symlink->Descriptor.TargetLength);
    __descriptor_metadata_initialize(&entry->Symlink->Descriptor.Metadata, &entry->Symlink->Stat);
    __descriptor_metadata_set_xattrs(&entry->Symlink->Descriptor.Metadata, entry->Symlink->Xattrs);
    entry->Symlink->Descriptor.Base.Length = (uint16_t)(sizeof(VaFsSymlinkDescriptor_t) + entry->Symlink->Descriptor.NameLength + entry->Symlink->Descriptor.TargetLength);

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        &entry->Symlink->Descriptor,
        sizeof(VaFsSymlinkDescriptor_t)
    );
    if (status != 0) {
        return status;
    }

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        entry->Symlink->Name,
        strlen(entry->Symlink->Name)
    );
    if (status != 0) {
        return status;
    }

    if (entry->Symlink->Descriptor.TargetLength == 0) {
        return 0;
    }

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        entry->Symlink->Target,
        strlen(entry->Symlink->Target)
    );
    return status;
}

static int __write_special_descriptor(
    struct VaFsDirectoryWriter* writer,
    struct VaFsDirectoryEntry*  entry)
{
    int status;

    VAFS_DEBUG("__write_special_descriptor(name=%s)\n", entry->Special->Name);

    // Persist the explicit entry subtype because host recreation cares about
    // more than permission bits when deciding which special node to create.
    // Like the other descriptor writers, rebuild the hot metadata here so the
    // special node sees the same final xattr indexing as regular files do.
    __finalize_entry_metadata(&entry->Special->Stat, entry->Special->Stat.Type, 0);
    __descriptor_metadata_initialize(&entry->Special->Descriptor.Metadata, &entry->Special->Stat);
    __descriptor_metadata_set_xattrs(&entry->Special->Descriptor.Metadata, entry->Special->Xattrs);
    entry->Special->Descriptor.EntryType = (uint16_t)entry->Special->Stat.Type;
    entry->Special->Descriptor.Base.Length = (uint16_t)(sizeof(VaFsSpecialDescriptor_t) + strlen(entry->Special->Name));

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        &entry->Special->Descriptor,
        sizeof(VaFsSpecialDescriptor_t)
    );
    if (status != 0) {
        return status;
    }

    return vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        entry->Special->Name,
        strlen(entry->Special->Name)
    );
}

static int __write_hardlink_descriptor(
    struct VaFsDirectoryWriter* writer,
    struct VaFsDirectoryEntry*  entry)
{
    int status;

    VAFS_DEBUG("__write_hardlink_descriptor(name=%s)\n", entry->Hardlink->Name);

    // Only the alias name and target object id are serialized here because
    // duplicating metadata would let aliases diverge from their shared target.
    entry->Hardlink->Descriptor.NameLength = (uint16_t)strlen(entry->Hardlink->Name);
    entry->Hardlink->Descriptor.Base.Length = (uint16_t)(sizeof(VaFsHardlinkDescriptor_t) + entry->Hardlink->Descriptor.NameLength);

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        &entry->Hardlink->Descriptor,
        sizeof(VaFsHardlinkDescriptor_t)
    );
    if (status != 0) {
        return status;
    }

    return vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        entry->Hardlink->Name,
        strlen(entry->Hardlink->Name)
    );
}

int vafs_directory_write_root_descriptor(
    struct VaFsDirectory* directory)
{
    struct VaFsDirectoryWriter* writer;
    struct VaFsDirectoryEntry   entry;
    int                         status;
    vafsblock_t                 block;
    uint32_t                    offset;

    if (directory == NULL || directory->VaFs->Mode != VaFsMode_Write) {
        errno = EINVAL;
        return -1;
    }

    writer = (struct VaFsDirectoryWriter*)directory;
    status = vafs_stream_position(writer->Base.VaFs->DescriptorStream, &block, &offset);
    if (status != 0) {
        return status;
    }

    // The root has no parent entry to carry its metadata, so it gets one final
    // standalone hot descriptor that the image header can point at directly.
    // That descriptor is written after the child list so its directory pointer
    // can name the already-flushed root payload with final coordinates.
    directory->DescriptorPosition.Index = block;
    directory->DescriptorPosition.Offset = offset;

    // Reuse the ordinary directory descriptor writer so root stays subject to
    // the same metadata finalization rules as every non-root directory entry.
    memset(&entry, 0, sizeof(struct VaFsDirectoryEntry));
    entry.Type = VA_FS_DESCRIPTOR_TYPE_DIRECTORY;
    entry.Directory = directory;
    return __write_directory_descriptor(writer, &entry);
}

int vafs_directory_flush(
    struct VaFsDirectory* directory)
{
    struct VaFsDirectoryWriter* writer = (struct VaFsDirectoryWriter*)directory;
    struct VaFsDirectoryEntry*  entry;
    int                         status;
    int                         entryCount = 0;
    vafsblock_t                 block;
    uint32_t                    offset;
    VAFS_DEBUG("vafs_directory_flush(name=%s)\n", directory->Name);

    // Parent descriptors must point at already-stable child coordinates, so
    // nested directories flush first and only then can this directory name
    // their final descriptor positions.
    entry = writer->Entries;
    while (entry != NULL) {
        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
            // flush the directory
            status = vafs_directory_flush(entry->Directory);
            if (status) {
                return status;
            }
        }
        entryCount++;
        entry = entry->Link;
    }

    // get current stream position;
    status = vafs_stream_position(
        writer->Base.VaFs->DescriptorStream,
        &block, &offset
    );
    if (status) {
        VAFS_ERROR("vafs_directory_flush: failed to get stream position\n");
        return status;
    }

    // Record where this directory's child list begins before emitting it so
    // parent descriptors, and the later root-descriptor pass, can reference
    // the finalized payload instead of a placeholder location.
    directory->Descriptor.Descriptor.Index  = block;
    directory->Descriptor.Descriptor.Offset = offset;
    VAFS_DEBUG("vafs_directory_flush  name=%s index=%d offset=%d\n", directory->Name, block, offset);

    status = __write_directory_header(writer, entryCount);
    if (status) {
        VAFS_ERROR("vafs_directory_flush: failed to write directory header\n");
        return status;
    }

    // Emit the contiguous child descriptor list only after the header is in
    // place so readers can trust the recorded entry count while walking it.
    entry = writer->Entries;
    while (entry != NULL) {
        VAFS_DEBUG("vafs_directory_flush: writing entry=%s, type=%i\n",
            __vafs_directory_entry_name(entry), entry->Type);
        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_FILE) {
            status = __write_file_descriptor(writer, entry);
        } else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
            status = __write_directory_descriptor(writer, entry);
        } else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_SYMLINK) {
            status = __write_symlink_descriptor(writer, entry);
        } else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_SPECIAL) {
            status = __write_special_descriptor(writer, entry);
        } else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_HARDLINK) {
            status = __write_hardlink_descriptor(writer, entry);
        } else {
            VAFS_ERROR("vafs_directory_flush: unknown descriptor type\n");
            return -1;
        }

        if (status) {
            VAFS_ERROR("vafs_directory_flush: failed to write descriptor: %i\n", status);
            return status;
        }
        entry = entry->Link;
    }
    return 0;
}

int vafs_directory_stat(
    struct VaFsDirectoryHandle* handle,
    struct VaFsMetadata*        metadata)
{
    if (handle == NULL || metadata == NULL) {
        errno = EINVAL;
        return -1;
    }

    return __vafs_directory_stat_internal(handle->Directory, metadata);
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

int vafs_directory_close(
    struct VaFsDirectoryHandle* handle)
{
    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }

    // free handle
    free(handle);
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

int vafs_directory_create_directory(
    struct VaFsDirectoryHandle*  handle,
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

int vafs_directory_create_file(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    const struct VaFsMetadata*  metadata,
    struct VaFsFileHandle**     handleOut)
{
    struct VaFsDirectoryWriter* writer;
    int                         status;
    struct VaFsDirectoryEntry*  entry;
    struct VaFsMetadata         preparedMetadata;
    char                        token[VAFS_NAME_MAX + 1];
    VAFS_DEBUG("vafs_directory_create_file(name=%s)\n", name);

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
    entry = __vafs_directory_find_entry(handle->Directory, token);
    
    *handleOut = vafs_file_create_handle(entry->File);
    return 0;
}

int vafs_directory_create_symlink(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    const char*                 target,
    const struct VaFsMetadata*  metadata)
{
    struct VaFsDirectoryEntry* entry;
    char                       token[VAFS_NAME_MAX + 1];
    struct VaFsMetadata        preparedMetadata;
    size_t                     targetLength;
    VAFS_DEBUG("vafs_directory_create_symlink(name=%s, target=%s)\n", name, target);

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
    VAFS_DEBUG("vafs_directory_create_symlink: locating %s\n", token);
    entry = __vafs_directory_find_entry(handle->Directory, token);
    if (entry == NULL) {
        struct VaFsDirectoryWriter* writer = (struct VaFsDirectoryWriter*)handle->Directory;
        return __create_symlink_entry(writer, token, target, &preparedMetadata);
    }

    errno = EEXIST;
    return -1;
}

int vafs_directory_create_special(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    const struct VaFsMetadata*  metadata)
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
    return __create_hardlink_entry(writer, token, objectId);
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
