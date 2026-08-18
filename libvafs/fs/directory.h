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

#ifndef __VAFS_FS_DIRECTORY_H_
#define __VAFS_FS_DIRECTORY_H_

#include <stdint.h>
#include <stddef.h>

#include <vafs/stat.h>

#include "../cache/hashtable.h"
#include "../format/format.h"

// If a directory has more entries than this threshold, we will build a 
// hash index for it to speed up lookups. This is a tradeoff between 
// memory usage and lookup performance.
#define VAFS_DIRECTORY_HASH_INDEX_THRESHOLD 512

// Read-only lookup cache for repeated path traversal. The cache is keyed by
// (parent directory pointer, child name), stores both hits and misses, and is
// bounded so mounted images have a predictable memory footprint.
#define VAFS_LOOKUP_CACHE_SET_COUNT          128
#define VAFS_LOOKUP_CACHE_SET_ASSOCIATIVITY  4
#define VAFS_LOOKUP_CACHE_CAPACITY           (VAFS_LOOKUP_CACHE_SET_COUNT * VAFS_LOOKUP_CACHE_SET_ASSOCIATIVITY)

// A directory with more than 1 million entries is suspicious and likely malformed
#define VAFS_MAX_DIRECTORY_ENTRIES 1000000

// Read-mode directories start in a lightweight open state and only transition
// to loaded once their child descriptors have been materialized.
enum VaFsDirectoryState {
    VaFsDirectoryState_Open,
    VaFsDirectoryState_Loaded
};

enum VaFsLookupCacheState {
    VaFsLookupCacheState_Empty = 0,
    VaFsLookupCacheState_Hit,
    VaFsLookupCacheState_Miss
};

// Forward declarations
struct VaFsDirectoryEntry;

struct VaFsDirectory {
    struct VaFs*              VaFs;
    VaFsDirectoryDescriptor_t Descriptor;
    // Only the root directory needs its own descriptor position persisted back
    // to the image header because every other directory descriptor is anchored
    // by its parent entry.
    VaFsBlockPosition_t       DescriptorPosition;
    const char*               Name;
    struct VaFsMetadata       Stat;
    struct VaFsXattrSet*      Xattrs;
    int                       StatCached;
    int                       XattrsLoaded;
};

struct VaFsDirectoryReader {
    struct VaFsDirectory       Base;
    enum VaFsDirectoryState    State;
    struct VaFsDirectoryEntry* Entries;
    struct VaFsStreamReader*   Reader;
    // Small read-mode directories use this sorted view for binary search by name.
    struct VaFsDirectoryEntry** Index;
    // Very large directories build a secondary name index to avoid binary-search overhead.
    hashtable_t                NameIndex;
    size_t                     EntryCount;
    int                        IndexDirty;
    int                        NameIndexInitialized;
};

struct VaFsDirectoryWriter {
    struct VaFsDirectory       Base;
    struct VaFsDirectoryEntry* Entries;
    // Writer mode keeps the linked list as the source of truth and rebuilds this cache on demand.
    struct VaFsDirectoryEntry** Index;
    size_t                     EntryCount;
    int                        IndexDirty;
};

struct VaFsDirectoryBuilder {
    struct VaFsDirectory* Directory;
    int                   Index;
};

