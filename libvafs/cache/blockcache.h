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
 * - Contains the implementation of the Vali Initrd Filesystem.
 *   This filesystem is used to store the initrd of the kernel.
 */

#ifndef __VAFS_BLOCKCACHE_CACHE_H__
#define __VAFS_BLOCKCACHE_CACHE_H__

#include <stdint.h>
#include <stddef.h>

struct VaFsBlockCache;

/**
 * @brief Creates a new block cache, that contains the N most-used blocks. The cache
 * will cache a maximum of @maxBlocks blocks, after this, the cache will start evicting
 * the least-used blocks.
 * 
 * @param[In]  maxBlocks The maximum number of blocks to cache.
 * @param[Out] cacheOut  A pointer to store the newly malloc'd cache.
 * @return int 0 on success, -1 on failure, errno will be set accordingly.
 */
extern int vafs_cache_create(int maxBlocks, struct VaFsBlockCache** cacheOut);

/**
 * @brief Destroys the blocks cache and frees any resources allocated.
 * 
 * @param[In] cache The cache to destroy. 
 */
extern void vafs_cache_destroy(struct VaFsBlockCache* cache);

/**
 * @brief Retrieves a block from the cache.
 * 
 * @param[In]  cache     The cache to retrieve the block from. 
 * @param[In]  index     The index of the block to retrieve.
 * @param[Out] bufferOut A pointer where a buffer pointer will be stored. 
 * @param[Out] sizeOut   A pointer where the size of the block will be stored.
 * @return int 0 on success, -1 on failure, errno will be set accordingly. 
 */
extern int vafs_cache_get(struct VaFsBlockCache* cache, uint32_t index, void** bufferOut, size_t* sizeOut);

/**
 * @brief Stores a block in the cache.
 * 
 * @param[In] cache  The cache to store the block in. 
 * @param[In] index  The index of the block to store.
 * @param[In] buffer The buffer to store, a copy of it will be created and managed by the cache.
 * @param[In] size   The size of the buffer.
 * @return int 
 */
extern int vafs_cache_set(struct VaFsBlockCache* cache, uint32_t index, void* buffer, size_t size);

#endif //!__VAFS_BLOCKCACHE_CACHE_H__
