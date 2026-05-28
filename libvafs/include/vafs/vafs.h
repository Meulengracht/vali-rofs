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

#ifndef __VAFS_H__
#define __VAFS_H__

#include <stdint.h>
#include <stddef.h>
#include <vafs/platform.h>

#define VAFS_PATH_MAX 4096
#define VAFS_NAME_MAX 255

struct VaFs;
struct VaFsDirectoryHandle;
struct VaFsFileHandle;
struct VaFsSymlinkHandle;

/**
 * @brief Identifies a filesystem feature.
 */
VAFS_ONDISK_STRUCT(VaFsGuid, {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
});

/**
 * List of builtin features for the filesystem
 * VA_FS_FEATURE_OVERVIEW   - Overview of the filesystem
 * VA_FS_FEATURE_FILTER     - Stream filter policies for descriptor/data streams
 * VA_FS_FEATURE_FILTER_OPS - Filter operations (Not persistant)
 */
#define VA_FS_FEATURE_OVERVIEW   { 0xB1382352, 0x4BC7, 0x45D2, { 0xB7, 0x59, 0x61, 0x5A, 0x42, 0xD4, 0x45, 0x2A } }
#define VA_FS_FEATURE_FILTER     { 0x99C25D91, 0xFA99, 0x4A71, { 0x9C, 0xB5, 0x96, 0x1A, 0xA9, 0x3D, 0xDF, 0xBB } }
#define VA_FS_FEATURE_FILTER_OPS { 0x17BC0212, 0x7DF3, 0x4BDD, { 0x99, 0x24, 0x5A, 0xC8, 0x13, 0xBE, 0x72, 0x49 } }

/**
 * @brief Verbosity levels for library logging.
 */
enum VaFsLogLevel {
    VaFsLogLevel_Error,
    VaFsLogLevel_Warning,
    VaFsLogLevel_Info,
    VaFsLogLevel_Debug
};

/**
 * @brief Target architecture constraints stored in the image header.
 */
enum VaFsArchitecture {
    VaFsArchitecture_UNKNOWN = 0,
    VaFsArchitecture_X86 = 0x8086,
    VaFsArchitecture_X64 = 0x8664,
    VaFsArchitecture_ARM = 0xA12B,
    VaFsArchitecture_ARM64 = 0xAA64,
    VaFsArchitecture_RISCV32 = 0x5032,
    VaFsArchitecture_RISCV64 = 0x5064,
    VaFsArchitecture_ALL = 0xDEAD,
};

/**
 * @brief Common header for persisted feature payloads.
 */
VAFS_ONDISK_STRUCT(VaFsFeatureHeader, {
    struct VaFsGuid Guid;
    uint32_t        Length; // Length of the entire feature data including this header
});

/**
 * @brief Built-in overview feature describing image size and entry counts.
 */
VAFS_ONDISK_STRUCT(VaFsFeatureOverview, {
    struct VaFsFeatureHeader Header;
    uint64_t                 TotalSizeUncompressed;
    
    // Individual entry counts
    struct {
        uint32_t Files;
        uint32_t Directories;
        uint32_t Symlinks;
    } Counts;
});

/**
 * @brief Public entry kinds returned by directory enumeration.
 */
enum VaFsEntryType {
    VaFsEntryType_Unknown,
    VaFsEntryType_File,
    VaFsEntryType_Directory,
    VaFsEntryType_Symlink,
    VaFsEntryType_CharacterDevice,
    VaFsEntryType_BlockDevice,
    VaFsEntryType_Fifo,
    VaFsEntryType_Hardlink,
};

/**
 * @brief Describes a single directory entry returned by vafs_directory_read.
 */
struct VaFsEntry {
    const char*        Name;
    enum VaFsEntryType Type;
    uint64_t           ObjectId;
    uint32_t           MetadataMask;
};

/**
 * @brief Built-in filter identifiers stored in persisted filter policy features.
 */
enum VaFsFilterType {
    VaFsFilterType_None = 0,
    VaFsFilterType_APLIB,
    VaFsFilterType_BRIEFLZ,
};

/**
 * @brief It is expected of the encode function to allocate a buffer of the size of the data and provide
 * the size of the allocated buffer for the encoded data in the Output/OutputLength parameters.
 */
typedef int(*VaFsFilterEncodeFunc)(void* Input, uint32_t InputLength, void** Output, uint32_t* OutputLength);

/**
 * @brief The decode function will be provided with a buffer of the encoded data, and the size of the encoded data. The
 * decode function will also be provided with a buffer of the size of the decoded data, and the maximum size of the decoded data.
 * If the decoded data size varies from the maximum size provided, the size should be set to the actual decoded data size. 
 */
typedef int(*VaFsFilterDecodeFunc)(void* Input, uint32_t InputLength, void* Output, uint32_t* OutputLength);

/**
 * @brief Persisted filter policy for descriptor and data streams.
 */
VAFS_ONDISK_STRUCT(VaFsFeatureFilter, {
    struct VaFsFeatureHeader Header;
    uint32_t                 DescriptorType;
    uint32_t                 DataType;
});

