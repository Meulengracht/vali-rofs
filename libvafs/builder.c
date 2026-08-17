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

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/builder.h>

#include "private.h"

static void vafs_destroy(
    struct VaFs* vafs);

static inline int __compare_guids(
    struct VaFsGuid* lh,
    struct VaFsGuid* rh)
{
    return memcmp(lh, rh, sizeof(struct VaFsGuid));
}

void vafs_builder_config_initialize(struct VaFsBuilderConfiguration* configuration)
{
    if (configuration == NULL) {
        return;
    }
    memset(configuration, 0, sizeof(struct VaFsBuilderConfiguration));
    configuration->Architecture = VaFsArchitecture_ALL;
    configuration->DescriptorBlockSize = VA_FS_DATA_DEFAULT_BLOCKSIZE;
    configuration->DataBlockSize = VA_FS_DATA_DEFAULT_BLOCKSIZE;
}

void vafs_builder_config_set_architecture(struct VaFsBuilderConfiguration* configuration, enum VaFsArchitecture architecture)
{
    if (configuration == NULL) {
        return;
    }
    configuration->Architecture = architecture;
}

void vafs_builder_config_set_codec(struct VaFsBuilderConfiguration* configuration, struct VaFsCodec* codec, int index)
{
    if (configuration == NULL || codec == NULL || index < 0 || index > 1) {
        return;
    }
    configuration->Codecs[index] = *codec;
}

void vafs_builder_config_set_descriptor_block_size(struct VaFsBuilderConfiguration* configuration, uint32_t blockSize)
{
    if (configuration == NULL) {
        return;
    }
    configuration->DescriptorBlockSize = blockSize;
}

void vafs_builder_config_set_data_block_size(struct VaFsBuilderConfiguration* configuration, uint32_t blockSize)
{
    if (configuration == NULL) {
        return;
    }
    configuration->DataBlockSize = blockSize;
}

static int __initialize_fsstreams_write(
    struct VaFs*                     vafs,
    struct VaFsBuilderConfiguration* configuration)
{
    int status;
    
    VAFS_DEBUG("__initialize_fsstreams_write: vafs: %p\n", vafs);
    // Allocate descriptor and data streams independently so each stream can
    // carry its own block size and filter policy from the start.
    status = vafs_streamdevice_create_memory(
        configuration->DescriptorBlockSize,
        &vafs->DescriptorDevice
    );
    if (status) {
        VAFS_ERROR("__initialize_fsstreams_write: failed to create descriptor stream device: %i\n", status);
        return status;
    }

    status = vafs_streamdevice_create_memory(
        configuration->DataBlockSize,
        &vafs->DataDevice
    );
    if (status) {
        VAFS_ERROR("__initialize_fsstreams_write: failed to create data stream device: %i\n", status);
        return status;
    }

    status = vafs_stream_create(
        vafs->DescriptorDevice, 
        configuration->DescriptorBlockSize,
        &vafs->DescriptorStream
    );
    if (status) {
        VAFS_ERROR("__initialize_fsstreams_write: failed to create descriptor stream: %i\n", status);
        return status;
    }

    status = vafs_stream_create(
        vafs->DataDevice, 
        configuration->DataBlockSize,
        &vafs->DataStream
    );
    return status;
}

static void __initialize_header(
    struct VaFs*                     vafs,
    struct VaFsBuilderConfiguration* configuration)
{
    vafs->Header.Magic = VA_FS_MAGIC;
    vafs->Header.Version = VA_FS_VERSION;
    vafs->Header.Architecture = configuration->Architecture;
    vafs->Header.FeatureCount = 0;
    vafs->Header.Reserved = 0;
    vafs->Header.Attributes = 0;
}

static int __initialize_imagestream(
    struct VaFs*                     vafs,
    struct VaFsStreamDevice*         imageDevice,
    struct VaFsBuilderConfiguration* configuration)
{
    int status = 0;

    VAFS_DEBUG("__initialize_imagestream()\n");

    // seed a fresh header for image creation.
    vafs->ImageDevice = imageDevice;
    __initialize_header(vafs, configuration);
    return status;
}

