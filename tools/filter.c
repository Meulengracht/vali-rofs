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
 * VaFs Builder
 * - Contains the implementation of the VaFs.
 *   This filesystem is used to store the initrd of the kernel.
 */

#include <errno.h>
#include <vafs/vafs.h>
#include <vafs/builder.h>
#include <vafs/reader.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static struct VaFsGuid g_filterGuid    = VA_FS_FEATURE_FILTER;
static struct VaFsCodec g_supportedCodecs[2];
static int              g_supportedCodecCount;

#if defined(__VAFS_FILTER_APLIB)
#include <aplib.h>
#ifndef CB_CALLCONV
# if defined(AP_DLL)
#  define CB_CALLCONV __stdcall
# elif defined(__GNUC__)
#  define CB_CALLCONV
# else
#  define CB_CALLCONV __cdecl
# endif
#endif

static int CB_CALLCONV callback(unsigned int insize, unsigned int inpos,
	unsigned int outpos, void *cbparam)
{
	(void)insize;
	(void)inpos;
	(void)outpos;
	(void)cbparam;
	return 1;
}

static int __aplib_encode(const void* Input, size_t InputLength, void** Output, size_t* OutputLength)
{
    void*    compressed;
    uint32_t compressedSize;
    void*    workmemory;

    compressed = malloc(aP_max_packed_size(InputLength));
    if (!compressed) {
        errno = ENOMEM;
        return -1;
    }

    workmemory = malloc(aP_workmem_size(InputLength));
    if (!workmemory) {
        free(compressed);
        errno = ENOMEM;
        return -1;
    }

    compressedSize = aPsafe_pack(Input, compressed, InputLength, workmemory, callback, NULL);
    if (compressedSize == APLIB_ERROR) {
        free(compressed);
        free(workmemory);
        errno = EINVAL;
        return -1;
    }
    free(workmemory);

    *Output = compressed;
    *OutputLength = compressedSize;
    return 0;
}

static int __aplib_decode(const void* Input, size_t InputLength, void* Output, size_t OutputLength, size_t* BytesWrittenOut)
{
    uint32_t decompressedSize;

    decompressedSize = aPsafe_get_orig_size(Input);
    if (decompressedSize == APLIB_ERROR) {
        errno = EINVAL;
        return -1;
    }

    if (decompressedSize > OutputLength) {
        errno = ENOSPC;
        return -1;
    }

    decompressedSize = aPsafe_depack(Input, InputLength, Output, decompressedSize);
    *BytesWrittenOut = decompressedSize;
    return 0;
}
#endif

#if defined(__VAFS_FILTER_BRIEFLZ)
#include <brieflz.h>
#ifndef CB_CALLCONV
# if defined(AP_DLL)
#  define CB_CALLCONV __stdcall
# elif defined(__GNUC__)
#  define CB_CALLCONV
# else
#  define CB_CALLCONV __cdecl
# endif
#endif

static int __brieflz_encode(const void* source, size_t sourceLength, void** output, size_t* outputLength)
{
    uint8_t*  buffer;
    uint32_t  compressedSize;
    uint64_t  uncompressedSize = sourceLength;
    void*     workmemory = NULL;

    buffer = malloc(blz_max_packed_size(sourceLength) + sizeof(uint64_t));
    if (buffer == NULL) {
        errno = ENOMEM;
        goto error;
    }

    workmemory = malloc(blz_workmem_size_level(sourceLength, 9));
    if (workmemory == NULL) {
        errno = ENOMEM;
        goto error;
    }

    compressedSize = blz_pack_level(source, buffer + sizeof(uint64_t), sourceLength, workmemory, 9);
    if (compressedSize == BLZ_ERROR) {
        errno = EINVAL;
        goto error;
    }
    free(workmemory);

    // Persist the logical size ahead of the compressed payload so the decoder
    // can size its destination buffer before calling into BriefLZ.
    memcpy(buffer, &uncompressedSize, sizeof(uint64_t));

    *output = buffer;
    *outputLength = compressedSize + sizeof(uint64_t);
    return 0;

error:
    free(buffer);
    free(workmemory);
    return -1;
}

static int __brieflz_decode(const void* source, size_t sourceLength, void* output, size_t outputLength, size_t* bytesWrittenOut)
{
    uint64_t decompressedSize;
    const uint8_t* bytes = source;

    if (sourceLength < sizeof(uint64_t)) {
        errno = EINVAL;
        return -1;
    }

    memcpy(&decompressedSize, bytes, sizeof(uint64_t));

    if (decompressedSize > outputLength) {
        errno = ENOSPC;
        return -1;
    }

    decompressedSize = blz_depack_safe(
        bytes + sizeof(uint64_t),
        sourceLength - (uint32_t)sizeof(uint64_t),
        output,
        (unsigned long)decompressedSize
    );
    if (decompressedSize == BLZ_ERROR) {
        errno = EINVAL;
        return -1;
    }

    *bytesWrittenOut = decompressedSize;
    return 0;
}
#endif

