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

struct __directory_name_index_entry {
    const char*                Name;
    struct VaFsDirectoryEntry* Entry;
};

struct VaFsDirectoryHandle {
    struct VaFsDirectory* Directory;
    int                   Index;
};

static uint64_t __directory_lookup_cache_hash(
    const struct VaFsDirectory* directory,
    const char*                 token)
{
    const unsigned char* name = (const unsigned char*)token;
    uintptr_t            parent = (uintptr_t)directory;
    uint64_t             hash = 1469598103934665603ULL;

    for (size_t i = 0; i < sizeof(parent); i++) {
        hash ^= (uint64_t)((parent >> (i * 8)) & 0xFF);
        hash *= 1099511628211ULL;
    }

    while (*name != '\0') {
        hash ^= (uint64_t)*name++;
        hash *= 1099511628211ULL;
    }
    return hash;
}

size_t __vafs_directory_lookup_cache_set(
    const struct VaFsDirectory* directory,
    const char*                 token)
{
    return (size_t)(__directory_lookup_cache_hash(directory, token) & (VAFS_LOOKUP_CACHE_SET_COUNT - 1));
}

static uint64_t __directory_lookup_cache_generation(
    struct VaFsLookupCache* cache)
{
    cache->Generation++;
    if (cache->Generation == 0) {
        cache->Generation = 1;
    }
    return cache->Generation;
}

static struct VaFsLookupCacheEntry* __directory_lookup_cache_entries(
    struct VaFs* vafs,
    size_t       setIndex)
{
    return &vafs->LookupCache.Entries[setIndex * VAFS_LOOKUP_CACHE_SET_ASSOCIATIVITY];
}

static int __directory_lookup_cache_get(
    struct VaFsDirectory*       directory,
    const char*                 token,
    struct VaFsDirectoryEntry** entryOut)
{
    struct VaFsLookupCache*      cache;
    struct VaFsLookupCacheEntry* entries;
    size_t                       setIndex;

    if (directory->VaFs->Mode != VaFsMode_Read) {
        return 0;
    }

    cache = &directory->VaFs->LookupCache;
    setIndex = __vafs_directory_lookup_cache_set(directory, token);
    entries = __directory_lookup_cache_entries(directory->VaFs, setIndex);
    for (size_t i = 0; i < VAFS_LOOKUP_CACHE_SET_ASSOCIATIVITY; i++) {
        if (entries[i].State == VaFsLookupCacheState_Empty) {
            continue;
        }

        if (entries[i].Parent != directory || strcmp(entries[i].Name, token) != 0) {
            continue;
        }

        entries[i].Generation = __directory_lookup_cache_generation(cache);
        if (entries[i].State == VaFsLookupCacheState_Miss) {
            errno = ENOENT;
            *entryOut = NULL;
            return 1;
        }

        *entryOut = entries[i].Entry;
        return 1;
    }
    return 0;
}

static void __directory_lookup_cache_store(
    struct VaFsDirectory*      directory,
    const char*                token,
    struct VaFsDirectoryEntry* entry)
{
    struct VaFsLookupCache*      cache;
    struct VaFsLookupCacheEntry* entries;
    struct VaFsLookupCacheEntry* target;
    size_t                       setIndex;

    if (directory->VaFs->Mode != VaFsMode_Read) {
        return;
    }

    cache = &directory->VaFs->LookupCache;
    setIndex = __vafs_directory_lookup_cache_set(directory, token);
    entries = __directory_lookup_cache_entries(directory->VaFs, setIndex);
    target = &entries[0];

    for (size_t i = 0; i < VAFS_LOOKUP_CACHE_SET_ASSOCIATIVITY; i++) {
        if (entries[i].State == VaFsLookupCacheState_Empty) {
            target = &entries[i];
            break;
        }

        if (entries[i].Parent == directory && strcmp(entries[i].Name, token) == 0) {
            target = &entries[i];
            break;
        }

        if (target->State != VaFsLookupCacheState_Empty && entries[i].Generation < target->Generation) {
            target = &entries[i];
        }
    }

    target->Parent = directory;
    target->Entry = entry;
    target->Generation = __directory_lookup_cache_generation(cache);
    target->State = entry != NULL ? VaFsLookupCacheState_Hit : VaFsLookupCacheState_Miss;
    memcpy(target->Name, token, strlen(token) + 1);
}