static int __new_vafs(
    enum VaFsMode                    mode,
    struct VaFsStreamDevice*         imageDevice,
    struct VaFsBuilderConfiguration* configuration,
    struct VaFs**                    vafsOut)
{
    struct VaFs* vafs;
    int          status;

    // Construction happens in phases: outer image header/features, inner
    // streams, then root-directory state and any runtime feature handling.

    if (imageDevice == NULL || vafsOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    // ensure the library is initialized before any reader instance is created
    vafs_init();

    vafs = (struct VaFs*)malloc(sizeof(struct VaFs));
    if (!vafs) {
        errno = ENOMEM;
        return -1;
    }
    memset(vafs, 0, sizeof(struct VaFs));

    vafs->Mode = mode;

    vafs->Features = malloc(sizeof(struct VaFsFeatureHeader*) * VA_FS_MAX_FEATURES);
    if (!vafs->Features) {
        vafs_destroy(vafs);
        errno = ENOMEM;
        return -1;
    }

    // try to create the output file, otherwise do not continue
    status = __initialize_imagestream(vafs, imageDevice, configuration);
    if (status) {
        VAFS_ERROR("__new_vafs: failed to initialize image stream: %i\n", status);
        vafs_destroy(vafs);
        return -1;
    }

    status = __initialize_fsstreams_write(vafs, configuration);
    if (status) {
        VAFS_ERROR("__new_vafs: failed to initialize filesystem streams: %i\n", status);
        vafs_destroy(vafs);
        return -1;
    }

    if (configuration->Codecs[0].ID != NULL) {
        vafs_stream_set_filter(
            vafs->DescriptorStream,
            configuration->Codecs[0].Encode,
            configuration->Codecs[0].Decode
        );
    }
    if (configuration->Codecs[1].ID != NULL) {
        vafs_stream_set_filter(
            vafs->DataStream,
            configuration->Codecs[1].Encode,
            configuration->Codecs[1].Decode
        );
    }

    status = vafs_directory_create_root(vafs, &vafs->RootDirectory);
    if (status) {
        VAFS_ERROR("__new_vafs: failed to initialize root directory: %i\n", status);
        vafs_destroy(vafs);
        return -1;
    }

    *vafsOut = vafs;
    return 0;
}

static void __initialize_overview(
    struct VaFs* vafs)
{
    // initialize the overview
    memcpy(&vafs->Overview.Header.Guid, &g_overviewGuid, sizeof(struct VaFsGuid));
    vafs->Overview.Header.Length = sizeof(struct VaFsFeatureOverview);
}

int vafs_builder_new(
    const char*                      path,
    struct VaFsBuilderConfiguration* configuration,
    struct VaFs**                    vafsOut,
    struct VaFsDirectoryBuilder**    builderOut)
{
    struct VaFsStreamDevice* imageDevice;
    int                      status;

    VAFS_INFO("vafs_builder_new: creating new image file\n");

    if (path == NULL || configuration == NULL || vafsOut == NULL || builderOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = vafs_streamdevice_create_file(path, &imageDevice);
    if (status) {
        VAFS_ERROR("vafs_builder_new: failed to create image file: %i\n", status);
        return status;
    }
    
    status = __new_vafs(VaFsMode_Write, imageDevice, configuration, vafsOut);
    if (status) {
        VAFS_ERROR("vafs_builder_new: failed to create new vafs instance: %i\n", status);
        return status;
    }

    __initialize_overview(*vafsOut);

    // Open the root directory handle for callers to use. This builder handle
    // will represent the root directory.
    *builderOut = malloc(sizeof(struct VaFsDirectoryBuilder));
    if (*builderOut == NULL) {
        vafs_destroy(*vafsOut);
        *vafsOut = NULL;
        errno = ENOMEM;
        return -1;
    }

    (*builderOut)->Directory = (*vafsOut)->RootDirectory;
    (*builderOut)->Index = 0;
    return 0;
}

static int __write_vafs_features(
    struct VaFs* vafs)
{
    size_t written;
    int    status;
    int    i;
    VAFS_DEBUG("__write_vafs_features: count=%i\n", vafs->FeatureCount);