static int __codec_from_name(
    const char*         filterName,
    struct VaFsCodec*   codecOut)
{
    if (filterName == NULL || !strcmp(filterName, "none")) {
        memset(codecOut, 0, sizeof(*codecOut));
        return 0;
    }

#if defined(__VAFS_FILTER_APLIB)
    if (!strcmp(filterName, "aplib")) {
        *codecOut = (struct VaFsCodec) {
            .ID = "aplib",
            .Encode = __aplib_encode,
            .Decode = __aplib_decode
        };
        return 0;
    }
#endif
#if defined(__VAFS_FILTER_BRIEFLZ)
    if (!strcmp(filterName, "brieflz")) {
        *codecOut = (struct VaFsCodec) {
            .ID = "brieflz",
            .Encode = __brieflz_encode,
            .Decode = __brieflz_decode
        };
        return 0;
    }
#endif

    fprintf(stderr, "unsupported filter type %s\n", filterName);
    return -1;
}

int __handle_filter(
    struct VaFs* vafs)
{
    (void)vafs;
    return 0;
}

int __configure_reader_filters(struct VaFsReaderConfiguration* configuration)
{
    int count = 0;

#if defined(__VAFS_FILTER_APLIB)
    g_supportedCodecs[count++] = (struct VaFsCodec) {
        .ID = "aplib",
        .Encode = __aplib_encode,
        .Decode = __aplib_decode
    };
#endif
#if defined(__VAFS_FILTER_BRIEFLZ)
    g_supportedCodecs[count++] = (struct VaFsCodec) {
        .ID = "brieflz",
        .Encode = __brieflz_encode,
        .Decode = __brieflz_decode
    };
#endif

    g_supportedCodecCount = count;
    vafs_reader_config_initialize(configuration);
    vafs_reader_config_set_codecs(configuration, g_supportedCodecs, g_supportedCodecCount);
    return 0;
}

int __configure_filters(
    struct VaFsBuilderConfiguration* configuration,
    const char*               descriptorFilterName,
    const char*               dataFilterName)
{
    struct VaFsCodec descriptorCodec;
    struct VaFsCodec dataCodec;

    if (__codec_from_name(descriptorFilterName, &descriptorCodec) != 0) {
        return -1;
    }
    if (__codec_from_name(dataFilterName, &dataCodec) != 0) {
        return -1;
    }

    configuration->Codecs[0] = descriptorCodec;
    configuration->Codecs[1] = dataCodec;
    return 0;
}

int __install_filters(
    struct VaFs* vafs,
    const char*  descriptorFilterName,
    const char*  dataFilterName)
{
    struct VaFsFeatureEncoding filter;
    struct VaFsCodec          descriptorCodec;
    struct VaFsCodec          dataCodec;
    int                       status;

    // Resolve both stream policies up front so persisted metadata and runtime
    // callbacks stay aligned.

    memset(&filter, 0, sizeof(filter));
    memcpy(&filter.Header.Guid, &g_filterGuid, sizeof(struct VaFsGuid));
    filter.Header.Length = sizeof(struct VaFsFeatureEncoding);

    status = __codec_from_name(descriptorFilterName, &descriptorCodec);
    if (status != 0) {
        return -1;
    }

    status = __codec_from_name(dataFilterName, &dataCodec);
    if (status != 0) {
        return -1;
    }

    if (descriptorCodec.ID == NULL && dataCodec.ID == NULL) {
        // Skip feature emission entirely when neither stream requests a filter.
        return 0;
    }

    if (descriptorCodec.ID != NULL) {
        strncpy(filter.DescriptorEncoding, descriptorCodec.ID, sizeof(filter.DescriptorEncoding) - 1);
    }
    if (dataCodec.ID != NULL) {
        strncpy(filter.DataEncoding, dataCodec.ID, sizeof(filter.DataEncoding) - 1);
    }

    status = vafs_builder_add_feature(vafs, &filter.Header);
    if (status) {
        // Stop here if the persisted policy could not be recorded so we do not
        // install runtime callbacks that the image metadata does not describe.
        return 0;
    }
    return 0;
}

int __install_filter(
    struct VaFs* vafs,
    const char*  filterName)
{
    return __install_filters(vafs, filterName, filterName);
}
