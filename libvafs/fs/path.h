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

#ifndef __VAFS_FS_PATH_H_
#define __VAFS_FS_PATH_H_

#include <stdint.h>
#include <stddef.h>

// Symlink resolution limits
// Maximum number of symlinks that can be traversed in a single path resolution
// This prevents infinite loops from cyclic symlinks and limits resource consumption
#define VAFS_SYMLINK_MAX_DEPTH 40

/**
 * @brief Checks whether a path refers to the filesystem root.
 *
 * Empty strings and `/` are treated as the root path.
 *
 * @param[In] path Path string to inspect.
 * @return 1 when the path resolves to root, otherwise 0.
 */
extern int __vafs_is_root_path(const char* path);

/**
 * @brief Extracts the next path component from a path string.
 *
 * Leading separators are skipped. The extracted component is copied into
 * `token`, and the return value is the number of characters consumed from
 * the original path string.
 *
 * @param[In]  path      Path string to tokenize.
 * @param[Out] token     Buffer that receives the next component.
 * @param[In]  tokenSize Size of the `token` buffer in bytes.
 * @return Number of characters consumed, or 0 when no token could be produced.
 */
extern int __vafs_pathtoken(const char* path, char* token, size_t tokenSize);

/**
 * @brief Resolves a symlink target relative to a base path prefix.
 *
 * The helper normalizes repeated separators and handles `.` and `..`
 * components while building the resolved path.
 *
 * @param[Out] buffer        Destination buffer for the resolved path.
 * @param[In]  bufferLength  Size of `buffer` in bytes.
 * @param[In]  baseStart     Start of the original path used as the base prefix.
 * @param[In]  baseLength    Number of bytes from `baseStart` to keep as the base prefix.
 * @param[In]  symlinkTarget Symlink target to append and normalize.
 * @return Number of characters written on success, otherwise -1 with `errno` set.
 */
extern int __vafs_resolve_symlink(char* buffer, size_t bufferLength, const char* baseStart, size_t baseLength, const char* symlinkTarget);

/**
 * @brief Internal path-stat implementation with explicit symlink depth tracking.
 *
 * This resolves the path component-by-component, optionally follows symlinks,
 * and fills the metadata structure for the final entry.
 *
 * @param[In]  vafs         Filesystem instance to resolve within.
 * @param[In]  path         Absolute or root-relative path to stat.
 * @param[In]  followLinks  Non-zero to follow symlinks, zero to report the symlink itself.
 * @param[Out] metadata     Receives the resolved metadata on success.
 * @param[In]  symlinkDepth Current recursive symlink depth.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int __vafs_path_stat_internal(struct VaFs* vafs, const char* path, int followLinks, struct VaFsMetadata* metadata, int symlinkDepth);

/**
 * @brief Internal file-open implementation with explicit symlink depth tracking.
 *
 * @param[In]  vafs         Filesystem instance to resolve within.
 * @param[In]  path         Path to the file that should be opened.
 * @param[Out] handleOut    Receives the opened file handle on success.
 * @param[In]  symlinkDepth Current recursive symlink depth.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int __vafs_file_open_internal(struct VaFs* vafs, const char* path, struct VaFsFileHandle** handleOut, int symlinkDepth);

#endif // __VAFS_FS_PATH_H_
