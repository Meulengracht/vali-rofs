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

#ifndef __VAFS_PRIVATE_H__
#define __VAFS_PRIVATE_H__

#include <platform.h>
#include <stdint.h>
#include <stdio.h>
#include "cache/hashtable.h"
#include <vafs.h>
#include <vafs/stat.h>

struct VaFsStream;
struct VaFsStreamReader;
struct VaFsStreamDevice;

typedef uint32_t vafsblock_t;

#define VA_FS_MAGIC       0x3144524D
#define VA_FS_VERSION     0x00010000

#define VA_FS_INVALID_BLOCK  0xFFFF
#define VA_FS_INVALID_OFFSET 0xFFFFFFFF

// I mean, do we really need more? But it's just a lazy implementation
// decision this.
#define VA_FS_MAX_FEATURES 16

// The default block size for the descriptor stream is 8kb.
// Both descriptor and data streams currently use the same supported range.
#define VA_FS_DESCRIPTOR_BLOCK_SIZE  (8 * 1024)
#define VA_FS_DATA_MIN_BLOCKSIZE     (8 * 1024)
#define VA_FS_DATA_DEFAULT_BLOCKSIZE (128 * 1024)
#define VA_FS_DATA_MAX_BLOCKSIZE     (1024 * 1024)

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

// Logging macros
#define VAFS_ERROR(...)  vafs_log_message(VaFsLogLevel_Error, "libvafs: " __VA_ARGS__)
#define VAFS_WARN(...)   vafs_log_message(VaFsLogLevel_Warning, "libvafs: " __VA_ARGS__)
#define VAFS_INFO(...)   vafs_log_message(VaFsLogLevel_Info, "libvafs: " __VA_ARGS__)
#define VAFS_DEBUG(...)  vafs_log_message(VaFsLogLevel_Debug, "libvafs: " __VA_ARGS__)

// Symlink resolution limits
// Maximum number of symlinks that can be traversed in a single path resolution
// This prevents infinite loops from cyclic symlinks and limits resource consumption
#define VAFS_SYMLINK_MAX_DEPTH 40

VAFS_ONDISK_STRUCT(VaFsBlockPosition, {
    vafsblock_t Index;
    uint32_t    Offset;
});

VAFS_ONDISK_STRUCT(VaFsHeader, {
    uint32_t            Magic;
    uint32_t            Version;
    uint32_t            Architecture;
    uint16_t            FeatureCount;
    uint16_t            Reserved;
    uint32_t            Attributes;
    uint32_t            DescriptorBlockOffset;
    uint32_t            DataBlockOffset;
    VaFsBlockPosition_t RootDescriptor;
});

#define VA_FS_DESCRIPTOR_TYPE_FILE      0x01
#define VA_FS_DESCRIPTOR_TYPE_DIRECTORY 0x02
#define VA_FS_DESCRIPTOR_TYPE_SYMLINK   0x03

VAFS_ONDISK_STRUCT(VaFsDescriptor, {
    uint16_t Type;
    uint16_t Length; // Length of the descriptor
});

VAFS_ONDISK_STRUCT(VaFsFileDescriptor, {
    VaFsDescriptor_t    Base;
    VaFsBlockPosition_t Data;
    uint32_t            FileLength;
    uint32_t            Permissions;
});

VAFS_ONDISK_STRUCT(VaFsDirectoryDescriptor, {
    VaFsDescriptor_t    Base;
    VaFsBlockPosition_t Descriptor;
    uint32_t            Permissions;
});

VAFS_ONDISK_STRUCT(VaFsDirectoryHeader, {
    uint32_t Count;
});

VAFS_ONDISK_STRUCT(VaFsSymlinkDescriptor, {
    VaFsDescriptor_t    Base;
    uint16_t            NameLength;
    uint16_t            TargetLength;
});

enum VaFsMode {
    VaFsMode_Read,
    VaFsMode_Write
};

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

struct VaFsFile {
    struct VaFs*         VaFs;
    VaFsFileDescriptor_t Descriptor;
    const char*          Name;
    struct vafs_stat     Stat;
    int                  StatCached;
};

struct VaFsDirectory {
    struct VaFs*              VaFs;
    VaFsDirectoryDescriptor_t Descriptor;
    const char*               Name;
    struct vafs_stat          Stat;
    int                       StatCached;
};

struct VaFsSymlink {
    struct VaFs*            VaFs;
    VaFsSymlinkDescriptor_t Descriptor;
    const char*             Name;
    const char*             Target;
    struct vafs_stat        Stat;
    int                     StatCached;
};

