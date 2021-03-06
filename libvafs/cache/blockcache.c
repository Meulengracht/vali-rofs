/**
 * Copyright 2022, Philip Meulengracht
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

#include <errno.h>
#include "blockcache.h"
#include "hashtable.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static uint64_t __cache_hash(const void* element);
static int      __cache_cmp(const void* lh, const void* rh);
static void     __cache_enum(int index, const void* element, void* userContext);
static void     __cache_enum_free(int index, const void* element, void* userContext);

struct vafs_block {
    uint32_t index;
    void*    buffer;
    size_t   size;
    int      uses;
};

struct VaFsBlockCache {
    int          max_blocks;
    hashtable_t* cache;
};

struct cache_enum_context {
    uint32_t index;
    int      uses;
};

struct VaFsBlockCache* __block_cache_new()
{
    struct VaFsBlockCache* cache;
    int                      status;
    
    cache = malloc(sizeof(struct VaFsBlockCache));
    if (!cache) {
        errno = ENOMEM;
        return NULL;
    }

    cache->cache = malloc(sizeof(hashtable_t));
    if (!cache->cache) {
        free(cache);
        errno = ENOMEM;
        return NULL;
    }
    
    status = vafs_hashtable_construct(
        cache->cache, 0, sizeof(struct vafs_block), 
        __cache_hash, __cache_cmp
    );
    if (status != 0) {
        free(cache);
        return NULL;
    }
    return cache;
}

int vafs_cache_create(int maxBlocks, struct VaFsBlockCache** cacheOut)
{
    struct VaFsBlockCache* cache;

    if (cacheOut == NULL || maxBlocks < 0) {
        errno = EINVAL;
        return -1;
    }

    cache = __block_cache_new();
    if (cache == NULL) {
        return -1;
    }

    cache->max_blocks = maxBlocks;

    *cacheOut = cache;
    return 0;
}

void vafs_cache_destroy(struct VaFsBlockCache* cache)
{
    if (!cache) {
        return;
    }
    
    vafs_hashtable_enumerate(cache->cache, __cache_enum_free, NULL);
    vafs_hashtable_destroy(cache->cache);
    free(cache);
}

int vafs_cache_get(struct VaFsBlockCache* cache, uint32_t index, void** bufferOut, size_t* sizeOut)
{
    struct vafs_block* block;

    if (!cache || !bufferOut || !sizeOut) {
        errno = EINVAL;
        return -1;
    }

    block = vafs_hashtable_get(cache->cache, &(struct vafs_block){ .index = index });
    if (!block) {
        errno = ENOENT;
        return -1;
    }

    block->uses++;
    *bufferOut = block->buffer;
    *sizeOut = block->size;
    return 0;
}

static void __eject_lowuse(struct VaFsBlockCache* cache)
{
    struct cache_enum_context context = { .index = UINT_MAX, .uses = INT_MAX };
    struct vafs_block*        block;

    if (cache->cache->element_count < cache->max_blocks) {
        return;
    }

    vafs_hashtable_enumerate(cache->cache, __cache_enum, &context);
    if (context.index == UINT_MAX) {
        return; // what?
    }
    
    block = vafs_hashtable_remove(cache->cache, &(struct vafs_block){ .index = context.index });
    if (!block) {
        return;
    }

    free(block->buffer);
}

static void* __memdup(const void* src, size_t size)
{
    void* dst;

    dst = malloc(size);
    if (!dst) {
        errno = ENOMEM;
        return NULL;
    }

    memcpy(dst, src, size);
    return dst;
}

int vafs_cache_set(struct VaFsBlockCache* cache, uint32_t index, void* buffer, size_t size)
{
    struct vafs_block* block;

    if (!cache || !buffer) {
        errno = EINVAL;
        return -1;
    }

    block = vafs_hashtable_get(cache->cache, &(struct vafs_block){ .index = index });
    if (!block) {
        // detect cases where we would like to eject an existing low-use
        // block
        __eject_lowuse(cache);

        vafs_hashtable_set(cache->cache, &(struct vafs_block){ 
            .index  = index,
            .buffer = __memdup(buffer, size),
            .size   = size,
            .uses   = 1
        });
    }
    else {
        errno = EEXIST;
        return -1;
    }

    return 0;
}

uint64_t __cache_hash(const void* element)
{
    const struct vafs_block* block = element;
    return block->index;
}

int __cache_cmp(const void* lh, const void* rh)
{
    const struct vafs_block* lblock = lh;
    const struct vafs_block* rblock = rh;
    return lblock->index == rblock->index;
}

void __cache_enum(int index, const void* element, void* userContext)
{
    const struct vafs_block*   block   = element;
    struct cache_enum_context* context = userContext;
    
    if (block->uses < context->uses) {
        context->index = block->index;
        context->uses  = block->uses;
    }
}

void __cache_enum_free(int index, const void* element, void* userContext)
{
    const struct vafs_block* block = element;
    free(block->buffer);
}
