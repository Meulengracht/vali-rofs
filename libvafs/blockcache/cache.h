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

#ifndef __VAFS_BLOCKCACHE_CACHE_H__
#define __VAFS_BLOCKCACHE_CACHE_H__

#include <stdint.h>
#include <stddef.h>

struct vafs_block_cache;

/**
 * @brief 
 * 
 * @param cacheOut 
 * @param maxBlocks 
 * @return int 
 */
extern int vafs_cache_create(struct vafs_block_cache** cacheOut, int maxBlocks);

/**
 * @brief 
 * 
 * @param cache 
 */
extern void vafs_cache_destroy(struct vafs_block_cache* cache);

/**
 * @brief 
 * 
 * @param cache 
 * @param index 
 * @param bufferOut 
 * @param sizeOut 
 * @return int 
 */
extern int vafs_cache_get(struct vafs_block_cache* cache, uint32_t index, void** bufferOut, size_t* sizeOut);

/**
 * @brief 
 * 
 * @param cache 
 * @param index 
 * @param buffer 
 * @param size 
 * @return int 
 */
extern int vafs_cache_set(struct vafs_block_cache* cache, uint32_t index, void* buffer, size_t size);


#endif //!__VAFS_BLOCKCACHE_CACHE_H__