struct VaFs {
    VaFsHeader_t               Header;
    enum VaFsMode              Mode;
    struct VaFsFeatureOverview Overview;

    // Features present
    struct VaFsFeatureHeader** Features;
    int                        FeatureCount;
    
    // The file stream device
    struct VaFsStreamDevice* ImageDevice;

    // The following two streams are either tied up to the
    // the image device (reading), or to a temporary device (writing).
    struct VaFsStreamDevice* DescriptorDevice;
    struct VaFsStream*       DescriptorStream;
    struct VaFsStreamDevice* DataDevice;
    struct VaFsStream*       DataStream;

    struct VaFsDirectory*  RootDirectory;
    struct VaFsLookupCache LookupCache;
};

/**
 * @brief Opens a file-backed stream device for read access to a VaFS image.
 *
 * @param[In]  path      Path to the backing file on the host filesystem.
 * @param[Out] deviceOut Receives the opened stream device on success.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_open_file(
    const char*               path,
    struct VaFsStreamDevice** deviceOut);

/**
 * @brief Wraps the provided buffer in a streamdevice object. Enabling the use
 * of the entire stremadevice API for the buffer. The buffer must stay valid untill
 * vafs_streamdevice_close has been called. This will not free the buffer.
 *
 * @param[In]  buffer    A pointer to the image buffer that should be used.
 * @param[In]  length    The length of the image buffer.
 * @param[Out] deviceOut A pointer to where to store the handle of the stream device.
 * @return Returns -1 if any error occured, otherwise 0.
 */
extern int vafs_streamdevice_open_memory(
    const void*               buffer,
    size_t                    length,
    struct VaFsStreamDevice** deviceOut);


/**
 * @brief Opens a stream device backed by user-provided callbacks.
 *
 * @param[In]  operations Callback table implementing the device operations.
 * @param[In]  userData   Opaque user context passed back to the callbacks.
 * @param[Out] deviceOut  Receives the opened stream device on success.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_open_ops(
    struct VaFsOperations*    operations,
    void*                     userData,
    struct VaFsStreamDevice** deviceOut);

/**
 * @brief Creates a writable file-backed stream device.
 *
 * @param[In]  path      Destination file path.
 * @param[Out] deviceOut Receives the created stream device on success.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_create_file(
    const char*               path,
    struct VaFsStreamDevice** deviceOut);

/**
 * @brief Creates an in-memory writable stream device.
 *
 * @param[In]  blockSize Initial allocation granularity used by the memory device.
 * @param[Out] deviceOut Receives the created stream device on success.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_create_memory(
    size_t                    blockSize,
    struct VaFsStreamDevice** deviceOut);

/**
 * @brief Closes a stream device and releases any owned resources.
 *
 * @param[In] device The stream device to close.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_close(
    struct VaFsStreamDevice* device);

/**
 * @brief Repositions a stream device's current offset.
 *
 * @param[In] device The stream device to seek.
 * @param[In] offset Offset interpreted according to `whence`.
 * @param[In] whence Standard seek origin such as `SEEK_SET`, `SEEK_CUR`, or `SEEK_END`.
 * @return The resulting absolute position, or a negative value on failure.
 */
extern long vafs_streamdevice_seek(
    struct VaFsStreamDevice* device,
    long                     offset,
    int                      whence);

