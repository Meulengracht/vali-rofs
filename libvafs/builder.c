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
#include <vafs/builder.h>

#include "private.h"

static inline int __compare_guids(
    struct VaFsGuid* lh,
    struct VaFsGuid* rh)
{
    return memcmp(lh, rh, sizeof(struct VaFsGuid));
}

void vafs_builder_config_initialize(struct VaFsBuilderConfiguration* configuration)
{

}

void vafs_builder_config_set_architecture(struct VaFsBuilderConfiguration* configuration, enum VaFsArchitecture architecture);

void vafs_builder_config_set_codec(struct VaFsBuilderConfiguration* configuration, struct VaFsCodec* codec, int index);

void vafs_builder_config_set_descriptor_block_size(struct VaFsBuilderConfiguration* configuration, uint32_t blockSize);

void vafs_builder_config_set_data_block_size(struct VaFsBuilderConfiguration* configuration, uint32_t blockSize);

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
        0,
        configuration->DescriptorBlockSize,
        &vafs->DescriptorStream
    );
    if (status) {
        VAFS_ERROR("__initialize_fsstreams_write: failed to create descriptor stream: %i\n", status);
        return status;
    }

    status = vafs_stream_create(
        vafs->DataDevice, 
        0,
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

    // Either read and validate the existing outer image header plus feature
    // list, or seed a fresh header for image creation.
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

    if (!g_initialized) {
        // CRC state is process-global, so initialize it lazily once.
        vafs_init();
    }

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

    vafs_stream_set_filter(vafs->DescriptorStream, ops.DescriptorEncode, ops.DescriptorDecode);
    vafs_stream_set_filter(vafs->DataStream, ops.DataEncode, ops.DataDecode);
    
    if (vafs->Mode == VaFsMode_Read) {
        // Root descriptor offsets depend on the descriptor stream header, so
        // validate them only after the descriptor stream has been opened.
        status = __verify_root_descriptor(vafs);
        if (status) {
            VAFS_ERROR("__new_vafs: failed to validate root descriptor: %i\n", status);
            vafs_destroy(vafs);
            return -1;
        }

        // Apply persisted stream policy before touching the root descriptor so
        // filtered descriptor blocks can be materialized during root open.
        status = __parse_known_features(vafs);
        if (status) {
            VAFS_ERROR("__new_vafs: failed to parse known features: %i\n", status);
            vafs_destroy(vafs);
            return status;
        }
    } else {
        status = __initialize_root(vafs);
        if (status) {
            VAFS_ERROR("__new_vafs: failed to initialize root directory: %i\n", status);
            vafs_destroy(vafs);
            return -1;
        }
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
    struct VaFs**                    vafsOut)
{
    struct VaFsStreamDevice* imageDevice;
    int                      status;

    VAFS_INFO("vafs_create: creating new image file\n");

    status = vafs_streamdevice_create_file(path, &imageDevice);
    if (status) {
        VAFS_ERROR("vafs_create: failed to create image file: %i\n", status);
        return status;
    }
    
    status = __new_vafs(VaFsMode_Write, imageDevice, configuration, vafsOut);
    if (status) {
        VAFS_ERROR("vafs_create: failed to create new vafs instance: %i\n", status);
        return status;
    }

    __initialize_overview(*vafsOut);
    return 0;
}

int vafs_builder_close(
    struct VaFs* vafs)
{

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
