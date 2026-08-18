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

#ifndef __VAFS_FS_XATTR_H_
#define __VAFS_FS_XATTR_H_

#include <stdint.h>

// Forward declarations
struct VaFs;

// For VaFsBlockPosition_t
#include "../format/format.h"

struct VaFsXattr {
    char*               Name;
    void*               Value;
    uint32_t            ValueLength;
    struct VaFsXattr*   Link;
};

struct VaFsXattrSet {
    uint32_t            Count;
    // Writer-side dedup assigns one stable section-local index that multiple
    // entries can share when their xattr payloads are identical.
    uint32_t            Index;
    struct VaFsXattr*   Entries;
};

struct VaFsXattrStore {
    int                    Present;
    int                    PositionsLoaded;
    // Readers discover the section once through the feature table, then cache
    // per-set positions only if some caller actually touches xattrs.
    VaFsBlockPosition_t    Start;
    uint32_t               Count;
    VaFsBlockPosition_t*   Positions;
    struct VaFsXattrSet**  Sets;
};

/**
 * @brief Internal listxattr implementation with explicit symlink-follow policy.
 *
 * Tooling uses this to preserve symlink-object xattrs without changing the
 * public API contract that always follows the final symlink component.
 *
 * @param[In]  vafs         Filesystem instance to query.
 * @param[In]  path         Absolute path of the entry.
 * @param[In]  followLinks  Non-zero to resolve symlinks, zero to stop on the link itself.
 * @param[Out] buffer       Optional output buffer for the packed xattr name list.
 * @param[In]  bufferSize   Size of `buffer` in bytes.
 * @param[Out] bytesWritten Receives the required or written byte count.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int __vafs_path_listxattr(struct VaFs* vafs, const char* path, int followLinks, char* buffer, size_t bufferSize, size_t* bytesWritten);

/**
 * @brief Internal getxattr implementation with explicit symlink-follow policy.
 *
 * @param[In]  vafs         Filesystem instance to query.
 * @param[In]  path         Absolute path of the entry.
 * @param[In]  followLinks  Non-zero to resolve symlinks, zero to stop on the link itself.
 * @param[In]  name         Xattr name to fetch.
 * @param[Out] value        Optional destination buffer for the xattr value.
 * @param[In]  valueSize    Size of `value` in bytes.
 * @param[Out] bytesWritten Receives the required or written byte count.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int __vafs_path_getxattr(struct VaFs* vafs, const char* path, int followLinks, const char* name, void* value, size_t valueSize, size_t* bytesWritten);

/**
 * @brief Internal setxattr implementation with explicit symlink-follow policy.
 *
 * @param[In] vafs        Filesystem instance opened in write mode.
 * @param[In] path        Absolute path of the entry.
 * @param[In] followLinks Non-zero to resolve symlinks, zero to stop on the link itself.
 * @param[In] name        Xattr name to write.
 * @param[In] value       Optional value buffer. May be `NULL` only when `valueSize` is zero.
 * @param[In] valueSize   Size of `value` in bytes.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int __vafs_path_setxattr(struct VaFs* vafs, const char* path, int followLinks, const char* name, const void* value, size_t valueSize);

/**
 * @brief Assigns stable xattr-set indices before descriptors are serialized.
 *
 * The writer runs this after all entry metadata has been finalized so hot
 * descriptors can point at deduplicated cold xattr payloads.
 *
 * @param[In] vafs Filesystem instance being written.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int __vafs_xattr_prepare_write(struct VaFs* vafs);

/**
 * @brief Serializes the deduplicated cold xattr section into the descriptor stream.
 *
 * @param[In] vafs Filesystem instance being written.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int __vafs_xattr_write_section(struct VaFs* vafs);

/**
 * @brief Releases cached reader-side xattr store state for an image.
 *
 * @param[In] vafs Filesystem instance whose xattr store should be discarded.
 */
extern void __vafs_xattr_store_destroy(struct VaFs* vafs);

/**
 * @brief Destroys one deduplicated xattr set and all of its entries.
 *
 * @param[In] set Xattr set to destroy. `NULL` is ignored.
 */
extern void __vafs_xattr_set_destroy(struct VaFsXattrSet* set);

#endif //!__VAFS_FS_XATTR_H_