/**
 * @brief Reads bytes directly from a stream device.
 *
 * @param[In]  device    The stream device to read from.
 * @param[Out] buffer    Destination buffer for the bytes read.
 * @param[In]  length    Number of bytes requested.
 * @param[Out] bytesRead Receives the number of bytes actually read.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_read(
    struct VaFsStreamDevice* device,
    void*                    buffer,
    size_t                   length,
    size_t*                  bytesRead);

/**
 * @brief Reads bytes from a stream device at an absolute offset.
 *
 * This is the read-only fast path used by stream and metadata loaders. Devices
 * with native positioned reads can avoid shared cursor mutations entirely.
 * Devices without that support fall back to an internal seek+read sequence.
 *
 * @param[In]  device    The stream device to read from.
 * @param[In]  offset    Absolute byte offset to read from.
 * @param[Out] buffer    Destination buffer for the bytes read.
 * @param[In]  length    Number of bytes requested.
 * @param[Out] bytesRead Receives the number of bytes actually read.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_read_at(
    struct VaFsStreamDevice* device,
    long                     offset,
    void*                    buffer,
    size_t                   length,
    size_t*                  bytesRead);

/**
 * @brief Writes bytes directly to a stream device.
 *
 * @param[In]  device       The stream device to write to.
 * @param[In]  buffer       Source buffer containing the bytes to write.
 * @param[In]  length       Number of bytes to write.
 * @param[Out] bytesWritten Receives the number of bytes actually written.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_write(
    struct VaFsStreamDevice* device,
    void*                    buffer,
    size_t                   length,
    size_t*                  bytesWritten);

/**
 * @brief Copies the complete contents of one stream device into another.
 *
 * @param[In] destination Device to receive the copied bytes.
 * @param[In] source      Device to copy from.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_copy(
    struct VaFsStreamDevice* destination,
    struct VaFsStreamDevice* source);

/**
 * @brief Locks a stream device for exclusive access.
 *
 * @param[In] device The device to lock.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_lock(
    struct VaFsStreamDevice* device);

/**
 * @brief Unlocks a previously locked stream device.
 *
 * @param[In] device The device to unlock.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_unlock(
    struct VaFsStreamDevice* device);

/**
 * @brief Creates a writable block stream on top of a stream device.
 *
 * The created stream writes its header immediately and stages data into
 * fixed-size blocks until the stream is finished.
 *
 * @param[In]  device       Backing device used to store the stream contents.
 * @param[In]  deviceOffset Byte offset where the stream begins in the device.
 * @param[In]  blockSize    Block size used for staging and on-disk layout.
 * @param[Out] streamOut    Receives the created stream instance on success.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_stream_create(
    struct VaFsStreamDevice* device,
    long                     deviceOffset,
    uint32_t                 blockSize,
    struct VaFsStream**      streamOut);

/**
 * @brief Open a new stream for reading from the provided stream device.
 * 
 * @param[In]  device       The stream device to read from.
 * @param[In]  deviceOffset The offset in the device to start reading from.
 * @param[Out] streamOut    A pointer to where to store the handle of the stream.
 * @return int 0 if the stream was valid and successfully opened, otherwise -1.
 */
extern int vafs_stream_open(
    struct VaFsStreamDevice* device,
    long                     deviceOffset,
    struct VaFsStream**      streamOut);

/**
 * @brief Installs encode and decode callbacks for a stream.
 *
 * These callbacks are used when blocks are written to or read from the
 * backing device.
 *
 * @param[In] stream The stream to update.
 * @param[In] encode Optional block encoder used on writes.
 * @param[In] decode Optional block decoder used on reads.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_stream_set_filter(
    struct VaFsStream*   stream,
    VaFsFilterEncodeFunc encode,
    VaFsFilterDecodeFunc decode);

/**
 * @brief Retrieves the current logical write position inside a stream.
 *
 * @param[In]  stream    The stream to query.
 * @param[Out] blockOut  Receives the current block index.
 * @param[Out] offsetOut Receives the current offset within that block.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_stream_position(
    struct VaFsStream* stream, 
    vafsblock_t*       blockOut,
    uint32_t*          offsetOut);

/**
 * @brief Creates a read cursor for a stream.
 *
 * Each reader owns its own staged block buffer and logical position so
 * independent callers can read the same stream concurrently.
 *
 * @param[In]  stream     Stream instance to read from.
 * @param[Out] readerOut  Receives the allocated reader.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_stream_reader_open(
    struct VaFsStream*        stream,
    struct VaFsStreamReader** readerOut);

/**
 * @brief Destroys a previously created stream reader.
 *
 * @param[In] reader Reader to destroy. NULL is ignored.
 */
extern void vafs_stream_reader_close(
    struct VaFsStreamReader* reader);

/**
 * @brief Retrieves the configured block size for a stream.
 *
 * @param[In] stream Stream instance to query.
 * @return Stream block size in bytes, or 0 if stream is invalid.
 */
extern uint32_t vafs_stream_block_size(
    struct VaFsStream* stream);

