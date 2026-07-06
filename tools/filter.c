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
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static struct VaFsGuid g_filterGuid    = VA_FS_FEATURE_FILTER;
static struct VaFsGuid g_filterOpsGuid = VA_FS_FEATURE_FILTER_OPS;

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

static int __aplib_encode(void* Input, uint32_t InputLength, void** Output, uint32_t* OutputLength)
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

static int __aplib_decode(void* Input, uint32_t InputLength, void* Output, uint32_t* OutputLength)
{
    uint32_t decompressedSize;

    decompressedSize = aPsafe_get_orig_size(Input);
    if (decompressedSize == APLIB_ERROR) {
        errno = EINVAL;
        return -1;
    }

    if (decompressedSize > *OutputLength) {
        errno = ENOSPC;
        return -1;
    }

    decompressedSize = aPsafe_depack(Input, InputLength, Output, decompressedSize);
    *OutputLength = decompressedSize;
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

static int __brieflz_encode(void* source, uint32_t sourceLength, void** output, uint32_t* outputLength)
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
    *outputLength = compressedSize + (uint32_t)sizeof(uint64_t);
    return 0;

error:
    free(buffer);
    free(workmemory);
    return -1;
}

static int __brieflz_decode(void* source, uint32_t sourceLength, void* output, uint32_t* outputLength)
{
    uint64_t decompressedSize;
    uint8_t* bytes = source;

    if (sourceLength < sizeof(uint64_t)) {
        errno = EINVAL;
        return -1;
    }

    memcpy(&decompressedSize, bytes, sizeof(uint64_t));

    if (decompressedSize > *outputLength) {
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

    *outputLength = (uint32_t)decompressedSize;
    return 0;
}
#endif

static int __set_filter_ops(
    struct VaFs*              vafs,
    struct VaFsFeatureEncoding* filter)
{
    struct VaFsFeatureEncodingOps filterOps;

    // Resolve descriptor and data filters independently so metadata can keep a
    // cheaper policy without forcing the same codec on file data.

    memcpy(&filterOps.Header.Guid, &g_filterOpsGuid, sizeof(struct VaFsGuid));
    filterOps.Header.Length = sizeof(struct VaFsFeatureEncodingOps);

    filterOps.DescriptorEncode = NULL;
    filterOps.DescriptorDecode = NULL;
    switch (filter->DescriptorType) {
        case VaFsFilterType_None:
            break;
#if defined(__VAFS_FILTER_APLIB)
        case VaFsFilterType_APLIB: {
            filterOps.DescriptorEncode = __aplib_encode;
            filterOps.DescriptorDecode = __aplib_decode;
        } break;
#endif
#if defined(__VAFS_FILTER_BRIEFLZ)
        case VaFsFilterType_BRIEFLZ: {
            filterOps.DescriptorEncode = __brieflz_encode;
            filterOps.DescriptorDecode = __brieflz_decode;
        } break;
#endif
        default: {
            fprintf(stderr, "unsupported descriptor filter type %u\n", filter->DescriptorType);
            return -1;
        }
    }

    filterOps.DataEncode = NULL;
    filterOps.DataDecode = NULL;
    switch (filter->DataType) {
        case VaFsFilterType_None:
            break;
#if defined(__VAFS_FILTER_APLIB)
        case VaFsFilterType_APLIB: {
            filterOps.DataEncode = __aplib_encode;
            filterOps.DataDecode = __aplib_decode;
        } break;
#endif
#if defined(__VAFS_FILTER_BRIEFLZ)
        case VaFsFilterType_BRIEFLZ: {
            filterOps.DataEncode = __brieflz_encode;
            filterOps.DataDecode = __brieflz_decode;
        } break;
#endif
        default: {
            fprintf(stderr, "unsupported data filter type %u\n", filter->DataType);
            return -1;
        }
    }

    return vafs_feature_add(vafs, &filterOps.Header);
}

int __handle_filter(
    struct VaFs* vafs)
{
    struct VaFsFeatureEncoding* filter;
    int                       status;

    // Opening an image only installs runtime callbacks when persisted filter
    // metadata exists and is large enough for the split-stream layout.

    status = vafs_feature_query(vafs, &g_filterGuid, (struct VaFsFeatureHeader**)&filter);
    if (status) {
        // Images without a filter feature leave both streams unfiltered.
        return 0;
    }

    if (filter->Header.Length < sizeof(struct VaFsFeatureEncoding)) {
        // Reject truncated metadata before reading descriptor/data type fields.
        errno = EINVAL;
        return -1;
    }
    return __set_filter_ops(vafs, filter);
}

static int __get_filter_from_name(
    const char* filterName,
    uint32_t*   typeOut)
{
    // Treat NULL the same as "none" so callers can configure just one stream
    // and leave the other raw.
    if (filterName == NULL || !strcmp(filterName, "none")) {
        *typeOut = VaFsFilterType_None;
        return 0;
    }

#if defined(__VAFS_FILTER_APLIB)
    if (!strcmp(filterName, "aplib")) {
        *typeOut = VaFsFilterType_APLIB;
        return 0;
    }
#endif
#if defined(__VAFS_FILTER_BRIEFLZ)
    if (!strcmp(filterName, "brieflz")) {
        *typeOut = VaFsFilterType_BRIEFLZ;
        return 0;
    }
#endif

    fprintf(stderr, "unsupported filter type %s\n", filterName);
    return -1;
}

int __install_filters(
    struct VaFs* vafs,
    const char*  descriptorFilterName,
    const char*  dataFilterName)
{
    struct VaFsFeatureEncoding filter;
    int                      status;

    // Resolve both stream policies up front so persisted metadata and runtime
    // callbacks stay aligned.

    memcpy(&filter.Header.Guid, &g_filterGuid, sizeof(struct VaFsGuid));
    filter.Header.Length = sizeof(struct VaFsFeatureEncoding);

    status = __get_filter_from_name(descriptorFilterName, &filter.DescriptorType);
    if (status != 0) {
        return -1;
    }

    status = __get_filter_from_name(dataFilterName, &filter.DataType);
    if (status != 0) {
        return -1;
    }

    if (filter.DescriptorType == VaFsFilterType_None && filter.DataType == VaFsFilterType_None) {
        // Skip feature emission entirely when neither stream requests a filter.
        return 0;
    }

    status = vafs_feature_add(vafs, &filter.Header);
    if (status) {
        // Stop here if the persisted policy could not be recorded so we do not
        // install runtime callbacks that the image metadata does not describe.
        return 0;
    }
    return __set_filter_ops(vafs, &filter);
}

int __install_filter(
    struct VaFs* vafs,
    const char*  filterName)
{
    return __install_filters(vafs, filterName, filterName);
}
