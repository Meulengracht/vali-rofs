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
 * - Contains the implementation of the Vali Container Filesystem.
 *   This filesystem is used to store the initrd of the kernel.
 */

#include <errno.h>
#include "blockcache.h"
#include "hashtable.h"
#include "../include/vafs/platform.h"
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
    mtx_t       lock;
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

    // Construct the cache as one fully-owned unit: mutex first, then the real
    // storage table, then the admission heatmap used to decide what is worth
    // caching in the first place.
    
    cache = malloc(sizeof(struct VaFsBlockCache));
    if (!cache) {
        return NULL;
    }
    memset(cache, 0, sizeof(struct VaFsBlockCache));

    if (mtx_init(&cache->lock, mtx_plain) != thrd_success) {
        free(cache);
        return NULL;
    }
    
    status = vafs_hashtable_construct(
        &cache->cache, 0, sizeof(struct __block_entry), 
        __cache_hash, __cache_cmp
    );
    if (status != 0) {
        mtx_destroy(&cache->lock);
        free(cache);
        return NULL;
    }

    status = vafs_hashtable_construct(
        &cache->heatmap, 0, sizeof(struct __heatmap_entry), 
        __heatmap_hash, __heatmap_cmp
    );
    if (status != 0) {
        vafs_hashtable_destroy(&cache->cache);
        mtx_destroy(&cache->lock);
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

    // The cache owns duplicated block buffers as well as both hashtables.
    
    vafs_hashtable_enumerate(&cache->cache, __cache_enum_free, NULL);
    vafs_hashtable_destroy(&cache->cache);
    vafs_hashtable_destroy(&cache->heatmap);
    mtx_destroy(&cache->lock);
    free(cache);
}

static void __heatmap_hit(struct VaFsBlockCache* cache, uint32_t index)
{
    struct __heatmap_entry* entry;

    // Every lookup updates the heatmap, even real cache misses, because block
    // admission is based on repeated demand rather than first touch.

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

int vafs_cache_get(struct VaFsBlockCache* cache, uint32_t index, void* buffer, size_t bufferCapacity, size_t* sizeOut)
{
    struct __block_entry* block;

    if (!cache || !buffer || !sizeOut) {
        errno = EINVAL;
        return -1;
    }

    // Copy out under the cache lock so readers never borrow raw cache-owned
    // pointers that another thread could evict immediately after lookup.

    if (mtx_lock(&cache->lock) != thrd_success) {
        errno = EBUSY;
        return -1;
    }

    // Mark the index hit, we use this to decide which blocks we will use and which
    // we won't be caching. If the user is extracting the entire vafs image, then it 
    // makes no sense to spend resources caching it. So a block index *must* have atleast
    // two hits before we cache it.
    __heatmap_hit(cache, index);

    block = vafs_hashtable_get(&cache->cache, &(struct __block_entry){ .index = index });
    if (!block) {
        // Misses still count toward future admission through the heatmap, but
        // there is no resident block to serve right now.
        mtx_unlock(&cache->lock);
        return -1;
    }

    // Increase it's use count, this is different from the heatmap, and we use
    // this count to decide which buffer we evict from the cache.
    block->uses++;

    if (bufferCapacity < block->size) {
        // The caller decides the destination buffer size, so fail rather than
        // truncating a cached block copy.
        mtx_unlock(&cache->lock);
        errno = EINVAL;
        return -1;
    }

    memcpy(buffer, block->buffer, block->size);
    *sizeOut = block->size;
    mtx_unlock(&cache->lock);
    return 0;
}

static void __eject_lowuse(struct VaFsBlockCache* cache)
{
    struct cache_enum_context context = { .index = UINT_MAX, .uses = INT_MAX };
    struct __block_entry*     block;

    // Once the cache reaches capacity, evict the least-used resident entry
    // before admitting a new block.

    if (cache->cache.element_count < cache->max_blocks) {
        return;
    }

    vafs_hashtable_enumerate(&cache->cache, __cache_enum, &context);
    if (context.index == UINT_MAX) {
        // No victim means the table was empty or inconsistent; either way a
        // no-op is safer than removing an arbitrary entry.
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

    // Admission is conservative: only frequently reused blocks get duplicated
    // into cache storage, and duplicate races are treated as success.

    if (mtx_lock(&cache->lock) != thrd_success) {
        errno = EBUSY;
        return -1;
    }

    // First and foremost, make sure that we actually want to cache this
    // entry to ensure it has enough hits.
    if (__heatmap_hits(cache, index) <= 1) {
        // let's not cache blocks that are only used once
        mtx_unlock(&cache->lock);
        return 0;
    }

    // Ensure that the block doesn't already exist in the system.
    block = vafs_hashtable_get(&cache->cache, &(struct __block_entry){ .index = index });
    if (block != NULL) {
        // Two readers can race to cache the same block; the later insert does
        // not need to surface an error because the block is already resident.
        mtx_unlock(&cache->lock);
        return 0;
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
    mtx_unlock(&cache->lock);
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