    for (i = 0; i < vafs->FeatureCount; i++) {
        VAFS_INFO("__write_vafs_features: writing feature: %i\n", i);
        status = vafs_streamdevice_write(
            vafs->ImageDevice,
            vafs->Features[i],
            vafs->Features[i]->Length,
            &written
        );
        if (status) {
            VAFS_ERROR("__write_vafs_features: failed to write feature header: %i\n", status);
            return status;
        }
    }

    return 0;
}

static int __relocate_stream_layout(
    VaFsStreamLayout_t* layout,
    uint64_t            baseOffset)
{
    if (layout == NULL || baseOffset > UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    if ((uint64_t)layout->DataOffset + baseOffset > UINT32_MAX ||
        (uint64_t)layout->IndexOffset + baseOffset > UINT32_MAX) {
        errno = EOVERFLOW;
        return -1;
    }

    layout->DataOffset += (uint32_t)baseOffset;
    layout->IndexOffset += (uint32_t)baseOffset;
    return 0;
}

static int __write_vafs_header(
    struct VaFs* vafs)
{
    size_t   written;
    uint64_t descriptorStreamSize;
    uint64_t descriptorStreamOffset;
    uint64_t dataStreamOffset;
    int      i;
    VAFS_INFO("__write_vafs_header: writing header\n");

    if (vafs_streamdevice_size(vafs->DescriptorDevice, &descriptorStreamSize)) {
        VAFS_ERROR("__write_vafs_header: failed to get descriptor stream size\n");
        return -1;
    }

    vafs->Header.FeatureCount = (uint16_t)vafs->FeatureCount;

    // Stream offsets are absolute in the final image. The streams themselves
    // were built in temporary append-only devices, so relocate their finished
    // layouts to the positions they will occupy after the header/features.
    descriptorStreamOffset = sizeof(VaFsHeader_t);
    for (i = 0; i < vafs->FeatureCount; i++) {
        descriptorStreamOffset += vafs->Features[i]->Length;
    }
    dataStreamOffset = descriptorStreamOffset + descriptorStreamSize;

    if (__relocate_stream_layout(&vafs->Header.DescriptorStream, descriptorStreamOffset) ||
        __relocate_stream_layout(&vafs->Header.DataStream, dataStreamOffset)) {
        VAFS_ERROR("__write_vafs_header: failed to relocate stream layouts\n");
        return -1;
    }

    VAFS_DEBUG("__write_vafs_header: descriptor data offset: %u\n", vafs->Header.DescriptorStream.DataOffset);
    VAFS_DEBUG("__write_vafs_header: descriptor index offset: %u\n", vafs->Header.DescriptorStream.IndexOffset);
    VAFS_DEBUG("__write_vafs_header: data data offset: %u\n", vafs->Header.DataStream.DataOffset);
    VAFS_DEBUG("__write_vafs_header: data index offset: %u\n", vafs->Header.DataStream.IndexOffset);

    vafs->Header.RootDescriptor.Index = vafs->RootDirectory->DescriptorPosition.Index;
    vafs->Header.RootDescriptor.Offset = vafs->RootDirectory->DescriptorPosition.Offset;
    VAFS_DEBUG("__write_vafs_header: root descriptor index: %i\n", vafs->Header.RootDescriptor.Index);
    VAFS_DEBUG("__write_vafs_header: root descriptor offset: %i\n", vafs->Header.RootDescriptor.Offset);

    return vafs_streamdevice_write(vafs->ImageDevice, &vafs->Header, sizeof(VaFsHeader_t), &written);
}

static int __create_image(
    struct VaFs* vafs)
{
    int status;

    // Hot descriptors only store xattr indices, so the writer has to assign the
    // final deduplicated section order before any directory payload is flushed.
    status = __vafs_xattr_prepare_write(vafs);
    if (status) {
        VAFS_ERROR("Failed to prepare xattr section: %i\n", status);
        return -1;
    }

    // flush files
    VAFS_DEBUG("__create_image: flushing files\n");
    status = vafs_directory_flush(vafs->RootDirectory);
    if (status) {
        VAFS_ERROR("Failed to flush files: %i\n", status);
        return -1;
    }

    // Root metadata is written after the child list because only then does the
    // root descriptor know the final location of that list, and before the cold
    // xattr section so every hot descriptor stays in one contiguous descriptor region.
    status = vafs_directory_write_root_descriptor(vafs->RootDirectory);
    if (status) {
        VAFS_ERROR("Failed to write root descriptor: %i\n", status);
        return -1;
    }

    // Xattr sets live outside the hot directory payloads so directory readers
    // never pay to walk them during ordinary lookup or stat traversal.
    status = __vafs_xattr_write_section(vafs);
    if (status) {
        VAFS_ERROR("Failed to write xattr section: %i\n", status);
        return -1;
    }

    // flush streams
    VAFS_DEBUG("__create_image: flushing streams\n");
    status = vafs_stream_finish(vafs->DescriptorStream, &vafs->Header.DescriptorStream);
    if (status) {
        VAFS_ERROR("Failed to flush descriptor stream: %i\n", status);
        return -1;
    }
    
    status = vafs_stream_finish(vafs->DataStream, &vafs->Header.DataStream);
    if (status) {
        VAFS_ERROR("Failed to flush data stream: %i\n", status);
        return -1;
    }

    // install the overview
    VAFS_DEBUG("__create_image: writing overview\n");
    status = vafs_builder_add_feature(vafs, &vafs->Overview.Header);
    if (status) {
        return -1;
    }

    // write the header
    VAFS_DEBUG("__create_image: writing header\n");
    status = __write_vafs_header(vafs);
    if (status) {
        return -1;
    }

    // write the features
    VAFS_DEBUG("__create_image: writing features\n");
    status = __write_vafs_features(vafs);
    if (status) {
        return -1;
    }

    // write the descriptor stream
    VAFS_DEBUG("__create_image: writing descriptor stream\n");
    status = vafs_streamdevice_copy(vafs->ImageDevice, vafs->DescriptorDevice);
    if (status) {
        return -1;
    }

    // write the data stream
    VAFS_DEBUG("__create_image: writing data stream\n");
    return vafs_streamdevice_copy(vafs->ImageDevice, vafs->DataDevice);
}

static void vafs_destroy(
    struct VaFs* vafs)
{
    VAFS_INFO("vafs_builder_close: cleaning up\n");

    // close all open streams
    vafs_stream_close(vafs->DescriptorStream);
    vafs_stream_close(vafs->DataStream);

    // close all the stream devices active
    vafs_streamdevice_close(vafs->DescriptorDevice);
    vafs_streamdevice_close(vafs->DataDevice);
    vafs_streamdevice_close(vafs->ImageDevice);

    // cleanup features
    for (int i = 0; i < vafs->FeatureCount; i++) {
        free(vafs->Features[i]);
    }
    free(vafs->Features);

    // cleanup directory instances
    vafs_directory_destroy(vafs->RootDirectory);
    __vafs_xattr_store_destroy(vafs);
    
    // cleanup the base instance
    free(vafs);
}

int vafs_builder_close(
    struct VaFs* vafs)
{
    int status;

    if (vafs == NULL) {
        errno = EINVAL;
        return -1;
    }

    VAFS_INFO("vafs_builder_close: building image file\n");
    status = __create_image(vafs);
    vafs_destroy(vafs);
    return status;
}

int vafs_builder_add_feature(
    struct VaFs*              vafs,
    struct VaFsFeatureHeader* feature)
{
    if (vafs == NULL || feature == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (int i = 0; i < vafs->FeatureCount; i++) {
        if (!__compare_guids(&vafs->Features[i]->Guid, &feature->Guid)) {
            errno = EEXIST;
            return -1;
        }
    }

    vafs->Features[vafs->FeatureCount] = malloc(feature->Length);
    if (!vafs->Features[vafs->FeatureCount]) {
        errno = ENOMEM;
        return -1;
    }

    memcpy(vafs->Features[vafs->FeatureCount++], feature, feature->Length);
    return 0;
}
