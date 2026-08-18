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
 * - Declares the draft backend contracts for the next-generation VaFS public API.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/reader.h>

#include "core.h"
#include "../fs/fs.h"
#include "../stream/stream.h"

static inline int __compare_guids(
    const struct VaFsGuid* lh,
    const struct VaFsGuid* rh)
{
    return memcmp(lh, rh, sizeof(struct VaFsGuid));
}

static void vafs_destroy(
    struct VaFs* vafs);

void vafs_reader_config_initialize(struct VaFsReaderConfiguration* configuration)
{
    if (configuration == NULL) {
        return;
    }
    memset(configuration, 0, sizeof(struct VaFsReaderConfiguration));
}

void vafs_reader_config_set_codecs(struct VaFsReaderConfiguration* configuration, struct VaFsCodec* codecs, int count)
{
    if (configuration == NULL) {
        return;
    }
    configuration->SupportedCodecs = codecs;
    configuration->SupportedCodecCount = count;
}

static struct VaFsFeatureHeader* __load_feature(
    struct VaFs* vafs,
    long         offset,
    long*        nextOffsetOut)
{
    struct VaFsFeatureHeader  header;
    struct VaFsFeatureHeader* feature;
    int                       status;
    size_t                    read;

    status = vafs_streamdevice_read_at(vafs->ImageDevice, offset, &header, sizeof(struct VaFsFeatureHeader), &read);
    if (status || read != sizeof(struct VaFsFeatureHeader)) {
        VAFS_ERROR("__load_feature: failed to read feature header %i\n", status);
        return NULL;
    }

    feature = malloc(header.Length);
    if (!feature) {
        VAFS_ERROR("__load_feature: failed to allocate memory\n");
        return NULL;
    }

    status = vafs_streamdevice_read_at(vafs->ImageDevice, offset, feature, header.Length, &read);
    if (status || read != header.Length) {
        VAFS_ERROR("__load_feature: failed to read feature %i\n", status);
        free(feature);
        return NULL;
    }

    *nextOffsetOut = offset + header.Length;
    return feature;
}

static int __load_features(
    struct VaFs* vafs)
{
    long offset;

    if (!vafs->Header.FeatureCount) {
        return 0;
    }

    offset = sizeof(VaFsHeader_t);
    
    for (int i = 0; i < vafs->Header.FeatureCount; i++) {
        vafs->Features[i] = __load_feature(vafs, offset, &offset);
        if (vafs->Features[i] == NULL) {
            return -1;
        }
    }
    vafs->FeatureCount = vafs->Header.FeatureCount;
    return 0;
}

static int __verify_header(
    struct VaFs* vafs)
{
    uint32_t minStreamOffset;

    // Validate only the outer-image invariants here; checks that depend on the
    // descriptor stream's own header run later in __verify_root_descriptor().
    // This split keeps the outer header checks focused on image layout and the
    // nested descriptor validation focused on the metadata stream's actual
    // runtime constraints.

    // Validate magic number
    if (vafs->Header.Magic != VA_FS_MAGIC) {
        VAFS_ERROR("__verify_header: invalid image magic 0x%x\n", vafs->Header.Magic);
        errno = EINVAL;
        return -1;
    }

    // Validate version
    if (vafs->Header.Version != VA_FS_VERSION) {
        VAFS_ERROR("__verify_header: invalid image version 0x%x\n", vafs->Header.Version);
        errno = EINVAL;
        return -1;
    }

    // Validate feature count bounds
    if (vafs->Header.FeatureCount > VA_FS_MAX_FEATURES) {
        VAFS_ERROR("__verify_header: feature count %u exceeds maximum %u\n",
                   vafs->Header.FeatureCount, VA_FS_MAX_FEATURES);
        errno = EINVAL;
        return -1;
    }

    // Validate reserved field (must be zero for format stability)
    if (vafs->Header.Reserved != 0) {
        VAFS_ERROR("__verify_header: reserved field must be zero, got 0x%x\n",
                   vafs->Header.Reserved);
        errno = EINVAL;
        return -1;
    }

    // Validate stream placement. The outer header owns stream layouts, so the
    // descriptor stream must start after the header and feature records, and
    // the data stream must follow the descriptor stream region.
    minStreamOffset = sizeof(VaFsHeader_t);
    if (vafs->Header.DescriptorStream.DataOffset < minStreamOffset) {
        VAFS_ERROR("__verify_header: descriptor stream offset %u is before end of header (min %u)\n",
                   vafs->Header.DescriptorStream.DataOffset, minStreamOffset);
        errno = EINVAL;
        return -1;
    }

