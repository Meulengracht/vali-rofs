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

static uint64_t __heatmap_hash(const void* element);
static int      __heatmap_cmp(const void* lh, const void* rh);

struct __block_entry {
    uint32_t index;
    void*    buffer;
    size_t   size;
    int      uses;
};

struct __heatmap_entry {
    uint32_t index;
    int      hits;
};

struct VaFsBlockCache {
    int         max_blocks;
    hashtable_t heatmap;
    hashtable_t cache;
};

struct cache_enum_context {
    uint32_t index;
    int      uses;
};

struct VaFsBlockCache* __block_cache_new(void)
{
    struct VaFsBlockCache* cache;
    int                    status;
    
    cache = malloc(sizeof(struct VaFsBlockCache));
    if (!cache) {
        return NULL;
    }
    memset(cache, 0, sizeof(sizeof(struct VaFsBlockCache)));
    
    status = vafs_hashtable_construct(
        &cache->cache, 0, sizeof(struct __block_entry), 
        __cache_hash, __cache_cmp
    );
    if (status != 0) {
        free(cache);
        return NULL;
    }

    status = vafs_hashtable_construct(
        &cache->heatmap, 0, sizeof(struct __heatmap_entry), 
        __heatmap_hash, __heatmap_cmp
    );
    if (status != 0) {
        vafs_hashtable_destroy(&cache->cache);
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
    
    vafs_hashtable_enumerate(&cache->cache, __cache_enum_free, NULL);
    vafs_hashtable_destroy(&cache->cache);
    vafs_hashtable_destroy(&cache->heatmap);
    free(cache);
}

static void __heatmap_hit(struct VaFsBlockCache* cache, uint32_t index)
{
    struct __heatmap_entry* entry;

    entry = vafs_hashtable_get(&cache->heatmap, &(struct __heatmap_entry) { .index = index });
    if (entry != NULL) {
        entry->hits++;
    } else {
        // insert a new entry
        vafs_hashtable_set(&cache->heatmap, &(struct __heatmap_entry) { .index = index, .hits = 1 });
    }
}

static int __heatmap_hits(struct VaFsBlockCache* cache, uint32_t index)
{
    struct __heatmap_entry* entry = vafs_hashtable_get(&cache->heatmap, &(struct __heatmap_entry) { .index = index });
    return entry != NULL ? entry->hits : 0;
}

int vafs_cache_get(struct VaFsBlockCache* cache, uint32_t index, void** bufferOut, size_t* sizeOut)
{
    struct __block_entry* block;

    if (!cache || !bufferOut || !sizeOut) {
        errno = EINVAL;
        return -1;
    }

    // Mark the index hit, we use this to decide which blocks we will use and which
    // we won't be caching. If the user is extracting the entire vafs image, then it 
    // makes no sense to spend resources caching it. So a block index *must* have atleast
    // two hits before we cache it.
    __heatmap_hit(cache, index);

    block = vafs_hashtable_get(&cache->cache, &(struct __block_entry){ .index = index });
    if (!block) {
        errno = ENOENT;
        return -1;
    }

    // Increase it's use count, this is different from the heatmap, and we use
    // this count to decide which buffer we evict from the cache.
    block->uses++;

    // provide the user with the stored values.
    *bufferOut = block->buffer;
    *sizeOut = block->size;
    return 0;
}

static void __eject_lowuse(struct VaFsBlockCache* cache)
{
    struct cache_enum_context context = { .index = UINT_MAX, .uses = INT_MAX };
    struct __block_entry*        block;

    if (cache->cache.element_count < cache->max_blocks) {
        return;
    }

    vafs_hashtable_enumerate(&cache->cache, __cache_enum, &context);
    if (context.index == UINT_MAX) {
        return; // what?
    }
    
    block = vafs_hashtable_remove(&cache->cache, &(struct __block_entry){ .index = context.index });
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
        return NULL;
    }

    memcpy(dst, src, size);
    return dst;
}

int vafs_cache_set(struct VaFsBlockCache* cache, uint32_t index, void* buffer, size_t size)
{
    struct __block_entry* block;

    if (!cache || !buffer) {
        errno = EINVAL;
        return -1;
    }

    // First and foremost, make sure that we actually want to cache this
    // entry to ensure it has enough hits.
    if (__heatmap_hits(cache, index) <= 1) {
        // let's not cache blocks that are only used once
        return 0;
    }

    // Ensure that the block doesn't already exist in the system.
    block = vafs_hashtable_get(&cache->cache, &(struct __block_entry){ .index = index });
    if (block != NULL) {
        errno = EEXIST;
        return -1;
    }

    // Ensure we stay below our max blocks limitation, by ejecting blocks that
    // are least used in the cache.
    __eject_lowuse(cache);

    // Store the new entry, and we dublicate the memory to ensure that we own
    // the cached memory.
    vafs_hashtable_set(&cache->cache, &(struct __block_entry){ 
        .index  = index,
        .buffer = __memdup(buffer, size),
        .size   = size,
        .uses   = 1
    });
    return 0;
}

uint64_t __cache_hash(const void* element)
{
    const struct __block_entry* block = element;
    return block->index;
}

int __cache_cmp(const void* lh, const void* rh)
{
    const struct __block_entry* lblock = lh;
    const struct __block_entry* rblock = rh;
    return lblock->index == rblock->index ? 0 : -1;
}

void __cache_enum(int index, const void* element, void* userContext)
{
    const struct __block_entry*   block   = element;
    struct cache_enum_context* context = userContext;
    
    if (block->uses < context->uses) {
        context->index = block->index;
        context->uses  = block->uses;
    }
}

void __cache_enum_free(int index, const void* element, void* userContext)
{
    const struct __block_entry* block = element;
    free(block->buffer);
}

uint64_t __heatmap_hash(const void* element)
{
    const struct __heatmap_entry* block = element;
    return block->index;
}

int __heatmap_cmp(const void* lh, const void* rh)
{
    const struct __heatmap_entry* lblock = lh;
    const struct __heatmap_entry* rblock = rh;
    return lblock->index == rblock->index ? 0 : -1;
}
