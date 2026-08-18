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
#include <stdlib.h>
#include <string.h>

#include "../core/core.h"
#include "directory.h"
#include "object.h"
#include "path.h"


struct __directory_name_index_entry {
    const char*                Name;
    struct VaFsDirectoryEntry* Entry;
};

static void __free_directory_index(struct VaFsDirectoryEntry** index)
{
    if (index == NULL) {
        return;
    }
    free(index);
}

static void __free_directory_name_index(struct VaFsDirectoryReader* reader)
{
    if (!reader->NameIndexInitialized) {
        return;
    }

    // Large read-mode directories build this hash table lazily, so only tear
    // it down when initialization actually completed.
    vafs_hashtable_destroy(&reader->NameIndex);
    memset(&reader->NameIndex, 0, sizeof(hashtable_t));
    reader->NameIndexInitialized = 0;
}

void __directory_reader_index_delete(struct VaFsDirectoryReader* reader)
{
    if (reader == NULL) {
        return;
    }

    __free_directory_index(reader->Index);
    __free_directory_name_index(reader);
}

void __directory_writer_index_delete(struct VaFsDirectoryWriter* writer)
{
    if (writer == NULL) {
        return;
    }

    __free_directory_index(writer->Index);
}

static uint64_t __directory_lookup_cache_hash(
    const struct VaFsDirectory* directory,
    const char*                 token)
{
    const unsigned char* name = (const unsigned char*)token;
    uintptr_t            parent = (uintptr_t)directory;
    uint64_t             hash = 1469598103934665603ULL;

    // Mix the parent pointer into the hash so identical child names in sibling
    // directories land in different lookup-cache slots.

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
    // The lookup cache is set-associative and bounded, so mask the full hash
    // down to one set and let replacement happen only inside that set.
    return (size_t)(__directory_lookup_cache_hash(directory, token) & (VAFS_LOOKUP_CACHE_SET_COUNT - 1));
}

static uint64_t __directory_lookup_cache_generation(
    struct VaFsLookupCache* cache)
{
    cache->Generation++;
    if (cache->Generation == 0) {
        // Generation 0 is reserved as the never-used sentinel, so wrap to 1
        // instead of letting hot entries look empty after overflow.
        cache->Generation = 1;
    }
    return cache->Generation;
}

static struct VaFsLookupCacheEntry* __directory_lookup_cache_entries(
    struct VaFs* vafs,
    size_t       setIndex)
{
    return &vafs->LookupCache->Entries[setIndex * VAFS_LOOKUP_CACHE_SET_ASSOCIATIVITY];
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
        // Writer mode mutates the namespace, so cached read answers would go
        // stale immediately and are not worth maintaining.
        return 0;
    }

    cache = directory->VaFs->LookupCache;
    setIndex = __vafs_directory_lookup_cache_set(directory, token);
    entries = __directory_lookup_cache_entries(directory->VaFs, setIndex);

    // Probe only the selected set and treat both hits and misses as cacheable
    // answers so repeated failed lookups do not rewalk the directory.
    for (size_t i = 0; i < VAFS_LOOKUP_CACHE_SET_ASSOCIATIVITY; i++) {
        if (entries[i].State == VaFsLookupCacheState_Empty) {
            continue;
        }

        if (entries[i].Parent != directory || strcmp(entries[i].Name, token) != 0) {
            continue;
        }

        entries[i].Generation = __directory_lookup_cache_generation(cache);
        if (entries[i].State == VaFsLookupCacheState_Miss) {
            // Negative caching is intentional here: a repeated miss should be
            // just as cheap as a repeated hit.
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
        // Only immutable read-mode directories participate in the bounded
        // lookup cache; write mode goes straight to live structures.
        return;
    }

    cache = directory->VaFs->LookupCache;
    setIndex = __vafs_directory_lookup_cache_set(directory, token);
    entries = __directory_lookup_cache_entries(directory->VaFs, setIndex);
    target = &entries[0];

    // Replacement prefers an empty slot first, then an exact-key overwrite,
    // and finally the least-recently-used entry in the set.

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
    // Readers and writers keep separate concrete types, but both expose the
    // same logical question: how many children are currently present?
    if (directory->VaFs->Mode == VaFsMode_Read) {
        return ((struct VaFsDirectoryReader*)directory)->EntryCount;
    }
    return ((struct VaFsDirectoryWriter*)directory)->EntryCount;
}

static int __directory_uses_name_index(
    struct VaFsDirectory* directory)
{
    // The hash index is reserved for large immutable directories where the
    // extra memory beats repeated binary searches over many entries.
    return directory->VaFs->Mode == VaFsMode_Read &&
        __directory_entry_count(directory) >= VAFS_DIRECTORY_HASH_INDEX_THRESHOLD;
}