    if (vafs->Header.DescriptorStream.IndexOffset < vafs->Header.DescriptorStream.DataOffset ||
        vafs->Header.DataStream.IndexOffset < vafs->Header.DataStream.DataOffset) {
        VAFS_ERROR("__verify_header: stream index offsets must not precede stream data offsets\n");
        errno = EINVAL;
        return -1;
    }

    if (vafs->Header.DataStream.DataOffset <= vafs->Header.DescriptorStream.DataOffset) {
        VAFS_ERROR("__verify_header: data stream offset %u must be after descriptor stream offset %u\n",
                   vafs->Header.DataStream.DataOffset, vafs->Header.DescriptorStream.DataOffset);
        errno = EINVAL;
        return -1;
    }

    if (vafs->Header.DescriptorStream.BlockSize < VA_FS_DATA_MIN_BLOCKSIZE ||
        vafs->Header.DescriptorStream.BlockSize > VA_FS_DATA_MAX_BLOCKSIZE ||
        vafs->Header.DataStream.BlockSize < VA_FS_DATA_MIN_BLOCKSIZE ||
        vafs->Header.DataStream.BlockSize > VA_FS_DATA_MAX_BLOCKSIZE) {
        VAFS_ERROR("__verify_header: stream block size out of supported range\n");
        errno = EINVAL;
        return -1;
    }

    if (vafs->Header.DescriptorStream.Reserved != 0 || vafs->Header.DataStream.Reserved != 0) {
        VAFS_ERROR("__verify_header: stream layout reserved fields must be zero\n");
        errno = EINVAL;
        return -1;
    }

    // Validate root descriptor has reasonable values
    // A valid block index should not be the invalid block marker
    if (vafs->Header.RootDescriptor.Index == VA_FS_INVALID_BLOCK) {
        VAFS_ERROR("__verify_header: root descriptor has invalid block index\n");
        errno = EINVAL;
        return -1;
    }

    // Root descriptor offset should not be invalid offset marker
    if (vafs->Header.RootDescriptor.Offset == VA_FS_INVALID_OFFSET) {
        VAFS_ERROR("__verify_header: root descriptor has invalid offset\n");
        errno = EINVAL;
        return -1;
    }

    return 0;
}

static int __verify_root_descriptor(
    struct VaFs* vafs)
{
    uint32_t descriptorBlockSize;

    // Validate the root descriptor against the descriptor stream's real block
    // size because metadata and file data can now use different block sizes.
    if (vafs == NULL || vafs->DescriptorStream == NULL) {
        errno = EINVAL;
        return -1;
    }

    descriptorBlockSize = vafs_stream_block_size(vafs->DescriptorStream);
    if (descriptorBlockSize == 0) {
        errno = EINVAL;
        return -1;
    }

