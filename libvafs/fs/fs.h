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

#ifndef __VAFS_FS_PRIVATE_H_
#define __VAFS_FS_PRIVATE_H_

#include <stdint.h>
#include <stddef.h>

#include <vafs/types.h>
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

// Symlink resolution limits
// Maximum number of symlinks that can be traversed in a single path resolution
// This prevents infinite loops from cyclic symlinks and limits resource consumption
#define VAFS_SYMLINK_MAX_DEPTH 40

// A directory with more than 1 million entries is suspicious and likely malformed
#define VAFS_MAX_DIRECTORY_ENTRIES 1000000

// Utility functions
struct VaFsDirectoryEntry;

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

enum VaFsLookupCacheState {
    VaFsLookupCacheState_Empty = 0,
    VaFsLookupCacheState_Hit,
    VaFsLookupCacheState_Miss
};

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
 * @brief Checks whether a path refers to the filesystem root.
 *
 * Empty strings and `/` are treated as the root path.
 *
 * @param[In] path Path string to inspect.
 * @return 1 when the path resolves to root, otherwise 0.
 */
extern int __vafs_is_root_path(const char* path);

/**
 * @brief Extracts the next path component from a path string.
 *
 * Leading separators are skipped. The extracted component is copied into
 * `token`, and the return value is the number of characters consumed from
 * the original path string.
 *
 * @param[In]  path      Path string to tokenize.
 * @param[Out] token     Buffer that receives the next component.
 * @param[In]  tokenSize Size of the `token` buffer in bytes.
 * @return Number of characters consumed, or 0 when no token could be produced.
 */
extern int __vafs_pathtoken(const char* path, char* token, size_t tokenSize);

/**
 * @brief Resolves a symlink target relative to a base path prefix.
 *
 * The helper normalizes repeated separators and handles `.` and `..`
 * components while building the resolved path.
 *
 * @param[Out] buffer        Destination buffer for the resolved path.
 * @param[In]  bufferLength  Size of `buffer` in bytes.
 * @param[In]  baseStart     Start of the original path used as the base prefix.
 * @param[In]  baseLength    Number of bytes from `baseStart` to keep as the base prefix.
 * @param[In]  symlinkTarget Symlink target to append and normalize.
 * @return Number of characters written on success, otherwise -1 with `errno` set.
 */
extern int __vafs_resolve_symlink(char* buffer, size_t bufferLength, const char* baseStart, size_t baseLength, const char* symlinkTarget);

/**
 * @brief Internal path-stat implementation with explicit symlink depth tracking.
 *
 * This resolves the path component-by-component, optionally follows symlinks,
 * and fills the metadata structure for the final entry.
 *
 * @param[In]  vafs         Filesystem instance to resolve within.
 * @param[In]  path         Absolute or root-relative path to stat.
 * @param[In]  followLinks  Non-zero to follow symlinks, zero to report the symlink itself.
 * @param[Out] metadata     Receives the resolved metadata on success.
 * @param[In]  symlinkDepth Current recursive symlink depth.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int __vafs_path_stat_internal(struct VaFs* vafs, const char* path, int followLinks, struct VaFsMetadata* metadata, int symlinkDepth);

/**
 * @brief Internal file-open implementation with explicit symlink depth tracking.
 *
 * @param[In]  vafs         Filesystem instance to resolve within.
 * @param[In]  path         Path to the file that should be opened.
 * @param[Out] handleOut    Receives the opened file handle on success.
 * @param[In]  symlinkDepth Current recursive symlink depth.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int __vafs_file_open_internal(struct VaFs* vafs, const char* path, struct VaFsFileHandle** handleOut, int symlinkDepth);

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
 * @brief Resolves a child entry from the immutable read-mode directory cache paths.
 *
 * @param[In] directory Directory to search.
 * @param[In] token Child entry name to resolve.
 * @return Matching entry on success, otherwise `NULL` with `errno` set.
 */
extern struct VaFsDirectoryEntry* __vafs_directory_get(struct VaFsDirectory* directory, const char* token);

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
 * @brief Completes the canonical metadata fields required for one entry.
 *
 * Descriptor writers and create paths both run through this helper so entry
 * type, size, and fallback link-count behavior stay identical everywhere.
 *
 * @param[In,Out] metadata Metadata structure to normalize.
 * @param[In]     type     Final entry type.
 * @param[In]     size     Final logical size for the entry.
 */
extern void __finalize_entry_metadata(
    struct VaFsMetadata* metadata,
    enum VaFsEntryType   type,
    uint64_t             size);

/**
 * @brief Returns non-zero when the supplied entry type is one of the supported special-node kinds.
 *
 * @param[In] type Entry type to inspect.
 * @return Non-zero for supported special entry types, otherwise zero.
 */
extern int __is_special_entry_type(
    enum VaFsEntryType type);

/**
 * @brief Initializes a file descriptor from already-normalized metadata.
 *
 * @param[Out] descriptor File descriptor to initialize.
 * @param[In]  metadata   Metadata snapshot to copy into the descriptor.
 */
extern void __initialize_file_descriptor(
    VaFsFileDescriptor_t*    descriptor,
    const struct VaFsMetadata* metadata);

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
 * @brief Initializes a symlink descriptor from already-normalized metadata.
 *
 * @param[Out] descriptor Symlink descriptor to initialize.
 * @param[In]  metadata   Metadata snapshot to copy into the descriptor.
 */
extern void __initialize_symlink_descriptor(
    VaFsSymlinkDescriptor_t*  descriptor,
    const struct VaFsMetadata* metadata);

/**
 * @brief Initializes a special-entry descriptor from already-normalized metadata.
 *
 * @param[Out] descriptor Special-entry descriptor to initialize.
 * @param[In]  metadata   Metadata snapshot to copy into the descriptor.
 */
extern void __initialize_special_descriptor(
    VaFsSpecialDescriptor_t*  descriptor,
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

/**
 * @brief Allocates a file handle wrapper for an already-resolved file entry.
 *
 * @param[In] fileEntry File entry to attach to the new handle.
 * @return A newly allocated handle on success, otherwise `NULL` with `errno` set.
 */
extern struct VaFsFileHandle* vafs_file_create_handle(
    struct VaFsFile* fileEntry);

/**
 * @brief Destroys a file entry object and releases its owned memory.
 *
 * @param[In] file File entry to destroy.
 */
extern void vafs_file_destroy(
    struct VaFsFile* file);

/**
 * @brief Destroys a symlink entry object and releases its owned memory.
 *
 * @param[In] symlink Symlink entry to destroy.
 */
extern void vafs_symlink_destroy(
    struct VaFsSymlink* symlink);

#endif // __VAFS_FS_PRIVATE_H_