/**
 * @brief Seeks to a logical block and offset within a stream.
 *
 * @param[In] stream      The stream to reposition.
 * @param[In] blockIndex  Destination block index.
 * @param[In] blockOffset Destination byte offset within the block.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_stream_reader_seek(
    struct VaFsStreamReader* reader,
    vafsblock_t        blockIndex,
    uint32_t           blockOffset);

/**
 * @brief Writes bytes into a writable stream.
 *
 * Data is appended at the current logical stream position and staged into the
 * stream's block buffer.
 *
 * @param[In] stream The stream to write to.
 * @param[In] buffer Source buffer containing the bytes to write.
 * @param[In] size   Number of bytes to write.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_stream_write(
    struct VaFsStream* stream,
    const void*        buffer,
    size_t             size);

/**
 * @brief Reads bytes from a stream at the current logical position.
 *
 * @param[In]  stream    The stream to read from.
 * @param[Out] buffer    Destination buffer for the bytes read.
 * @param[In]  size      Maximum number of bytes to read.
 * @param[Out] bytesRead Receives the number of bytes actually read.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_stream_reader_read(
    struct VaFsStreamReader* reader,
    void*              buffer,
    size_t             size,
    size_t*            bytesRead);

/**
 * @brief Finalizes a writable stream and flushes its metadata.
 *
 * This writes any pending block data, serializes the block header table, and
 * updates the on-disk stream header.
 *
 * @param[In] stream The stream to finish.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_stream_finish(
    struct VaFsStream* stream);

/**
 * @brief Closes a stream and frees its in-memory state.
 *
 * This does not implicitly finish a writable stream; callers are expected to
 * call `vafs_stream_finish()` first when they need the final metadata written.
 *
 * @param[In] stream The stream to close.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_stream_close(
    struct VaFsStream* stream);

/**
 * @brief Locks a specific stream for exclusive access, this is neccessary while
 * writing data to the stream, to avoid any concurrent access to those streams, or
 * the user deciding to write two files at once. For read access this is not as neccessary
 * but could still be done.
 * 
 * @param[In] stream The stream that should be locked.
 * @return int Returns -1 if the stream is already locked, 0 on success.
 */
extern int vafs_stream_lock(
    struct VaFsStream* stream);

/**
 * @brief Unlocks a previously locked stream.
 * 
 * @param[In] stream The stream that should be unlocked.
 * @return int Returns -1 if the stream was not locked, 0 on success.
 */
extern int vafs_stream_unlock(
    struct VaFsStream* stream);

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

/**
 * @brief Emits a formatted log message through the library logging backend.
 *
 * @param[In] level  Severity level for the message.
 * @param[In] format `printf`-style format string.
 * @param[In] ...    Format arguments.
 */
extern void vafs_log_message(
    enum VaFsLogLevel level,
    const char*       format,
    ...);

// Utility functions
struct VaFsDirectoryEntry;

enum VaFsDirectoryState {
    VaFsDirectoryState_Open,
    VaFsDirectoryState_Loaded
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

struct VaFsDirectoryEntry {
    int Type;
    union {
        struct VaFsFile*      File;
        struct VaFsDirectory* Directory;
        struct VaFsSymlink*   Symlink;
    };
    struct VaFsDirectoryEntry* Link;
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
 * and fills the lightweight `vafs_stat` structure for the final entry.
 *
 * @param[In]  vafs         Filesystem instance to resolve within.
 * @param[In]  path         Absolute or root-relative path to stat.
 * @param[In]  followLinks  Non-zero to follow symlinks, zero to report the symlink itself.
 * @param[Out] stat         Receives the resolved metadata on success.
 * @param[In]  symlinkDepth Current recursive symlink depth.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int __vafs_path_stat_internal(struct VaFs* vafs, const char* path, int followLinks, struct vafs_stat* stat, int symlinkDepth);

/**
 * @brief Internal directory-open implementation with explicit symlink depth tracking.
 *
 * @param[In]  vafs         Filesystem instance to resolve within.
 * @param[In]  path         Path to the directory that should be opened.
 * @param[Out] handleOut    Receives the opened directory handle on success.
 * @param[In]  symlinkDepth Current recursive symlink depth.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int __vafs_directory_open_internal(struct VaFs* vafs, const char* path, struct VaFsDirectoryHandle** handleOut, int symlinkDepth);

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
 * @brief Materializes a lightweight stat structure for a directory entry.
 *
 * Read mode may satisfy this from cached metadata stored on the entry object.
 *
 * @param[In]  entry Directory entry to describe.
 * @param[Out] stat  Receives the entry metadata on success.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int __vafs_directory_entry_stat(struct VaFsDirectoryEntry* entry, struct vafs_stat* stat);

/**
 * @brief Returns the stable name string associated with a directory entry.
 *
 * @param[In] entry Directory entry to inspect.
 * @return Entry name string, or `NULL` if the entry type is invalid.
 */
extern const char* __vafs_directory_entry_name(struct VaFsDirectoryEntry* entry);

#endif // __VAFS_PRIVATE_H__