static void __initialize_file_descriptor(
    VaFsFileDescriptor_t* descriptor,
    uint32_t              permissions)
{
    descriptor->Base.Type = VA_FS_DESCRIPTOR_TYPE_FILE;
    descriptor->Base.Length = sizeof(VaFsFileDescriptor_t);

    descriptor->Data.Index = VA_FS_INVALID_BLOCK;
    descriptor->Data.Offset = VA_FS_INVALID_OFFSET;
    descriptor->FileLength = 0;
    descriptor->Permissions = permissions;
}

static void __initialize_directory_descriptor(
    VaFsDirectoryDescriptor_t* descriptor,
    uint32_t                   permissions)
{
    descriptor->Base.Type = VA_FS_DESCRIPTOR_TYPE_DIRECTORY;
    descriptor->Base.Length = sizeof(VaFsDirectoryDescriptor_t);

    descriptor->Descriptor.Index = VA_FS_INVALID_BLOCK;
    descriptor->Descriptor.Offset = VA_FS_INVALID_OFFSET;
    descriptor->Permissions = permissions;
}

static void __initialize_symlink_descriptor(
    VaFsSymlinkDescriptor_t* descriptor)
{
    descriptor->Base.Type = VA_FS_DESCRIPTOR_TYPE_SYMLINK;
    descriptor->Base.Length = sizeof(VaFsSymlinkDescriptor_t);

    descriptor->NameLength = 0;
    descriptor->TargetLength = 0;
}

static int __vafs_file_stat_internal(
    struct VaFsFile*  file,
    struct vafs_stat* stat)
{
    if (file->VaFs->Mode == VaFsMode_Read && file->StatCached) {
        *stat = file->Stat;
        return 0;
    }

    stat->mode = S_IFREG | file->Descriptor.Permissions;
    stat->size = file->Descriptor.FileLength;
    if (file->VaFs->Mode == VaFsMode_Read) {
        file->Stat = *stat;
        file->StatCached = 1;
    }
    return 0;
}

static int __vafs_directory_stat_internal(
    struct VaFsDirectory* directory,
    struct vafs_stat*     stat)
{
    if (directory->VaFs->Mode == VaFsMode_Read && directory->StatCached) {
        *stat = directory->Stat;
        return 0;
    }

    stat->mode = S_IFDIR | directory->Descriptor.Permissions;
    stat->size = 0;
    if (directory->VaFs->Mode == VaFsMode_Read) {
        directory->Stat = *stat;
        directory->StatCached = 1;
    }
    return 0;
}

static int __vafs_symlink_stat_internal(
    struct VaFsSymlink* symlink,
    struct vafs_stat*   stat)
{
    if (symlink->VaFs->Mode == VaFsMode_Read && symlink->StatCached) {
        *stat = symlink->Stat;
        return 0;
    }

    stat->mode = S_IFLNK | 0777;
    stat->size = strlen(symlink->Target);
    if (symlink->VaFs->Mode == VaFsMode_Read) {
        symlink->Stat = *stat;
        symlink->StatCached = 1;
    }
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
    directory->Base.StatCached = 0;
    directory->Index = NULL;
    directory->EntryCount = 0;
    directory->IndexDirty = 1;

    // rwxrwxr-x
    __initialize_directory_descriptor(&directory->Base.Descriptor, 0775);

    *directoryOut = (struct VaFsDirectory*)directory;
    return 0;
}

