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

#include <errno.h>
#include "private.h"
#include <stdlib.h>
#include <string.h>
#include <vafs/stat.h>

#if defined(_WIN32) || defined(_WIN64)
#  if !defined(S_IFLNK)
#     define S_IFLNK 0
#  endif
#endif //!#if defined(_WIN32) || defined(_WIN64)

int __vafs_is_root_path(
    const char* path)
{
    size_t len = strlen(path);
    if (len == 1 && path[0] == '/') {
        return 1;
    }
    if (len == 0) {
        return 1;
    }
    return 0;
}

int __vafs_pathtoken(
    const char* path,
    char*       token,
    size_t      tokenSize)
{
    size_t i;
    size_t j;
    size_t remainingLength;

    if (path == NULL || token == NULL) {
        errno = EINVAL;
        return 0;
    }

    remainingLength = strlen(path);
    if (remainingLength == 0) {
        errno = ENOENT;
        return 0;
    }

    // skip leading slashes
    for (i = 0; i < remainingLength; i++) {
        if (path[i] != '/') {
            break;
        }
    }

    // copy over token untill \0 or /
    for (j = 0; i < remainingLength; i++, j++) {
        if (path[i] == '/' || path[i] == '\0') {
            break;
        }

        if (j >= tokenSize) {
            errno = ENAMETOOLONG;
            return 0;
        }
        token[j] = path[i];
    }
    token[j] = '\0';
    return (int)i;
}