static struct VaFsDirectoryEntry** __vafs_directory_index(
    struct VaFsDirectory* directory)
{
    struct VaFsDirectoryReader* reader;
    struct VaFsDirectoryWriter* writer;
    struct VaFsDirectoryEntry*  entry;
    struct VaFsDirectoryEntry** index;
    size_t                      count = __directory_entry_count(directory);
    size_t                      i;

    if (count == 0) {
        // Empty directories do not need an index structure at all.
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

        // Snapshot the linked-list entries into a dense array so repeated name
        // lookups can stay logarithmic instead of rescanning the list.
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

    // Writer mode keeps the linked list as the source of truth, but still
    // benefits from a rebuilt sorted array when lookups are repeated.
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
        // Small directories intentionally stay on the lighter-weight sorted
        // vector path instead of paying for a second hash table.
        return NULL;
    }

    if (reader->NameIndexInitialized) {
        return &reader->NameIndex;
    }

    // Build the large-directory name index lazily so unopened directories do
    // not pay the allocation cost up front.

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
            // Tear the whole index back down if any insert fails so later
            // lookups never observe a partially populated structure.
            __free_directory_name_index(reader);
            return NULL;
        }
        entry = entry->Link;
    }

    return &reader->NameIndex;
}

int __vafs_directory_index_build(
    struct VaFsDirectoryReader* reader)
{
    if (__directory_uses_name_index(&reader->Base)) {
        // Very large directories pay the extra memory for a direct name index.
        if (__vafs_directory_name_index(&reader->Base) == NULL && reader->EntryCount != 0) {
            return -1;
        }
    } else if (__vafs_directory_index(&reader->Base) == NULL && reader->EntryCount != 0) {
        // Smaller directories keep only the sorted vector used for binary
        // search and iteration order reconstruction.
        return -1;
    }
    return 0;
}

static struct VaFsDirectoryEntry* __vafs_directory_name_index_lookup(
    struct VaFsDirectory* directory,
    const char*           token)
{
    hashtable_t*                         nameIndex;
    struct __directory_name_index_entry* indexedEntry;

    if (directory == NULL || token == NULL) {
        errno = EINVAL;
        return NULL;
    }

    // Large-directory lookups go straight through the hash index, then feed the
    // bounded `(parent, name)` cache with either the hit or the negative result.

    nameIndex = __vafs_directory_name_index(directory);
    if (nameIndex == NULL) {
        return NULL;
    }

    indexedEntry = vafs_hashtable_get(
        nameIndex,
        &(struct __directory_name_index_entry) { .Name = token }
    );
    if (indexedEntry == NULL) {
        if (errno == ENOENT) {
            // Persist misses into the bounded lookup cache so repeated probes
            // do not keep hitting the large-directory hash table either.
            __directory_lookup_cache_store(directory, token, NULL);
        }
        return NULL;
    }
    __directory_lookup_cache_store(directory, token, indexedEntry->Entry);
    return indexedEntry->Entry;
}


struct VaFsDirectoryEntry* __vafs_directory_lookup_cache_resolve(
    struct VaFsDirectory* directory,
    const char*           token)
{
    struct VaFsDirectoryEntry** index;
    size_t                      count;
    size_t                      left;
    size_t                      right;

    // Smaller read-mode directories use the cached sorted view for binary search by entry name.
    index = __vafs_directory_index(directory);
    if (!index) {
        return NULL;
    }

    // Binary search keeps small-directory lookups cheap without the memory cost
    // of building the large-directory hash index.
    count = __directory_entry_count(directory);
    left = 0;
    right = count;
    while (left < right) {
        size_t middle = left + ((right - left) / 2);
        int comparison = strcmp(__vafs_directory_entry_name(index[middle]), token);

        if (comparison == 0) {
            // Feed successful binary-search results back into the bounded cache
            // so the next lookup can avoid the search altogether.
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

struct VaFsDirectoryEntry* __vafs_directory_get(
    struct VaFsDirectory* directory,
    const char*           token)
{
    struct VaFsDirectoryEntry* entry;

    if (directory == NULL || token == NULL) {
        errno = EINVAL;
        return NULL;
    }

    // Resolve in three tiers: the tiny bounded lookup cache first, then the
    // large-directory hash index when available, otherwise the sorted-array
    // binary search for smaller immutable directories.
    if (__directory_lookup_cache_get(directory, token, &entry)) {
        return entry;
    }

    if (__directory_uses_name_index(directory)) {
        return __vafs_directory_name_index_lookup(directory, token);
    }
    return __vafs_directory_lookup_cache_resolve(directory, token);
}
