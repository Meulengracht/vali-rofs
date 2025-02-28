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
#include <vafs.h>

struct VaFsStream;
struct VaFsStreamDevice;

typedef uint32_t vafsblock_t;

#define VA_FS_MAGIC       0x3144524D
#define VA_FS_VERSION     0x00010000

#define VA_FS_INVALID_BLOCK  0xFFFF
#define VA_FS_INVALID_OFFSET 0xFFFFFFFF

// I mean, do we really need more? But it's just a lazy implementation
// decision this.
#define VA_FS_MAX_FEATURES 16

// The default block size for the descriptor stream is 8kb
// The allowed block sizes for data streams are between 16kb - 1mb
#define VA_FS_DESCRIPTOR_BLOCK_SIZE  (8 * 1024)
#define VA_FS_DATA_MIN_BLOCKSIZE     (8 * 1024)
#define VA_FS_DATA_DEFAULT_BLOCKSIZE (128 * 1024)
#define VA_FS_DATA_MAX_BLOCKSIZE     (1024 * 1024)

// Logging macros
#define VAFS_ERROR(...)  vafs_log_message(VaFsLogLevel_Error, "libvafs: " __VA_ARGS__)
#define VAFS_WARN(...)   vafs_log_message(VaFsLogLevel_Warning, "libvafs: " __VA_ARGS__)
#define VAFS_INFO(...)   vafs_log_message(VaFsLogLevel_Info, "libvafs: " __VA_ARGS__)
#define VAFS_DEBUG(...)  vafs_log_message(VaFsLogLevel_Debug, "libvafs: " __VA_ARGS__)

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

struct VaFsFile {
    struct VaFs*         VaFs;
    VaFsFileDescriptor_t Descriptor;
    const char*          Name;
};

struct VaFsDirectory {
    struct VaFs*              VaFs;
    VaFsDirectoryDescriptor_t Descriptor;
    const char*               Name;
};

struct VaFsSymlink {
    struct VaFs*            VaFs;
    VaFsSymlinkDescriptor_t Descriptor;
    const char*             Name;
    const char*             Target;
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

    struct VaFsDirectory* RootDirectory;
};

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


extern int vafs_streamdevice_open_ops(
    struct VaFsOperations*    operations,
    void*                     userData,
    struct VaFsStreamDevice** deviceOut);

extern int vafs_streamdevice_create_file(
    const char*               path,
    struct VaFsStreamDevice** deviceOut);

extern int vafs_streamdevice_create_memory(
    size_t                    blockSize,
    struct VaFsStreamDevice** deviceOut);

extern int vafs_streamdevice_close(
    struct VaFsStreamDevice* device);

extern long vafs_streamdevice_seek(
    struct VaFsStreamDevice* device,
    long                     offset,
    int                      whence);

extern int vafs_streamdevice_read(
    struct VaFsStreamDevice* device,
    void*                    buffer,
    size_t                   length,
    size_t*                  bytesRead);

extern int vafs_streamdevice_write(
    struct VaFsStreamDevice* device,
    void*                    buffer,
    size_t                   length,
    size_t*                  bytesWritten);

extern int vafs_streamdevice_copy(
    struct VaFsStreamDevice* destination,
    struct VaFsStreamDevice* source);

extern int vafs_streamdevice_lock(
    struct VaFsStreamDevice* device);

extern int vafs_streamdevice_unlock(
    struct VaFsStreamDevice* device);

/**
 * @brief 
 * 
 * @param device 
 * @param deviceOffset 
 * @param blockSize 
 * @param streamOut 
 * @return int 
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
 * @brief 
 * 
 * @param stream 
 * @param encode 
 * @param decode 
 * @return int 
 */
extern int vafs_stream_set_filter(
    struct VaFsStream*   stream,
    VaFsFilterEncodeFunc encode,
    VaFsFilterDecodeFunc decode);

/**
 * @brief 
 * 
 * @param stream 
 * @param blockOut 
 * @param offsetOut 
 * @return int 
 */
extern int vafs_stream_position(
    struct VaFsStream* stream, 
    vafsblock_t*       blockOut,
    uint32_t*          offsetOut);

/**
 * @brief 
 * 
 * @param stream 
 * @param blockOut 
 * @param offsetOut 
 * @return int 
 */
extern int vafs_stream_seek(
    struct VaFsStream* stream, 
    vafsblock_t        blockIndex,
    uint32_t           blockOffset);

/**
 * @brief 
 * 
 * @param stream 
 * @param buffer 
 * @param size 
 * @return int 
 */
extern int vafs_stream_write(
    struct VaFsStream* stream,
    const void*        buffer,
    size_t             size);

/**
 * @brief 
 * 
 * @param stream 
 * @param buffer 
 * @param size 
 * @return int 
 */
extern int vafs_stream_read(
    struct VaFsStream* stream,
    void*              buffer,
    size_t             size,
    size_t*            bytesRead);

/**
 * @brief 
 * 
 * @param stream 
 * @return int 
 */
extern int vafs_stream_finish(
    struct VaFsStream* stream);

/**
 * @brief 
 * 
 * @param stream 
 * @return int 
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
 * @brief 
 * 
 * @param stream 
 * @param directoryOut 
 * @return int 
 */
extern int vafs_directory_create_root(
    struct VaFs*           vafs,
    struct VaFsDirectory** directoryOut);

/**
 * @brief 
 * 
 * @param directory 
 */
extern void vafs_directory_destroy(
    struct VaFsDirectory* directory);

/**
 * @brief 
 * 
 * @param directory 
 * @return int 
 */
extern int vafs_directory_flush(
    struct VaFsDirectory* directory);

/**
 * @brief 
 * 
 * @param stream 
 * @param directoryOut 
 * @return int 
 */
extern int vafs_directory_open_root(
    struct VaFs*           vafs,
    VaFsBlockPosition_t*   position,
    struct VaFsDirectory** directoryOut);

/**
 * @brief 
 * 
 * @param fileEntry 
 * @return struct VaFsFileHandle* 
 */
extern struct VaFsFileHandle* vafs_file_create_handle(
    struct VaFsFile* fileEntry);

/**
 * @brief 
 * 
 * @param file 
 */
extern void vafs_file_destroy(
    struct VaFsFile* file);

/**
 * @brief 
 * 
 * @param symlink 
 */
extern void vafs_symlink_destroy(
    struct VaFsSymlink* symlink);

/**
 * @brief 
 * 
 * @param level 
 * @param format 
 * @param ... 
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
};

struct VaFsDirectoryWriter {
    struct VaFsDirectory       Base;
    struct VaFsDirectoryEntry* Entries;
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

extern int __vafs_is_root_path(const char* path);
extern int __vafs_pathtoken(const char* path, char* token, size_t tokenSize);
extern int __vafs_resolve_symlink(char* buffer, size_t bufferLength, const char* baseStart, size_t baseLength, const char* symlinkTarget);
extern struct VaFsDirectoryEntry* __vafs_directory_entries(struct VaFsDirectory* directory);
extern const char* __vafs_directory_entry_name(struct VaFsDirectoryEntry* entry);

#endif // __VAFS_PRIVATE_H__