// Directory entries are the shared tagged-union node type used by lookup,
// serialization, and metadata code across both read and write modes.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4201)
#endif
struct VaFsDirectoryEntry {
    int Type;
    union {
        struct VaFsFile*      File;
        struct VaFsDirectory* Directory;
        struct VaFsSymlink*   Symlink;
        struct VaFsSpecial*   Special;
        struct VaFsHardlink*  Hardlink;
    };
    struct VaFsDirectoryEntry* Link;
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

struct VaFsLookupCacheEntry {
    struct VaFsDirectory*      Parent;
    struct VaFsDirectoryEntry* Entry;
    uint64_t                   Generation;
    enum VaFsLookupCacheState  State;
    char                       Name[VAFS_NAME_MAX + 1];
};

struct VaFsLookupCache {
    uint64_t                    Generation;
    struct VaFsLookupCacheEntry Entries[VAFS_LOOKUP_CACHE_CAPACITY];
};

/**
 * @brief Returns the linked-list entries for a directory, loading them on demand in read mode.
 *
 * @param[In] directory Directory whose entries should be available.
 * @return Pointer to the head of the entry list, or `NULL` on failure.
 */
extern struct VaFsDirectoryEntry* __vafs_directory_entries(struct VaFsDirectory* directory);

/**
 * @brief Resolves a child entry by name inside a directory.
 *
 * Read mode uses the cached binary-search/hash-index path and the bounded
 * `(parent, name)` lookup cache. Write mode falls back to the mutable linked list.
 *
 * @param[In] directory Directory to search.
 * @param[In] token     Child entry name to resolve.
 * @return Matching entry on success, otherwise `NULL` with `errno` set.
 */
extern struct VaFsDirectoryEntry* __vafs_directory_find_entry(struct VaFsDirectory* directory, const char* token);

/**
 * @brief Resolves a child entry from the immutable read-mode directory cache paths.
 *
 * @param[In] directory Directory to search.
 * @param[In] token Child entry name to resolve.
 * @return Matching entry on success, otherwise `NULL` with `errno` set.
 */
extern struct VaFsDirectoryEntry* __vafs_directory_get(struct VaFsDirectory* directory, const char* token);

/**
 * @brief Resolves a hardlink placeholder entry to its canonical backing object.
 *
 * Internal metadata and xattr code calls this first so aliases cannot diverge
 * from the payload-bearing entry that owns the shared object state.
 *
 * @param[In] vafs  Filesystem instance that owns the entry.
 * @param[In] entry Directory entry that may represent a hardlink alias.
 * @return Canonical backing entry on success, otherwise `NULL` with `errno` set.
 */
extern struct VaFsDirectoryEntry* __vafs_resolve_hardlink(struct VaFs* vafs, struct VaFsDirectoryEntry* entry);

/**
 * @brief Computes the lookup-cache set index for a `(parent, name)` pair.
 *
 * This is primarily exposed for internal tests that need deterministic eviction coverage.
 *
 * @param[In] directory Parent directory used as part of the cache key.
 * @param[In] token     Child entry name used as part of the cache key.
 * @return Cache set index for the supplied lookup key.
 */
extern size_t __vafs_directory_lookup_cache_set(const struct VaFsDirectory* directory, const char* token);

/**
 * @brief Materializes metadata for a directory entry.
 *
 * Read mode may satisfy this from cached metadata stored on the entry object.
 *
 * @param[In]  entry Directory entry to describe.
 * @param[Out] metadata Receives the entry metadata on success.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int __vafs_directory_entry_stat(struct VaFsDirectoryEntry* entry, struct VaFsMetadata* metadata);

/**
 * @brief Returns the stable name string associated with a directory entry.
 *
 * @param[In] entry Directory entry to inspect.
 * @return Entry name string, or `NULL` if the entry type is invalid.
 */
extern const char* __vafs_directory_entry_name(struct VaFsDirectoryEntry* entry);

/**
 * @brief Builds whichever read-mode lookup accelerator matches the directory size.
 *
 * @param[In] reader Read-mode directory whose indexes should be materialized.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int __vafs_directory_index_build(struct VaFsDirectoryReader* reader);

/**
 * @brief Releases the sorted and hashed lookup accelerators owned by a read-mode directory.
 *
 * @param[In] reader Directory reader whose cached indexes should be dropped.
 */
extern void __directory_reader_index_delete(struct VaFsDirectoryReader* reader);

/**
 * @brief Releases the name index cache owned by a write-mode directory.
 *
 * @param[In] writer Directory writer whose cached indexes should be dropped.
 */
extern void __directory_writer_index_delete(struct VaFsDirectoryWriter* writer);

/**
 * @brief Initializes a directory descriptor from already-normalized metadata.
 *
 * @param[Out] descriptor Directory descriptor to initialize.
 * @param[In]  metadata   Metadata snapshot to copy into the descriptor.
 */
extern void __initialize_directory_descriptor(
    VaFsDirectoryDescriptor_t* descriptor,
    const struct VaFsMetadata* metadata);

/**
 * @brief Creates the mutable root directory used while building a new image.
 *
 * @param[In]  vafs         Filesystem instance being created.
 * @param[Out] directoryOut Receives the created root directory.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_directory_create_root(
    struct VaFs*           vafs,
    struct VaFsDirectory** directoryOut);

/**
 * @brief Destroys a directory object and all loaded child entry state.
 *
 * @param[In] directory Directory instance to destroy.
 */
extern void vafs_directory_destroy(
    struct VaFsDirectory* directory);

/**
 * @brief Recursively serializes a writable directory tree into the descriptor stream.
 *
 * @param[In] directory Root of the writable directory subtree to flush.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_directory_flush(
    struct VaFsDirectory* directory);

/**
 * @brief Writes the standalone root directory descriptor for a writable image.
 *
 * @param[In] directory Writable root directory whose child payload is already flushed.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_directory_write_root_descriptor(
    struct VaFsDirectory* directory);

/**
 * @brief Opens the root directory of an existing image for read access.
 *
 * @param[In]  vafs         Filesystem instance that owns the root directory.
 * @param[In]  position     On-disk descriptor position of the root directory.
 * @param[Out] directoryOut Receives the opened root directory.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_directory_open_root(
    struct VaFs*           vafs,
    VaFsBlockPosition_t*   position,
    struct VaFsDirectory** directoryOut);

#endif //!__VAFS_FS_DIRECTORY_H_