int __vafs_resolve_symlink(
        char*       buffer,
        size_t      bufferLength,
        const char* baseStart,
        size_t      baseLength,
        const char* symlinkTarget)
{
    size_t i, j;

    if (buffer == NULL || baseStart == NULL || symlinkTarget == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Validate symlink target is not empty
    if (symlinkTarget[0] == '\0') {
        VAFS_ERROR("__vafs_resolve_symlink: symlink target is empty\n");
        errno = EINVAL;
        return -1;
    }

    // Validate symlink target length
    size_t targetLength = strlen(symlinkTarget);
    if (targetLength > VAFS_PATH_MAX) {
        VAFS_ERROR("__vafs_resolve_symlink: symlink target exceeds max path length\n");
        errno = ENAMETOOLONG;
        return -1;
    }

    // start by copying base path over into buffer, but let us
    // 'clean' the path as we go
    for (i = 0, j = 0; i < baseLength && j < bufferLength; i++) {
        // skip double separators, this way we minimize the final path
        // and also make things neater.
        if (baseStart[i] == '/') {
            if (j == 0 || (j > 0 && buffer[j - 1] != '/')) {
                buffer[j++] = '/';
            }
        } else {
            buffer[j++] = baseStart[i];
        }
    }

    // Check for buffer overflow during base path copy
    if (i < baseLength) {
        VAFS_ERROR("__vafs_resolve_symlink: buffer overflow during base path copy\n");
        errno = ENAMETOOLONG;
        return -1;
    }

    // now we resolve the final path by appending the symlink target,
    // while canonicalization of the final path
    for (i = 0; i < strlen(symlinkTarget) && j < bufferLength; i++) {
        if (symlinkTarget[i] == '/') {
            if (j > 0 && buffer[j - 1] != '/') {
                buffer[j++] = '/';
            }
            continue;
        }

        if (symlinkTarget[i] == '.') {
            // handle ./ as skip
            if (symlinkTarget[i + 1] == '/') {
                i++;
                continue;
            }

            // handle ../ as go back one level if possible
            if (symlinkTarget[i + 1] == '.' && symlinkTarget[i + 2] == '/') {
                i += 2;
                if (j > 0) {
                    j--;
                    while (j > 0 && buffer[j - 1] != '/') {
                        j--;
                    }
                }
                continue;
            } else {
                buffer[j++] = symlinkTarget[i];
            }
        } else {
            buffer[j++] = symlinkTarget[i];
        }
    }

    // Check for buffer overflow during target append
    if (i < strlen(symlinkTarget)) {
        VAFS_ERROR("__vafs_resolve_symlink: buffer overflow during target append\n");
        errno = ENAMETOOLONG;
        return -1;
    }

    // Validate final path length
    if (j >= bufferLength) {
        VAFS_ERROR("__vafs_resolve_symlink: resolved path exceeds buffer length\n");
        errno = ENAMETOOLONG;
        return -1;
    }

    // terminate the string
    buffer[j] = '\0';

    // return the number of characters written
    return (int)j;
}

int __vafs_path_stat_internal(
    struct VaFs*      vafs,
    const char*       path,
    int               followLinks,
    struct vafs_stat* stat,
    int               symlinkDepth)
{
    struct VaFsDirectoryEntry* entries;
    const char*                remainingPath = path;
    char                       token[VAFS_NAME_MAX + 1];

    if (vafs == NULL || path == NULL || stat == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Check symlink depth limit
    if (symlinkDepth > VAFS_SYMLINK_MAX_DEPTH) {
        VAFS_ERROR("__vafs_path_stat_internal: symlink depth limit exceeded (depth=%d, max=%d)\n",
            symlinkDepth, VAFS_SYMLINK_MAX_DEPTH);
        errno = ELOOP;
        return -1;
    }

    // special case - root directory, we specfiy
    // default access for it for now
    if (__vafs_is_root_path(path)) {
        stat->mode = S_IFDIR | 0755;
        stat->size = 0;
        return 0;
    }

    entries = __vafs_directory_entries(vafs->RootDirectory);
    do {
        const char* previousPath = remainingPath;
        int         charsConsumed = __vafs_pathtoken(remainingPath, token, sizeof(token));
        if (!charsConsumed) {
            break;
        }
        remainingPath += charsConsumed;

        // find the name in the directory
        while (entries != NULL) {
            if (!strcmp(__vafs_directory_entry_name(entries), token)) {
                if (entries->Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
                    if (remainingPath[0] == '\0') {
                        stat->mode = S_IFDIR | entries->Directory->Descriptor.Permissions;
                        stat->size = 0;
                        return 0;
                    }

                    // otherwise fall-through and continue
                } else if (entries->Type == VA_FS_DESCRIPTOR_TYPE_SYMLINK) {
                    if (!followLinks) {
                        if (remainingPath[0] == '\0') {
                            stat->mode = S_IFLNK | 0777;
                            stat->size = strlen(entries->Symlink->Target);
                            return 0;
                        } else {
                            errno = ENOTDIR;
                            return -1;
                        }
                    }

                    char* pathBuffer = malloc(VAFS_PATH_MAX);
                    int   written;
                    int   status;
                    if (!pathBuffer) {
                        VAFS_ERROR("__vafs_path_stat_internal: failed to allocate path buffer\n");
                        errno = ENOMEM;
                        return -1;
                    }

                    written = __vafs_resolve_symlink(
                            pathBuffer,
                            VAFS_PATH_MAX,
                            path,
                            previousPath - path,
                            entries->Symlink->Target
                    );
                    if (written < 0) {
                        VAFS_ERROR("__vafs_path_stat_internal: failed to resolve symlink %s\n", entries->Symlink->Target);
                        free(pathBuffer);
                        return -1;
                    }

                    status = __vafs_path_stat_internal(vafs, pathBuffer, followLinks, stat, symlinkDepth + 1);
                    free(pathBuffer);
                    return status;
                } else if (entries->Type == VA_FS_DESCRIPTOR_TYPE_FILE) {
                    // If we encounter a file in this case, we must be at the end of the path
                    if (remainingPath[0] != '\0') {
                        errno = ENOTDIR;
                        return -1;
                    }

                    stat->mode = S_IFREG | entries->File->Descriptor.Permissions;
                    stat->size = entries->File->Descriptor.FileLength;
                    return 0;
                } else {
                    errno = ENOENT;
                    return -1;
                }

                entries = __vafs_directory_entries(entries->Directory);
                break;
            }
            entries = entries->Link;
        }
    } while (1);

    errno = ENOENT;
    return -1;
}

int vafs_path_stat(
    struct VaFs*      vafs,
    const char*       path,
    int               followLinks,
    struct vafs_stat* stat)
{
    return __vafs_path_stat_internal(vafs, path, followLinks, stat, 0);
}
