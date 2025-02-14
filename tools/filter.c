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

enum VaFsFilterType {
    VaFsFilterType_APLIB,
    VaFsFilterType_BRIEFLZ
};

struct VaFsFeatureFilter {
    struct VaFsFeatureHeader Header;
    int                      Type;
};

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

struct __brieflz_block {
    uint64_t usize;
    char     payload[];
};

static int __brieflz_encode(void* source, uint32_t sourceLength, void** output, uint32_t* outputLength)
{
    struct __brieflz_block* block;
    uint32_t                compressedSize;
    void*                   workmemory = NULL;
    char                    header[16];

    block = malloc(blz_max_packed_size(sourceLength) + sizeof(struct __brieflz_block));
    if (block == NULL) {
        errno = ENOMEM;
        goto error;
    }

    workmemory = malloc(blz_workmem_size_level(sourceLength, 9));
    if (workmemory == NULL) {
        errno = ENOMEM;
        goto error;
    }

    compressedSize = blz_pack_level(source, &block->payload[0], sourceLength, workmemory, 9);
    if (compressedSize == BLZ_ERROR) {
        errno = EINVAL;
        goto error;
    }
    free(workmemory);

    // store the uncompressed size
    block->usize = sourceLength;

    *output = block;
    *outputLength = compressedSize + sizeof(struct __brieflz_block);
    return 0;

error:
    free(block);
    free(workmemory);
    return -1;
}

static int __brieflz_decode(void* source, uint32_t sourceLength, void* output, uint32_t* outputLength)
{
    uint64_t                decompressedSize;
    struct __brieflz_block* block = source;

    if (block->usize > *outputLength) {
        errno = ENOSPC;
        return -1;
    }

    decompressedSize = blz_depack_safe(&block->payload[0], sourceLength, output, block->usize);
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
    struct VaFsFeatureFilter* filter)
{
    struct VaFsFeatureFilterOps filterOps;

    memcpy(&filterOps.Header.Guid, &g_filterOpsGuid, sizeof(struct VaFsGuid));
    filterOps.Header.Length = sizeof(struct VaFsFeatureFilterOps);

    switch (filter->Type) {
#if defined(__VAFS_FILTER_APLIB)
        case VaFsFilterType_APLIB: {
            filterOps.Encode = __aplib_encode;
            filterOps.Decode = __aplib_decode;
        } break;
#endif
#if defined(__VAFS_FILTER_BRIEFLZ)
        case VaFsFilterType_BRIEFLZ: {
            filterOps.Encode = __brieflz_encode;
            filterOps.Decode = __brieflz_decode;
        } break;
#endif
        default: {
            fprintf(stderr, "unsupported filter type %i\n", filter->Type);
            return -1;
        }
    }

    return vafs_feature_add(vafs, &filterOps.Header);
}

int __handle_filter(
    struct VaFs* vafs)
{
    struct VaFsFeatureFilter* filter;
    int                       status;

    status = vafs_feature_query(vafs, &g_filterGuid, (struct VaFsFeatureHeader**)&filter);
    if (status) {
        // no filter present
        return 0;
    }
    return __set_filter_ops(vafs, filter);
}

static enum VaFsFilterType __get_filter_from_name(
    const char* filterName)
{
#if defined(__VAFS_FILTER_APLIB)
    if (!strcmp(filterName, "aplib"))
        return VaFsFilterType_APLIB;
#endif
#if defined(__VAFS_FILTER_BRIEFLZ)
    if (!strcmp(filterName, "brieflz"))
        return VaFsFilterType_BRIEFLZ;
#endif
    return -1;
}

int __install_filter(
    struct VaFs* vafs,
    const char*  filterName)
{
    struct VaFsFeatureFilter filter;
    int                      status;

    memcpy(&filter.Header.Guid, &g_filterGuid, sizeof(struct VaFsGuid));
    filter.Header.Length = sizeof(struct VaFsFeatureFilter);
    filter.Type          = __get_filter_from_name(filterName);
    if (filter.Type == -1) {
        fprintf(stderr, "unsupported filter type %s\n", filterName);
        return -1;
    }

    status = vafs_feature_add(vafs, &filter.Header);
    if (status) {
        // no filter present
        return 0;
    }
    return __set_filter_ops(vafs, &filter);
}
