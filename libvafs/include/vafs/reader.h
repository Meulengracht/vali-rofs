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
 * - Declares the draft backend contracts for the next-generation VaFS public API.
 */

#ifndef __VAFS_READER_H__
#define __VAFS_READER_H__

#include <stddef.h>
#include <stdint.h>

#include <vafs/backend.h>
#include <vafs/codec.h>
#include <vafs/types.h>

/**
 * @brief Configuration used when creating a new filesystem image.
 */
struct VaFsReaderConfiguration {
    // Allow the filesystem to be valid only for a specific
    // architecture
    enum VaFsArchitecture Architecture;
    struct VaFsCodec      Codecs[2];
};

/**
 * @brief Initializes a configuration structure with library defaults.
 *
 * The default architecture is VaFsArchitecture_UNKNOWN and the block size is the library default.
 * Passing NULL is a no-op.
 *
 * @param configuration Configuration structure to initialize.
 */
extern void vafs_reader_config_initialize(struct VaFsReaderConfiguration* configuration);

/**
 * @brief Sets the architecture constraint to store in a creation configuration.
 *
 * Passing NULL is a no-op.
 *
 * @param configuration Configuration structure to update.
 * @param architecture  Architecture value to store in the image header.
 */
extern void vafs_reader_config_set_architecture(struct VaFsReaderConfiguration* configuration, enum VaFsArchitecture architecture);

/**
 * @brief Registers a codec in a configuration.
 * 
 * Passing NULL is a no-op.
 * 
 * @param configuration Configuration structure to update.
 * @param codec         Codec descriptor to register.
 * @param index         Index of the codec in the configuration. Must be 0 or 1.
 *                      0 is the metadata codec, 1 is the data codec.
 */
extern void vafs_reader_config_set_codec(struct VaFsReaderConfiguration* configuration, struct VaFsCodec* codec, int index);

/**
 * @brief Opens an existing filesystem image. The image handle only permits operations that read
 * from the image. All images that are created by this library are read-only.
 * 
 * @param[In]  path    Path to the filesystem image. 
 * @param[Out] vafsOut A pointer where the handle of the filesystem instance will be stored.
 * @return int 0 on success, -1 on failure. See errno for more details.
 */
extern int vafs_reader_open_file(
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
extern int vafs_reader_open_memory(
        const void*   buffer,
        size_t        size,
        struct VaFs** vafsOut);

/**
 * @brief Provides the user with the ability to supply their own underlying storage
 * implementation to be used, like a raw device, or a loop-back interface. This could
 * also be any other file implementation. The caller is responsible for cleaning up after
 * the call to vafs_reader_close.
 * 
 * @param operations A pointer to the function table providing either readAt or the legacy seek+read pair.
 * @param userData   A pointer to user-supplied data which will be passed to operations.
 * @param vafsOut    A pointer where the handle of the filesystem instance will be stored.
 * @return int 0 on success, -1 on failure. See errno for more details
 */
extern int vafs_reader_open_ops(
        struct VaFsReaderBackendOps* operations,
        void*                        userData,
        struct VaFs**                vafsOut);

/**
 * @brief Closes the filesystem handle. If the image was just created, the data streams are kept in 
 * memory at this point and will not be written to disk before this function is called.
 * 
 * @param[In] vafs The filesystem handle to close. 
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_reader_close(
    struct VaFs* vafs);

/**
 * @brief Checks if a specific feature is present in the filesystem image. 
 * 
 * @param[In]  vafs       The filesystem image to check.
 * @param[In]  guid       The GUID of the feature to check for.
 * @param[Out] featureOut A pointer to a feature header pointer which will be set to the feature.
 * @return int Returns -1 if the feature is not present, 0 if it is present.
 */
extern int vafs_reader_query_feature(
    struct VaFs*               vafs,
    struct VaFsGuid*           guid,
    struct VaFsFeatureHeader** featureOut);

#endif //!__VAFS_READER_H__