static void __directory_entry_destroy(struct VaFsDirectoryEntry* entry)
{
    switch (entry->Type) {
        case VaFsEntryType_Directory:
            vafs_directory_destroy(entry->Directory);
            break;
        case VaFsEntryType_File:
            vafs_file_destroy(entry->File);
            break;
        case VaFsEntryType_Symlink:
            vafs_symlink_destroy(entry->Symlink);
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

static void __free_directory_index(struct VaFsDirectoryEntry** index)
{
    free(index);
}

static void __free_directory_name_index(struct VaFsDirectoryReader* reader)
{
    if (reader->NameIndexInitialized) {
        vafs_hashtable_destroy(&reader->NameIndex);
        memset(&reader->NameIndex, 0, sizeof(hashtable_t));
        reader->NameIndexInitialized = 0;
    }
}

static void __directory_reader_destroy(struct VaFsDirectoryReader* reader)
{
    __cleanup_directory_entries(reader->Entries);
    __free_directory_index(reader->Index);
    __free_directory_name_index(reader);
}

static void __directory_writer_destroy(struct VaFsDirectoryWriter* writer)
{
    __cleanup_directory_entries(writer->Entries);
    __free_directory_index(writer->Index);
}

static int __compare_directory_entries(
    const void* lhs,
    const void* rhs)
{
    const struct VaFsDirectoryEntry* left = *(const struct VaFsDirectoryEntry* const*)lhs;
    const struct VaFsDirectoryEntry* right = *(const struct VaFsDirectoryEntry* const*)rhs;
    const char* leftName = __vafs_directory_entry_name((struct VaFsDirectoryEntry*)left);
    const char* rightName = __vafs_directory_entry_name((struct VaFsDirectoryEntry*)right);

    return strcmp(leftName, rightName);
}

static uint64_t __directory_name_hash(
    const void* element)
{
    const struct __directory_name_index_entry* indexEntry = element;
    const unsigned char*                       name = (const unsigned char*)indexEntry->Name;
    uint64_t                                   hash = 1469598103934665603ULL;

    while (*name != '\0') {
        hash ^= (uint64_t)*name++;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static int __directory_name_cmp(
    const void* lhs,
    const void* rhs)
{
    const struct __directory_name_index_entry* left = lhs;
    const struct __directory_name_index_entry* right = rhs;

    return strcmp(left->Name, right->Name);
}

static size_t __directory_entry_count(
    struct VaFsDirectory* directory)
{
    if (directory->VaFs->Mode == VaFsMode_Read) {
        return ((struct VaFsDirectoryReader*)directory)->EntryCount;
    }

    return ((struct VaFsDirectoryWriter*)directory)->EntryCount;
}

static int __directory_uses_name_index(
    struct VaFsDirectory* directory)
{
    return directory->VaFs->Mode == VaFsMode_Read &&
        __directory_entry_count(directory) >= VAFS_DIRECTORY_HASH_INDEX_THRESHOLD;
}

static struct VaFsDirectoryEntry** __vafs_directory_index(
    struct VaFsDirectory* directory)
{
    struct VaFsDirectoryReader* reader;
    struct VaFsDirectoryWriter* writer;
    struct VaFsDirectoryEntry*  entry;
    struct VaFsDirectoryEntry**  index;
    size_t                      count = __directory_entry_count(directory);
    size_t                      i;

    if (count == 0) {
        return NULL;
    }

    if (directory->VaFs->Mode == VaFsMode_Read) {
        reader = (struct VaFsDirectoryReader*)directory;
        // Loaded directories cache a sorted pointer array so repeated lookups stay logarithmic.
        if (reader->Index != NULL && !reader->IndexDirty) {
            return reader->Index;
        }

        index = malloc(sizeof(struct VaFsDirectoryEntry*) * count);
        if (!index) {
            errno = ENOMEM;
            return NULL;
        }

        entry = reader->Entries;
        for (i = 0; i < count; i++) {
            index[i] = entry;
            entry = entry->Link;
        }

        qsort(index, count, sizeof(struct VaFsDirectoryEntry*), __compare_directory_entries);
        __free_directory_index(reader->Index);
        reader->Index = index;
        reader->IndexDirty = 0;
        return reader->Index;
    }

    writer = (struct VaFsDirectoryWriter*)directory;
    if (writer->Index != NULL && !writer->IndexDirty) {
        return writer->Index;
    }

    index = malloc(sizeof(struct VaFsDirectoryEntry*) * count);
    if (!index) {
        errno = ENOMEM;
        return NULL;
    }

    entry = writer->Entries;
    for (i = 0; i < count; i++) {
        index[i] = entry;
        entry = entry->Link;
    }

    qsort(index, count, sizeof(struct VaFsDirectoryEntry*), __compare_directory_entries);
    __free_directory_index(writer->Index);
    writer->Index = index;
    writer->IndexDirty = 0;
    return writer->Index;
}

static hashtable_t* __vafs_directory_name_index(
    struct VaFsDirectory* directory)
{
    struct VaFsDirectoryReader* reader = (struct VaFsDirectoryReader*)directory;
    struct VaFsDirectoryEntry*  entry;
    size_t                      count;
    int                         status;

    if (!__directory_uses_name_index(directory)) {
        return NULL;
    }

    if (reader->NameIndexInitialized) {
        return &reader->NameIndex;
    }

    count = __directory_entry_count(directory);
    status = vafs_hashtable_construct(
        &reader->NameIndex,
        count,
        sizeof(struct __directory_name_index_entry),
        __directory_name_hash,
        __directory_name_cmp
    );
    if (status != 0) {
        return NULL;
    }
    reader->NameIndexInitialized = 1;

    entry = reader->Entries;
    while (entry != NULL) {
        struct __directory_name_index_entry indexEntry = {
            .Name = __vafs_directory_entry_name(entry),
            .Entry = entry
        };

        errno = 0;
        if (vafs_hashtable_set(&reader->NameIndex, &indexEntry) == NULL && errno != 0) {
            __free_directory_name_index(reader);
            return NULL;
        }
        entry = entry->Link;
    }

    return &reader->NameIndex;
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

    if (__directory_lookup_cache_get(directory, token, &entry)) {
        return entry;
    }

    if (__vafs_directory_entries(directory) == NULL) {
        return NULL;
    }

    if (__directory_uses_name_index(directory)) {
        hashtable_t* nameIndex = __vafs_directory_name_index(directory);
        struct __directory_name_index_entry* indexedEntry;

        if (nameIndex == NULL) {
            return NULL;
        }

        indexedEntry = vafs_hashtable_get(
            nameIndex,
            &(struct __directory_name_index_entry) { .Name = token }
        );
        if (indexedEntry == NULL) {
            if (errno == ENOENT) {
                __directory_lookup_cache_store(directory, token, NULL);
            }
            return NULL;
        }
        __directory_lookup_cache_store(directory, token, indexedEntry->Entry);
        return indexedEntry->Entry;
    }

    struct VaFsDirectoryEntry** index;
    size_t                      count;
    size_t                      left;
    size_t                      right;

    // Smaller read-mode directories use the cached sorted view for binary search by entry name.
    index = __vafs_directory_index(directory);
    if (!index) {
        return NULL;
    }

    count = __directory_entry_count(directory);
    left = 0;
    right = count;
    while (left < right) {
        size_t middle = left + ((right - left) / 2);
        int comparison = strcmp(__vafs_directory_entry_name(index[middle]), token);

        if (comparison == 0) {
            __directory_lookup_cache_store(directory, token, index[middle]);
            return index[middle];
        }

        if (comparison < 0) {
            left = middle + 1;
        } else {
            right = middle;
        }
    }

    errno = ENOENT;
    __directory_lookup_cache_store(directory, token, NULL);
    return NULL;
}

int __vafs_directory_entry_stat(
    struct VaFsDirectoryEntry* entry,
    struct vafs_stat*          stat)
{
    if (entry == NULL || stat == NULL) {
        errno = EINVAL;
        return -1;
    }

    switch (entry->Type) {
        case VA_FS_DESCRIPTOR_TYPE_FILE:
            return __vafs_file_stat_internal(entry->File, stat);
        case VA_FS_DESCRIPTOR_TYPE_DIRECTORY:
            return __vafs_directory_stat_internal(entry->Directory, stat);
        case VA_FS_DESCRIPTOR_TYPE_SYMLINK:
            return __vafs_symlink_stat_internal(entry->Symlink, stat);
        default:
            errno = EINVAL;
            return -1;
    }
}

void vafs_directory_destroy(struct VaFsDirectory* directory)
{
    if (directory == NULL) {
        return;
    }

    // A directory instance can either be a reader or a writer. They
    // need different kinds of cleanup, but only one can be instanced
    // at the time. When reading images, we only use directory readers, and
    // when we write, we only use directory writers
    if (directory->VaFs->Mode == VaFsMode_Read) {
        __directory_reader_destroy((struct VaFsDirectoryReader*)directory);
    } else if (directory->VaFs->Mode == VaFsMode_Write) {
        __directory_writer_destroy((struct VaFsDirectoryWriter*)directory);
    }

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

    // Check for reasonable maximum length (prevent massive allocations)
    // Maximum descriptor size should be type size + reasonable name length
    // Use VAFS_NAME_MAX * 2 to allow for file name + symlink target
    if (descriptor->Length > (uint16_t)expectedSize + (VAFS_NAME_MAX * 2)) {
        VAFS_ERROR("__validate_descriptor_length: descriptor length %u exceeds maximum %d\n",
            descriptor->Length, expectedSize + (VAFS_NAME_MAX * 2));
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

    if (descriptor->TargetLength == 0) {
        VAFS_ERROR("__validate_symlink_descriptor: symlink has no target\n");
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

    status = vafs_stream_read(
        reader->Base.VaFs->DescriptorStream, 
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
        VAFS_ERROR("__read_descriptor: invalid descriptor size: %i for type %i\n", base->Length, base->Type);
        errno = EINVAL;
        return -1;
    }

    if (base->Length > sizeof(VaFsDescriptor_t)) {
        VAFS_DEBUG("__read_descriptor: read %u/%u descriptor bytes, reading rest\n", 
            sizeof(VaFsDescriptor_t), size);

        status = vafs_stream_read(
            reader->Base.VaFs->DescriptorStream, 
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

            // read rest of descriptor
            char* extendedBuffer = (char*)malloc(base->Length - size);
            if (!extendedBuffer) {
                VAFS_ERROR("__read_descriptor: failed to allocate extended buffer: %i\n", status);
                errno = ENOMEM;
                return -1;
            }

            status = vafs_stream_read(
                reader->Base.VaFs->DescriptorStream, 
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

    file = (struct VaFsFile*)malloc(sizeof(struct VaFsFile));
    if (!file) {
        errno = ENOMEM;
        return NULL;
    }

    memcpy(&file->Descriptor, descriptor, sizeof(VaFsFileDescriptor_t));
    file->Name = __read_extended_string(extendedData, descriptor->Base.Length - sizeof(VaFsFileDescriptor_t));
    file->VaFs = vafs;
    file->StatCached = 0;
    return file;
}

static struct VaFsDirectory* __create_directory_from_descriptor(
    struct VaFs*               vafs,
    VaFsDirectoryDescriptor_t* descriptor,
    const char*                extendedData)
{
    struct VaFsDirectoryReader* directory;

    // Validate the directory descriptor
    if (__validate_directory_descriptor(descriptor, extendedData) != 0) {
        errno = EINVAL;
        return NULL;
    }

    directory = (struct VaFsDirectoryReader*)malloc(sizeof(struct VaFsDirectoryReader));
    if (!directory) {
        errno = ENOMEM;
        return NULL;
    }

    directory->State     = VaFsDirectoryState_Open;
    directory->Entries   = NULL;
    directory->Index     = NULL;
    directory->EntryCount = 0;
    directory->IndexDirty = 1;
    directory->NameIndexInitialized = 0;
    memset(&directory->NameIndex, 0, sizeof(hashtable_t));
    directory->Base.Name = __read_extended_string(extendedData, descriptor->Base.Length - sizeof(VaFsDirectoryDescriptor_t));
    directory->Base.VaFs = vafs;
    directory->Base.StatCached = 0;
    memcpy(&directory->Base.Descriptor, descriptor, sizeof(VaFsDirectoryDescriptor_t));
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

    symlink = (struct VaFsSymlink*)malloc(sizeof(struct VaFsSymlink));
    if (!symlink) {
        errno = ENOMEM;
        return NULL;
    }

    memcpy(&symlink->Descriptor, descriptor, sizeof(VaFsSymlinkDescriptor_t));
    symlink->Name   = __read_extended_string(extendedData, descriptor->NameLength);
    symlink->Target = __read_extended_string(extendedData + descriptor->NameLength, descriptor->TargetLength);
    symlink->VaFs   = vafs;
    symlink->StatCached = 0;
    return symlink;
}

static struct VaFsDirectoryEntry* __create_entry_from_descriptor(
    struct VaFs*      vafs,
    VaFsDescriptor_t* descriptor,
    const char*       extendedData)
{
    struct VaFsDirectoryEntry* entry;
    
    entry = (struct VaFsDirectoryEntry*)malloc(sizeof(struct VaFsDirectoryEntry));
    if (!entry) {
        errno = ENOMEM;
        return NULL;
    }
    memset(entry, 0, sizeof(struct VaFsDirectoryEntry));

    entry->Type = descriptor->Type;
    if (entry->Type == VA_FS_DESCRIPTOR_TYPE_FILE) {
        entry->File = __create_file_from_descriptor(vafs, (VaFsFileDescriptor_t*)descriptor, extendedData);
    } else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
        entry->Directory = __create_directory_from_descriptor(vafs, (VaFsDirectoryDescriptor_t*)descriptor, extendedData);
    } else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_SYMLINK) {
        entry->Symlink = __create_symlink_from_descriptor(vafs, (VaFsSymlinkDescriptor_t*)descriptor, extendedData);
    } else {
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

    // if the directory has no entries, we can skip loading it
    if (reader->Base.Descriptor.Descriptor.Index == VA_FS_INVALID_BLOCK) {
        reader->State = VaFsDirectoryState_Loaded;
        return 0;
    }

    status = vafs_stream_lock(reader->Base.VaFs->DescriptorStream);
    if (status) {
        VAFS_ERROR("__load_directory: failed to get lock on stream\n");
        return status;
    }

    // we lock the descriptor stream while reading the directory
    // as only one can access the underlying media at the time due
    // the c file interface.
    status = vafs_stream_seek(
        reader->Base.VaFs->DescriptorStream,
        reader->Base.Descriptor.Descriptor.Index,
        reader->Base.Descriptor.Descriptor.Offset
    );
    if (status) {
        VAFS_ERROR("__load_directory: failed to seek to directory data\n");
        vafs_stream_unlock(reader->Base.VaFs->DescriptorStream);
        return status;
    }

    // read the directory descriptor
    status = vafs_stream_read(
        reader->Base.VaFs->DescriptorStream,
        &header, sizeof(VaFsDirectoryHeader_t),
        &read
    );
    if (status) {
        VAFS_ERROR("__load_directory: failed to read directory header\n");
        vafs_stream_unlock(reader->Base.VaFs->DescriptorStream);
        return status;
    }

    // Validate directory entry count is reasonable
    // A directory with more than 1 million entries is suspicious and likely malformed
    #define MAX_DIRECTORY_ENTRIES 1000000
    if (header.Count > MAX_DIRECTORY_ENTRIES) {
        VAFS_ERROR("__load_directory: directory entry count %u exceeds maximum %d\n",
            header.Count, MAX_DIRECTORY_ENTRIES);
        vafs_stream_unlock(reader->Base.VaFs->DescriptorStream);
        errno = EINVAL;
        return -1;
    }

    // read the directory entries
    VAFS_INFO("__load_directory: reading %u entries\n", header.Count);
    for (uint32_t i = 0; i < header.Count; i++) {
        struct VaFsDirectoryEntry* entry;
        char                       buffer[64];
        char*                      extendedData = NULL;
        VAFS_INFO("__load_directory: reading entry %i/%u\n", i, header.Count);
        
        status = __read_descriptor(reader, &buffer[0], &extendedData);
        if (status) {
            VAFS_ERROR("__load_directory: failed to read descriptor\n");
            vafs_stream_unlock(reader->Base.VaFs->DescriptorStream);
            return status;
        }

        // create a new entry
        entry = __create_entry_from_descriptor(reader->Base.VaFs, (VaFsDescriptor_t*)&buffer[0], extendedData);
        free(extendedData);

        if (!entry) {
            VAFS_ERROR("__load_directory: failed to create entry\n");
            vafs_stream_unlock(reader->Base.VaFs->DescriptorStream);
            return -1;
        }

        // add the entry to the directory
        entry->Link = reader->Entries;
        reader->Entries = entry;
        reader->EntryCount++;
    }

    // unlock the descriptor stream
    vafs_stream_unlock(reader->Base.VaFs->DescriptorStream);

    reader->State = VaFsDirectoryState_Loaded;
    reader->IndexDirty = 1;
    if (__directory_uses_name_index(&reader->Base)) {
        if (__vafs_directory_name_index(&reader->Base) == NULL && reader->EntryCount != 0) {
            return -1;
        }
    } else if (__vafs_directory_index(&reader->Base) == NULL && reader->EntryCount != 0) {
        return -1;
    }
    return 0;
}

int vafs_directory_open_root(
    struct VaFs*           vafs,
    VaFsBlockPosition_t*   position,
    struct VaFsDirectory** directoryOut)
{
    struct VaFsDirectoryReader* reader;
    
    if (vafs == NULL || position == NULL || directoryOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    VAFS_DEBUG("vafs_directory_open_root(pos=%u/%u)\n", position->Index, position->Offset);
    
    reader = (struct VaFsDirectoryReader*)malloc(sizeof(struct VaFsDirectoryReader));
    if (!reader) {
        VAFS_ERROR("vafs_directory_open_root: failed to allocate directory reader\n");
        errno = ENOMEM;
        return -1;
    }

    reader->Base.VaFs = vafs;
    reader->Base.Name = strdup("root");
    reader->Base.StatCached = 0;
    reader->State     = VaFsDirectoryState_Open;
    reader->Entries   = NULL;
    reader->Index     = NULL;
    reader->EntryCount = 0;
    reader->IndexDirty = 1;
    reader->NameIndexInitialized = 0;
    memset(&reader->NameIndex, 0, sizeof(hashtable_t));
    
    // initialize the root descriptor for the directory
    reader->Base.Descriptor.Base.Length = sizeof(VaFsDirectoryDescriptor_t);
    reader->Base.Descriptor.Base.Type   = VA_FS_DESCRIPTOR_TYPE_DIRECTORY;
    reader->Base.Descriptor.Descriptor.Index = position->Index;
    reader->Base.Descriptor.Descriptor.Offset = position->Offset;
    reader->Base.Descriptor.Permissions = 0775;

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
    }
    else {
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
            errno = ENOENT;
            return -1;
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

    // increase descriptor length by name, do not account
    // for the null terminator
    entry->File->Descriptor.Base.Length += (uint16_t)strlen(entry->File->Name);

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
    
    // increase descriptor length by name, do not account
    // for the null terminator
    entry->Directory->Descriptor.Base.Length += (uint16_t)strlen(entry->Directory->Name);

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

    entry->Symlink->Descriptor.NameLength   = (uint16_t)strlen(entry->Symlink->Name);
    entry->Symlink->Descriptor.TargetLength = (uint16_t)strlen(entry->Symlink->Target);

    // increase descriptor length by names, do not account
    // for the null terminator
    entry->Symlink->Descriptor.Base.Length += entry->Symlink->Descriptor.NameLength;
    entry->Symlink->Descriptor.Base.Length += entry->Symlink->Descriptor.TargetLength;

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

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        entry->Symlink->Target,
        strlen(entry->Symlink->Target)
    );
    return status;
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

    // We must flush all subdirectories first to initialize their
    // index and offset. Otherwise, we will be writing empty descriptors
    // for subdirectories.
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

    directory->Descriptor.Descriptor.Index  = block;
    directory->Descriptor.Descriptor.Offset = offset;
    VAFS_DEBUG("vafs_directory_flush  name=%s index=%d offset=%d\n", directory->Name, block, offset);

    status = __write_directory_header(writer, entryCount);
    if (status) {
        VAFS_ERROR("vafs_directory_flush: failed to write directory header\n");
        return status;
    }

    // now we actually write all the descriptors
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

uint32_t vafs_directory_permissions(
    struct VaFsDirectoryHandle* handle)
{
    if (handle == NULL) {
        errno = EINVAL;
        return (uint32_t)-1;
    }

    return handle->Directory->Descriptor.Permissions;
}

int vafs_directory_read(
    struct VaFsDirectoryHandle* handle,
    struct VaFsEntry*           entryOut)
{
    struct VaFsDirectoryEntry* entry;
    size_t                     count;
    size_t                     i;
    VAFS_INFO("vafs_directory_read(handle=%p)\n", handle);

    if (handle == NULL || entryOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    __vafs_directory_entries(handle->Directory);

    count = __directory_entry_count(handle->Directory);
    VAFS_DEBUG("vafs_directory_read: locate index %i\n", handle->Index);
    if (count == 0 || (size_t)handle->Index >= count) {
        VAFS_INFO("vafs_directory_read: end of directory\n");
        errno = ENOENT;
        return -1;
    }

    // Directory iteration preserves the stored list order for compatibility.
    entry = __vafs_directory_entries(handle->Directory);
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

    // initialize the entry structure
    entryOut->Name = __vafs_directory_entry_name(entry);
    entryOut->Type = (enum VaFsEntryType)entry->Type;
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

    newEntry = (struct VaFsDirectoryEntry*)malloc(sizeof(struct VaFsDirectoryEntry));
    if (!newEntry) {
        errno = ENOMEM;
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
    uint32_t                    permissions)
{
    struct VaFsFile* entry;
    int              status;

    entry = (struct VaFsFile*)malloc(sizeof(struct VaFsFile));
    if (!entry) {
        errno = ENOMEM;
        return -1;
    }

    entry->VaFs = writer->Base.VaFs;
    entry->Name = strdup(name);
    entry->StatCached = 0;
    if (!entry->Name) {
        free(entry);
        errno = ENOMEM;
        return -1;
    }

    __initialize_file_descriptor(&entry->Descriptor, permissions);
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

    newEntry = (struct VaFsDirectoryEntry*)malloc(sizeof(struct VaFsDirectoryEntry));
    if (!newEntry) {
        errno = ENOMEM;
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
    const char*                 target)
{
    struct VaFsSymlink* entry;
    int                 status;

    entry = (struct VaFsSymlink*)malloc(sizeof(struct VaFsSymlink));
    if (!entry) {
        errno = ENOMEM;
        return -1;
    }

    entry->VaFs = writer->Base.VaFs;
    entry->Name = strdup(name);
    entry->StatCached = 0;
    if (!entry->Name) {
        free(entry);
        errno = ENOMEM;
        return -1;
    }

    entry->Target = strdup(target);
    if (!entry->Target) {
        free((void*)entry->Name);
        free(entry);
        errno = ENOMEM;
        return -1;
    }

    __initialize_symlink_descriptor(&entry->Descriptor);
    entry->StatCached = 0;
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

static int __add_directory_entry(
    struct VaFsDirectoryWriter* writer,
    struct VaFsDirectory*       entry)
{
    struct VaFsDirectoryEntry* newEntry;

    newEntry = (struct VaFsDirectoryEntry*)malloc(sizeof(struct VaFsDirectoryEntry));
    if (!newEntry) {
        errno = ENOMEM;
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
    uint32_t                    permissions)
{
    struct VaFsDirectoryWriter* entry;
    int                         status;
    VAFS_DEBUG("__create_directory_entry(name=%s)\n", name);

    entry = (struct VaFsDirectoryWriter*)malloc(sizeof(struct VaFsDirectoryWriter));
    if (!entry) {
        errno = ENOMEM;
        return -1;
    }

    entry->Entries = NULL;
    entry->Index = NULL;
    entry->EntryCount = 0;
    entry->IndexDirty = 1;
    entry->Base.VaFs = writer->Base.VaFs;
    entry->Base.Name = strdup(name);
    entry->Base.StatCached = 0;
    if (!entry->Base.Name) {
        free(entry);
        errno = ENOMEM;
        return -1;
    }

    __initialize_directory_descriptor(&entry->Base.Descriptor, permissions);
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
        errno = ENOENT;
        return -1;
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
    uint32_t                     permissions,
    struct VaFsDirectoryHandle** handleOut)
{

    struct VaFsDirectoryWriter* writer;
    int                         status;
    struct VaFsDirectoryEntry*  entry;
    char                        token[VAFS_NAME_MAX + 1];
    VAFS_DEBUG("vafs_directory_create_directory(handle=%p, name=%s, handleOut=%p)\n", handle, name, handleOut);

    if (handle == NULL || name == NULL || handleOut == NULL) {
        errno = EINVAL;
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
    entry = __vafs_directory_find_entry(handle->Directory, token);
    if (entry != NULL) {
        *handleOut = __create_handle(entry->Directory);
        return 0;
    }

    writer = (struct VaFsDirectoryWriter*)handle->Directory;
    status = __create_directory_entry(writer, token, permissions);
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
        errno = ENOENT;
        return -1;
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
    uint32_t                    permissions,
    struct VaFsFileHandle**     handleOut)
{
    struct VaFsDirectoryWriter* writer;
    int                         status;
    struct VaFsDirectoryEntry*  entry;
    char                        token[VAFS_NAME_MAX + 1];
    VAFS_DEBUG("vafs_directory_create_file(name=%s)\n", name);

    if (handle == NULL || name == NULL || handleOut == NULL) {
        errno = EINVAL;
        return -1;
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
    status = __create_file_entry(writer, token, permissions);
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
    const char*                 target)
{
    struct VaFsDirectoryEntry* entry;
    char                       token[VAFS_NAME_MAX + 1];
    VAFS_DEBUG("vafs_directory_create_symlink(name=%s, target=%s)\n", name, target);

    if (handle == NULL || name == NULL || target == NULL) {
        errno = EINVAL;
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
        return __create_symlink_entry(writer, token, target);
    }

    errno = EEXIST;
    return -1;
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
        errno = ENOENT;
        return -1;
    }
    
    if (entry->Type != VA_FS_DESCRIPTOR_TYPE_SYMLINK) {
        errno = EINVAL;
        return -1;
    }

    *targetOut = entry->Symlink->Target;
    return 0;
}