/**
 * @brief Runtime filter callbacks used when a filesystem image is read or written through filtered streams.
 *
 * Install this feature after opening or creating an image. When reopening a filtered image, install the
 * callbacks after `vafs_open_*` returns and before the first directory, path, or file operation that would
 * force descriptor-stream decode. The callback table is consumed at runtime and is not serialized into the
 * on-disk image.
 */
struct VaFsFeatureFilterOps {
    struct VaFsFeatureHeader Header;
    VaFsFilterEncodeFunc     DescriptorEncode;
    VaFsFilterDecodeFunc     DescriptorDecode;
    VaFsFilterEncodeFunc     DataEncode;
    VaFsFilterDecodeFunc     DataDecode;
};

/**
 * @brief Configuration used when creating a new filesystem image.
 */
struct VaFsConfiguration {
    // Allow the filesystem to be valid only for a specific
    // architecture
    enum VaFsArchitecture Architecture;

    // The descriptor stream block size. Metadata streams are often smaller and more random-access
    // friendly than file payload streams, so they can use a different block size.
    uint32_t              DescriptorBlockSize;

    // The data block size can override the dynamic selection of 
    // block sizes, by enforcing all data block sizes to be of this
    // size. The allowed range for this value is 8kb - 1mb.
    uint32_t              DataBlockSize;
};

/**
 * @brief Initializes a configuration structure with library defaults.
 *
 * The default architecture is VaFsArchitecture_UNKNOWN and the block size is the library default.
 * Passing NULL is a no-op.
 *
 * @param configuration Configuration structure to initialize.
 */
extern void vafs_config_initialize(struct VaFsConfiguration* configuration);

/**
 * @brief Sets the architecture constraint to store in a creation configuration.
 *
 * Passing NULL is a no-op.
 *
 * @param configuration Configuration structure to update.
 * @param architecture  Architecture value to store in the image header.
 */
extern void vafs_config_set_architecture(struct VaFsConfiguration* configuration, enum VaFsArchitecture architecture);

/**
 * @brief Overrides the descriptor block size used for a newly created image.
 *
 * Values outside the supported range are ignored and reported through the library log. Passing NULL
 * is a no-op.
 *
 * @param configuration Configuration structure to update.
 * @param blockSize     Desired descriptor stream block size in bytes.
 */
extern void vafs_config_set_descriptor_block_size(struct VaFsConfiguration* configuration, uint32_t blockSize);

/**
 * @brief Overrides the data block size used for a newly created image.
 *
 * Values outside the supported range are ignored and reported through the library log. Passing NULL
 * is a no-op.
 *
 * @param configuration Configuration structure to update.
 * @param blockSize     Desired data stream block size in bytes.
 */
extern void vafs_config_set_data_block_size(struct VaFsConfiguration* configuration, uint32_t blockSize);

/**
 * @brief Allows custom backends as vafs images. The default API for vafs only supports
 * the standard C file, and memory backed images. To allow scenario's that differ from this
 * we allow the user to supply its own storage callbacks. Read-only backends may implement
 * either positioned reads through readAt or the legacy seek+read pair. It is the users
 * responsibility to make sure that the implementation of these functions are properly initialized
 * before calling vafs_open_ops, and properly disposed after the call to vafs_close.
 */
struct VaFsOperations {
    /**
     * @brief Seek to a specific position on the storage. If offset is 0
     * and whence is SEEK_CUR, then it will return the current position.
     * The seek operation is required, and must be provided by the implementation.
     * @param userData The user-supplied pointer that was given to vafs_open_ops
     * @param offset   The offset, described by <whence> on the storage.
     * @param whence   The origion of the offset.
     * @return The position on the storage after the seek.
     */
    long (*seek)(void* userData, long offset, int whence);

    /**
     * @brief Read bytes from the storage at the current position. The position will
     * advance by <bytesRead> if the operation returns 0.
     * The read function is required, and must be provided by the implementation.
     * @param userData  The user-supplied pointer that was given to vafs_open_ops
     * @param buffer    The buffer that will be used to store the data.
     * @param length    The number of bytes which will be read from the storage.
     * @param bytesRead The actual number of bytes read, up to max <length>.
     * @return int 0 on success, -1 on failure. See errno for more details.
     */
    int (*read)(void* userData, void*, size_t, size_t*);

    /**
     * @brief Read bytes from the storage at an absolute offset without mutating
     * any shared cursor state in the backend.
     * This callback is optional, but strongly recommended for read-only backends.
     * If omitted, VaFS falls back to serializing seek+read under an internal device lock.
     * @param userData  The user-supplied pointer that was given to vafs_open_ops.
     * @param offset    Absolute byte offset on the storage.
     * @param buffer    The buffer that will be used to store the data.
     * @param length    The number of bytes which will be read from the storage.
     * @param bytesRead The actual number of bytes read, up to max <length>.
     * @return int 0 on success, -1 on failure. See errno for more details.
     */
    int (*readAt)(void* userData, long offset, void* buffer, size_t length, size_t* bytesRead);

