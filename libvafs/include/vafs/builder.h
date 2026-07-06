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

#ifndef __VAFS_BUILDER_H__
#define __VAFS_BUILDER_H__

#include <stddef.h>
#include <stdint.h>

#include <vafs/backend.h>
#include <vafs/codec.h>
#include <vafs/types.h>

/**
 * @brief Configuration used when creating a new filesystem image.
 */
struct VaFsBuilderConfiguration {
    // Allow the filesystem to be valid only for a specific
    // architecture
    enum VaFsArchitecture Architecture;

    // Index 0 - The codec to use for the descriptor stream,
    //           this is the metadata stream that contains the filesystem descriptors.
    // Index 1 - The codec to use for the data stream,
    //           this is the stream that contains the file payloads.
    struct VaFsCodec      Codecs[2];

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
extern void vafs_builder_config_initialize(struct VaFsBuilderConfiguration* configuration);

/**
 * @brief Sets the architecture constraint to store in a creation configuration.
 *
 * Passing NULL is a no-op.
 *
 * @param configuration Configuration structure to update.
 * @param architecture  Architecture value to store in the image header.
 */
extern void vafs_builder_config_set_architecture(struct VaFsBuilderConfiguration* configuration, enum VaFsArchitecture architecture);

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
extern void vafs_builder_config_set_codec(struct VaFsBuilderConfiguration* configuration, struct VaFsCodec* codec, int index);

/**
 * @brief Overrides the descriptor block size used for a newly created image.
 *
 * Values outside the supported range are ignored and reported through the library log. Passing NULL
 * is a no-op.
 *
 * @param configuration Configuration structure to update.
 * @param blockSize     Desired descriptor stream block size in bytes.
 */
extern void vafs_builder_config_set_descriptor_block_size(struct VaFsBuilderConfiguration* configuration, uint32_t blockSize);

/**
 * @brief Overrides the data block size used for a newly created image.
 *
 * Values outside the supported range are ignored and reported through the library log. Passing NULL
 * is a no-op.
 *
 * @param configuration Configuration structure to update.
 * @param blockSize     Desired data stream block size in bytes.
 */
extern void vafs_builder_config_set_data_block_size(struct VaFsBuilderConfiguration* configuration, uint32_t blockSize);

/**
 * @brief Creates a new filesystem image. The image handle only permits operations that write
 * to the image. This means that reading from the image will fail.
 * 
 * @param[In]  path          The path the image file should be created at.
 * @param[In]  configuration Configuration parameters for the filesystem.
 * @param[Out] vafsOut       A pointer where the handle of the filesystem instance will be stored.
 * @return int 0 on success, -1 on failure.
 */
extern int vafs_builder_new(
    const char*                      path,
    struct VaFsBuilderConfiguration* configuration,
    struct VaFs**                    vafsOut);

/**
 * @brief Closes the filesystem handle. If the image was just created, the data streams are kept in 
 * memory at this point and will not be written to disk before this function is called.
 * 
 * @param[In] vafs The filesystem handle to close. 
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_builder_close(
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
extern int vafs_builder_add_feature(
    struct VaFs*              vafs,
    struct VaFsFeatureHeader* feature);

#endif //!__VAFS_BUILDER_H__