    if (vafs->Header.RootDescriptor.Offset >= descriptorBlockSize) {
        VAFS_ERROR("__verify_root_descriptor: root descriptor offset %u exceeds descriptor block size %u\n",
                   vafs->Header.RootDescriptor.Offset, descriptorBlockSize);
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int __initialize_imagestream(
    struct VaFs*             vafs,
    struct VaFsStreamDevice* imageDevice)
{
    size_t read;
    int    status = 0;

    VAFS_DEBUG("__initialize_imagestream()\n");

    // Read and validate the existing outer image header plus feature list.
    // The same entry point is used for both opening images and creating a new
    // one, but the read path must reject any malformed outer header before we
    // proceed to descriptor-stream initialization or root traversal.
    vafs->ImageDevice = imageDevice;

    status = vafs_streamdevice_read_at(vafs->ImageDevice, 0, &vafs->Header, sizeof(VaFsHeader_t), &read);
    if (status || read != sizeof(VaFsHeader_t)) {
        VAFS_ERROR("__initialize_imagestream: failed to read image header: %i\n", status);
        return status;
    }

    status = __verify_header(vafs);
    if (status) {
        VAFS_ERROR("__initialize_imagestream: failed to verify image header: %i\n", status);
        return status;
    }

    status = __load_features(vafs);
    if (status) {
        VAFS_ERROR("__initialize_imagestream: failed to load features: %i\n", status);
    }
    return status;
}

static int __initialize_fsstreams_read(struct VaFs* vafs)
{
    int status;
    
    VAFS_DEBUG("__initialize_fsstreams_read: vafs: %p\n", vafs);
    // Open both inner streams first; persisted features decide afterward
    // whether either stream needs runtime filter callbacks.
    status = vafs_stream_open(
        vafs->ImageDevice, 
        &vafs->Header.DescriptorStream,
        &vafs->DescriptorStream
    );
    if (status) {
        VAFS_ERROR("__initialize_fsstreams_read: failed to create descriptor stream: %i\n", status);
        return status;
    }

    status = vafs_stream_open(
        vafs->ImageDevice, 
        &vafs->Header.DataStream,
        &vafs->DataStream
    );
    return status;
}

static int __default_fail_encode(const void* Input, size_t InputLength, void** Output, size_t* OutputLength)
{
    (void)Input;
    (void)InputLength;
    (void)Output;
    (void)OutputLength;
    VAFS_ERROR("__default_fail_encode: encode handler not installed\n");
    errno = ENOTSUP;
    return -1;
}

static int __default_fail_decode(const void* Input, size_t InputLength, void* Output, size_t OutputLength, size_t* BytesWrittenOut)
{
    (void)Input;
    (void)InputLength;
    (void)Output;
    (void)OutputLength;
    (void)BytesWrittenOut;
    VAFS_ERROR("__default_fail_decode: decode handler not installed\n");
    errno = ENOTSUP;
    return -1;
}

static struct VaFsCodec* __find_codec(
    struct VaFsReaderConfiguration* configuration,
    const char*                     id)
{
    if (configuration == NULL || id == NULL) {
        return NULL;
    }

    for (int i = 0; i < configuration->SupportedCodecCount; i++) {
        if (strcmp(configuration->SupportedCodecs[i].ID, id) == 0) {
            return &configuration->SupportedCodecs[i];
        }
    }
    return NULL;
}

static int __install_encoding_handlers(
    struct VaFs*                    vafs,
    struct VaFsReaderConfiguration* configuration)
{
    for (int i = 0; i < vafs->Header.FeatureCount; i++) {
        if (!__compare_guids(&vafs->Features[i]->Guid, &g_filterGuid)) {
            struct VaFsFeatureEncoding* filter = (struct VaFsFeatureEncoding*)vafs->Features[i];
            struct VaFsCodec*           codec;

            if (filter->Header.Length < sizeof(struct VaFsFeatureEncoding)) {
                VAFS_ERROR("__install_encoding_handlers: filter feature length %u smaller than expected %zu\n",
                    filter->Header.Length, sizeof(struct VaFsFeatureEncoding));
                errno = EINVAL;
                return -1;
            }

            if (filter->DescriptorEncoding[0] != '\0') {
                codec = __find_codec(configuration, &filter->DescriptorEncoding[0]);
                if (codec == NULL) {
                    VAFS_ERROR("__install_encoding_handlers: no codec found for filter id '%s'\n", &filter->DescriptorEncoding[0]);
                    errno = ENOTSUP;
                    return -1;
                }
                vafs_stream_set_filter(vafs->DescriptorStream, codec->Encode, codec->Decode);
            }

            if (filter->DataEncoding[0] != '\0') {
                codec = __find_codec(configuration, &filter->DataEncoding[0]);
                if (codec == NULL) {
                    VAFS_ERROR("__install_encoding_handlers: no codec found for filter id '%s'\n", &filter->DataEncoding[0]);
                    errno = ENOTSUP;
                    return -1;
                }
                vafs_stream_set_filter(vafs->DataStream, codec->Encode, codec->Decode);
            }
        }
    }

    return 0;
}

static int __open_vafs(
    struct VaFsStreamDevice*        imageDevice,
    struct VaFsReaderConfiguration* configuration,
    struct VaFs**                   vafsOut)
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

    vafs->Mode = VaFsMode_Read;

    vafs->LookupCache = malloc(sizeof(struct VaFsLookupCache));
    if (!vafs->LookupCache) {
        vafs_destroy(vafs);
        errno = ENOMEM;
        return -1;
    }

    vafs->Features = malloc(sizeof(struct VaFsFeatureHeader*) * VA_FS_MAX_FEATURES);
    if (!vafs->Features) {
        vafs_destroy(vafs);
        errno = ENOMEM;
        return -1;
    }

    // try to create the output file, otherwise do not continue
    status = __initialize_imagestream(vafs, imageDevice);
    if (status) {
        VAFS_ERROR("__open_vafs: failed to initialize image stream: %i\n", status);
        vafs_destroy(vafs);
        return -1;
    }

    // open the desc/data streams valid
    status = __initialize_fsstreams_read(vafs);
    if (status) {
        VAFS_ERROR("__open_vafs: failed to initialize filesystem streams: %i\n", status);
        vafs_destroy(vafs);
        return -1;
    }

    // install the required encoding handlers
    status = __install_encoding_handlers(vafs, configuration);
    if (status) {
        VAFS_ERROR("__open_vafs: failed to parse known features: %i\n", status);
        vafs_destroy(vafs);
        return status;
    }

    // Root descriptor offsets depend on the descriptor stream header, so
    // validate them only after the descriptor stream has been opened.
    status = __verify_root_descriptor(vafs);
    if (status) {
        VAFS_ERROR("__open_vafs: failed to validate root descriptor: %i\n", status);
        vafs_destroy(vafs);
        return -1;
    }

    status = vafs_directory_open_root(vafs, &vafs->Header.RootDescriptor, &vafs->RootDirectory);
    if (status) {
        VAFS_ERROR("__open_vafs: failed to open root directory: %i\n", status);
        vafs_destroy(vafs);
        return -1;
    }

    *vafsOut = vafs;
    return 0;
}

int vafs_reader_open_file(
    const char*                     path,
    struct VaFsReaderConfiguration* configuration,
    struct VaFs**                   vafsOut)
{
    struct VaFsStreamDevice* imageDevice;
    int                      status;
    VAFS_INFO("vafs_reader_open_file: opening existing image file\n");