    /**
     * @brief Write bytes to the storage at the current position. The position will
     * advance by <bytesWritten> if the operation returns 0.
     * The write function is only required if the image is opened by a *_create function.
     * @param userData     The user-supplied pointer that was given to vafs_open_ops
     * @param buffer       The buffer which contains the data to be written.
     * @param length       The number of bytes which will be written to the storage.
     * @param bytesWritten The actual number of bytes written, up to max <length>.
     * @return int 0 on success, -1 on failure. See errno for more details.
     */
    int (*write)(void* userData, const void*, size_t, size_t*);

    /**
     * @brief When closing the stream, if this function was provided it will be 
     * called to indicate the vafs library is done with the underlying storage.
     * The close function is optional, and can be set to NULL.
     * @param userData The user-supplied pointer that was given to vafs_open_ops
     * @return int 0 on success, -1 on failure. See errno for more details.
     */
    int (*close)(void* userData);
};

/**
 * @brief Control the log level of the library. This is useful for debugging. The default
 * log level is set to VaFsLogLevel_Warning.
 * 
 * @param[In] level The level of log output to enable
 */
extern void vafs_log_initalize(
    enum VaFsLogLevel level);

/**
 * @brief Creates a new filesystem image. The image handle only permits operations that write
 * to the image. This means that reading from the image will fail.
 * 
 * @param[In]  path          The path the image file should be created at.
 * @param[In]  configuration Configuration parameters for the filesystem.
 * @param[Out] vafsOut       A pointer where the handle of the filesystem instance will be stored.
 * @return int 0 on success, -1 on failure.
 */
extern int vafs_create(
    const char*               path,
    struct VaFsConfiguration* configuration,
    struct VaFs**             vafsOut);

/**
 * @brief Opens an existing filesystem image. The image handle only permits operations that read
 * from the image. All images that are created by this library are read-only.
 * 
 * @param[In]  path    Path to the filesystem image. 
 * @param[Out] vafsOut A pointer where the handle of the filesystem instance will be stored.
 * @return int 0 on success, -1 on failure. See errno for more details.
 */
extern int vafs_open_file(
    const char*   path,
    struct VaFs** vafsOut);

/**
 * @brief Opens an existing filesystem image buffer. The image handle only permits operations that read
 * from the image. All images that are created by this library are read-only. The image buffer needs to stay
 * valid for duration of the time the vafs handle is used.
 *
 * @param[In]  buffer  Pointer to the filesystem image buffer.
 * @param[In]  size    Size of the filesystem image buffer.
 * @param[Out] vafsOut A pointer where the handle of the filesystem instance will be stored.
 * @return int 0 on success, -1 on failure. See errno for more details.
 */
extern int vafs_open_memory(
        const void*   buffer,
        size_t        size,
        struct VaFs** vafsOut);

/**
 * @brief Provides the user with the ability to supply their own underlying storage
 * implementation to be used, like a raw device, or a loop-back interface. This could
 * also be any other file implementation. The caller is responsible for cleaning up after
 * the call to vafs_close.
 * 
 * @param operations A pointer to the function table providing either readAt or the legacy seek+read pair.
 * @param userData   A pointer to user-supplied data which will be passed to operations.
 * @param vafsOut    A pointer where the handle of the filesystem instance will be stored.
 * @return int 0 on success, -1 on failure. See errno for more details
 */
extern int vafs_open_ops(
        struct VaFsOperations* operations,
        void*                  userData,
        struct VaFs**          vafsOut);

/**
 * @brief Closes the filesystem handle. If the image was just created, the data streams are kept in 
 * memory at this point and will not be written to disk before this function is called.
 * 
 * @param[In] vafs The filesystem handle to close. 
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_close(
    struct VaFs* vafs);

/**
 * @brief This installs a feature into the filesystem. The features must be installed after
 * creating or opening the image, before any other operations are performed.
 * Persisted feature payloads are written to the image byte-for-byte, so custom on-disk
 * feature structs should be declared with VAFS_ONDISK_STRUCT and use fixed-width field types.
 * Runtime-only features that are never serialized do not need the on-disk macro.
 * 
 * @param[In] vafs    The filesystem to install the feature into.
 * @param[In] feature The feature to install. The feature data is copied, so no need to keep the feature around.
 * @return int Returns -1 if the feature is already installed, 0 on success. 
 */
extern int vafs_feature_add(
    struct VaFs*              vafs,
    struct VaFsFeatureHeader* feature);

/**
 * @brief Checks if a specific feature is present in the filesystem image. 
 * 
 * @param[In]  vafs       The filesystem image to check.
 * @param[In]  guid       The GUID of the feature to check for.
 * @param[Out] featureOut A pointer to a feature header pointer which will be set to the feature.
 * @return int Returns -1 if the feature is not present, 0 if it is present.
 */
extern int vafs_feature_query(
    struct VaFs*               vafs,
    struct VaFsGuid*           guid,
    struct VaFsFeatureHeader** featureOut);

#endif //!__VAFS_H__