    status = vafs_streamdevice_open_file(path, &imageDevice);
    if (status) {
        VAFS_ERROR("vafs_reader_open_file: failed to open image file: %i\n", status);
        return status;
    }
    return __open_vafs(imageDevice, configuration, vafsOut);
}

int vafs_reader_open_memory(
        const void*                     buffer,
        size_t                          size,
        struct VaFsReaderConfiguration* configuration,
        struct VaFs**                   vafsOut)
{
    struct VaFsStreamDevice* imageDevice;
    int                      status;
    VAFS_INFO("vafs_open_memory: parsing image buffer\n");

    status = vafs_streamdevice_open_memory(buffer, size, &imageDevice);
    if (status) {
        VAFS_ERROR("vafs_open_memory: failed to parse image buffer: %i\n", status);
        return status;
    }
    return __open_vafs(imageDevice, configuration, vafsOut);
}

int vafs_reader_open_ops(
        struct VaFsReaderBackendOps*    operations,
        void*                           userData,
        struct VaFsReaderConfiguration* configuration,
        struct VaFs**                   vafsOut)
{
    struct VaFsStreamDevice* imageDevice;
    int                      status;
    VAFS_INFO("vafs_reader_open_ops: parsing image buffer\n");

    status = vafs_streamdevice_reader_new(operations, userData, &imageDevice);
    if (status) {
        VAFS_ERROR("vafs_reader_open_ops: failed to parse image buffer: %i\n", status);
        return status;
    }
    return __open_vafs(imageDevice, configuration, vafsOut);
}

static void vafs_destroy(
    struct VaFs* vafs)
{
    VAFS_INFO("vafs_reader_close: cleaning up\n");

    // close all open streams
    vafs_stream_close(vafs->DescriptorStream);
    vafs_stream_close(vafs->DataStream);
    vafs_streamdevice_close(vafs->ImageDevice);

    // cleanup features
    for (int i = 0; i < vafs->FeatureCount; i++) {
        free(vafs->Features[i]);
    }
    free(vafs->Features);
    free(vafs->LookupCache);

    // cleanup directory instances
    vafs_directory_destroy(vafs->RootDirectory);
    __vafs_xattr_store_destroy(vafs);
    
    // cleanup the base instance
    free(vafs);
}

int vafs_reader_close(struct VaFs* vafs)
{
    if (vafs == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    vafs_destroy(vafs);
    return 0;
}

int vafs_reader_query_feature(
    struct VaFs*               vafs,
    struct VaFsGuid*           guid,
    struct VaFsFeatureHeader** featureOut)
{
    int i;

    if (vafs == NULL || guid == NULL || featureOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (i = 0; i < vafs->FeatureCount; i++) {
        if (!__compare_guids(&vafs->Features[i]->Guid, guid)) {
            *featureOut = vafs->Features[i];
            return 0;
        }
    }
    
    errno = ENOENT;
    return -1;
}
