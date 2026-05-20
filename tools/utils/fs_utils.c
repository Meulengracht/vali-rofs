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
 */

#include "utils.h"

#include <stddef.h>
#include <errno.h>
#include <vafs/stat.h>
#include <vafs/xattr.h>

#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#include "dirent_win32.h"
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#if defined(__linux__) || defined(__APPLE__)
#include <sys/xattr.h>
#endif
#if defined(__linux__)
#include <sys/sysmacros.h>
#endif
#endif

#if defined(ENOATTR) && !defined(ENODATA)
#define VAFS_XATTR_ENOATTR ENOATTR
#else
#define VAFS_XATTR_ENOATTR ENODATA
#endif

typedef ptrdiff_t platform_xattr_ssize_t;

// The tool layer needs the library's internal non-following xattr path hooks
// for symlink packaging/extraction, but not the rest of libvafs/private.h.
extern int __vafs_path_listxattr(struct VaFs* vafs, const char* path, int followLinks, char* buffer, size_t bufferSize, size_t* bytesWritten);
extern int __vafs_path_getxattr(struct VaFs* vafs, const char* path, int followLinks, const char* name, void* value, size_t valueSize, size_t* bytesWritten);
extern int __vafs_path_setxattr(struct VaFs* vafs, const char* path, int followLinks, const char* name, const void* value, size_t valueSize);

static uint64_t __platform_fs_make_object_id(
    const struct stat* st)
{
    uint64_t hash = 1469598103934665603ULL;
    uint64_t device;
    uint64_t inode;

    device = (uint64_t)(unsigned long long)st->st_dev;
    inode = (uint64_t)(unsigned long long)st->st_ino;

    // Host inode and device identifiers are platform-specific in width, so we
    // fold both into one stable 64-bit token before persisting the image-level
    // object id used to reconstruct hardlink groups.
    hash ^= device;
    hash *= 1099511628211ULL;
    hash ^= inode;
    hash *= 1099511628211ULL;
    return hash != 0 ? hash : 1;
}

static enum VaFsEntryType __platform_fs_mode_to_entry_type(uint32_t mode)
{
    if (platform_fs_mode_is_directory(mode)) {
        return VaFsEntryType_Directory;
    }
    if (platform_fs_mode_is_symlink(mode)) {
        return VaFsEntryType_Symlink;
    }
    if (platform_fs_mode_is_character_device(mode)) {
        return VaFsEntryType_CharacterDevice;
    }
    if (platform_fs_mode_is_block_device(mode)) {
        return VaFsEntryType_BlockDevice;
    }
    if (platform_fs_mode_is_fifo(mode)) {
        return VaFsEntryType_Fifo;
    }
    if (platform_fs_mode_is_file(mode)) {
        return VaFsEntryType_File;
    }
    return VaFsEntryType_Unknown;
}

int platform_fs_mode_is_file(uint32_t mode)
{
    return S_ISREG(mode);
}

int platform_fs_mode_is_symlink(uint32_t mode)
{
    return S_ISLNK(mode);
}

int platform_fs_mode_is_directory(uint32_t mode)
{
    return S_ISDIR(mode);
}

int platform_fs_mode_is_character_device(uint32_t mode)
{
    return S_ISCHR(mode);
}

int platform_fs_mode_is_block_device(uint32_t mode)
{
#if defined(_WIN32) || defined(_WIN64)
    (void)mode;
    return 0;
#else
    return S_ISBLK(mode);
#endif
}

int platform_fs_mode_is_fifo(uint32_t mode)
{
    return S_ISFIFO(mode);
}

int platform_fs_mode_is_special(uint32_t mode)
{
    return platform_fs_mode_is_character_device(mode) ||
        platform_fs_mode_is_block_device(mode) ||
        platform_fs_mode_is_fifo(mode);
}

uint32_t platform_fs_mode_permissions(uint32_t mode)
{
    return mode & 07777u;
}

static void __platform_fs_normalize_xattr_errno(void)
{
    if (errno == VAFS_XATTR_ENOATTR) {
        errno = ENODATA;
    }
}

static platform_xattr_ssize_t __platform_fs_listxattr(
    const char* path,
    int         followLinks,
    char*       buffer,
    size_t      bufferSize)
{
#if defined(_WIN32) || defined(_WIN64)
    (void)path;
    (void)followLinks;
    (void)buffer;
    (void)bufferSize;
    errno = ENOTSUP;
    return -1;
#elif defined(__linux__)
    return followLinks ?
        listxattr(path, buffer, bufferSize) :
        llistxattr(path, buffer, bufferSize);
#elif defined(__APPLE__)
    return listxattr(path, buffer, bufferSize, followLinks ? 0 : XATTR_NOFOLLOW);
#else
    (void)path;
    (void)followLinks;
    (void)buffer;
    (void)bufferSize;
    errno = ENOTSUP;
    return -1;
#endif
}

static platform_xattr_ssize_t __platform_fs_getxattr(
    const char* path,
    const char* name,
    int         followLinks,
    void*       value,
    size_t      valueSize)
{
#if defined(_WIN32) || defined(_WIN64)
    (void)path;
    (void)name;
    (void)followLinks;
    (void)value;
    (void)valueSize;
    errno = ENOTSUP;
    return -1;
#elif defined(__linux__)
    return followLinks ?
        getxattr(path, name, value, valueSize) :
        lgetxattr(path, name, value, valueSize);
#elif defined(__APPLE__)
    return getxattr(path, name, value, valueSize, 0, followLinks ? 0 : XATTR_NOFOLLOW);
#else
    (void)path;
    (void)name;
    (void)followLinks;
    (void)value;
    (void)valueSize;
    errno = ENOTSUP;
    return -1;
#endif
}

static int __platform_fs_setxattr(
    const char* path,
    const char* name,
    int         followLinks,
    const void* value,
    size_t      valueSize)
{
    static const char g_emptyValue = '\0';
    const void*       payload = valueSize != 0 ? value : &g_emptyValue;

#if defined(_WIN32) || defined(_WIN64)
    (void)path;
    (void)name;
    (void)followLinks;
    (void)payload;
    (void)valueSize;
    errno = ENOTSUP;
    return -1;
#elif defined(__linux__)
    return followLinks ?
        setxattr(path, name, payload, valueSize, 0) :
        lsetxattr(path, name, payload, valueSize, 0);
#elif defined(__APPLE__)
    return setxattr(path, name, payload, valueSize, 0, followLinks ? 0 : XATTR_NOFOLLOW);
#else
    (void)path;
    (void)name;
    (void)followLinks;
    (void)payload;
    (void)valueSize;
    errno = ENOTSUP;
    return -1;
#endif
}

static int __vafs_listxattr(
    struct VaFs* vafs,
    const char*  path,
    int          followLinks,
    char*        buffer,
    size_t       bufferSize,
    size_t*      bytesWritten)
{
    return followLinks ?
        vafs_path_listxattr(vafs, path, buffer, bufferSize, bytesWritten) :
        __vafs_path_listxattr(vafs, path, 0, buffer, bufferSize, bytesWritten);
}

static int __vafs_getxattr(
    struct VaFs* vafs,
    const char*  path,
    int          followLinks,
    const char*  name,
    void*        value,
    size_t       valueSize,
    size_t*      bytesWritten)
{
    return followLinks ?
        vafs_path_getxattr(vafs, path, name, value, valueSize, bytesWritten) :
        __vafs_path_getxattr(vafs, path, 0, name, value, valueSize, bytesWritten);
}

static int __vafs_setxattr(
    struct VaFs* vafs,
    const char*  path,
    int          followLinks,
    const char*  name,
    const void*  value,
    size_t       valueSize)
{
    return followLinks ?
        vafs_path_setxattr(vafs, path, name, value, valueSize) :
        __vafs_path_setxattr(vafs, path, 0, name, value, valueSize);
}

int platform_fs_xattr_error_is_nonfatal(int error)
{
    return error == EACCES ||
        error == EPERM ||
        error == ENOTSUP ||
        error == ENOSYS;
}

int platform_fs_import_xattrs(
    struct VaFs* vafs,
    const char*  imagePath,
    const char*  hostPath,
    int          followLinks)
{
    char*                names = NULL;
    platform_xattr_ssize_t namesLength;
    size_t               offset = 0;

    if (vafs == NULL || imagePath == NULL || hostPath == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Probe first because host xattr name lists are variable-length and the
    // builder should size one buffer to the exact host answer.
    namesLength = __platform_fs_listxattr(hostPath, followLinks, NULL, 0);
    if (namesLength < 0) {
        __platform_fs_normalize_xattr_errno();
        if (errno == ENODATA) {
            return 0;
        }
        return -1;
    }
    if (namesLength == 0) {
        return 0;
    }

    names = malloc((size_t)namesLength);
    if (names == NULL) {
        errno = ENOMEM;
        return -1;
    }

    namesLength = __platform_fs_listxattr(hostPath, followLinks, names, (size_t)namesLength);
    if (namesLength < 0) {
        __platform_fs_normalize_xattr_errno();
        free(names);
        return -1;
    }

    while (offset < (size_t)namesLength) {
        const char* name = names + offset;
        size_t      nameLength = strlen(name);
        void*       value = NULL;
        platform_xattr_ssize_t valueLength;
        int         status;

        // Fetch each value in two steps so empty xattrs round-trip cleanly and
        // this helper never bakes in an arbitrary host-side size ceiling.
        valueLength = __platform_fs_getxattr(hostPath, name, followLinks, NULL, 0);
        if (valueLength < 0) {
            __platform_fs_normalize_xattr_errno();
            free(names);
            return -1;
        }

        if (valueLength != 0) {
            value = malloc((size_t)valueLength);
            if (value == NULL) {
                free(names);
                errno = ENOMEM;
                return -1;
            }

            valueLength = __platform_fs_getxattr(hostPath, name, followLinks, value, (size_t)valueLength);
            if (valueLength < 0) {
                __platform_fs_normalize_xattr_errno();
                free(value);
                free(names);
                return -1;
            }
        }

        // Preserve the caller's follow policy so packaging can intentionally
        // target the symlink object itself instead of its resolved destination.
        status = __vafs_setxattr(vafs, imagePath, followLinks, name, value, (size_t)valueLength);
        free(value);
        if (status != 0) {
            free(names);
            return -1;
        }

        offset += nameLength + 1;
    }

    free(names);
    return 0;
}

int platform_fs_export_xattrs(
    struct VaFs* vafs,
    const char*  imagePath,
    const char*  hostPath,
    int          followLinks)
{
    char*  names = NULL;
    size_t namesLength;
    size_t offset = 0;
    int    status;

    if (vafs == NULL || imagePath == NULL || hostPath == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Mirror the POSIX size-probe pattern on the image side so extraction does
    // not guess at how much metadata a particular entry carries.
    status = __vafs_listxattr(vafs, imagePath, followLinks, NULL, 0, &namesLength);
    if (status != 0 || namesLength == 0) {
        return status;
    }

    names = malloc(namesLength);
    if (names == NULL) {
        errno = ENOMEM;
        return -1;
    }

    status = __vafs_listxattr(vafs, imagePath, followLinks, names, namesLength, &namesLength);
    if (status != 0) {
        free(names);
        return -1;
    }

    while (offset < namesLength) {
        const char* name = names + offset;
        size_t      nameLength = strlen(name);
        size_t      valueLength;
        void*       value = NULL;

        // Image xattr values are also variable-length, so extraction probes
        // first and only allocates when there is real payload to restore.
        status = __vafs_getxattr(vafs, imagePath, followLinks, name, NULL, 0, &valueLength);
        if (status != 0) {
            free(names);
            return -1;
        }

        if (valueLength != 0) {
            value = malloc(valueLength);
            if (value == NULL) {
                free(names);
                errno = ENOMEM;
                return -1;
            }

            status = __vafs_getxattr(vafs, imagePath, followLinks, name, value, valueLength, &valueLength);
            if (status != 0) {
                free(value);
                free(names);
                return -1;
            }
        }

        // Reuse the same follow policy that resolved the image entry so
        // symlink-object xattrs do not collapse onto the target during export.
        if (__platform_fs_setxattr(hostPath, name, followLinks, value, valueLength) != 0) {
            __platform_fs_normalize_xattr_errno();
            free(value);
            free(names);
            return -1;
        }

        free(value);
        offset += nameLength + 1;
    }

    free(names);
    return 0;
}

int platform_fs_read_metadata(const char* path, struct VaFsMetadata* metadata)
{
    struct stat        st;
    enum VaFsEntryType type;

    if (path == NULL || metadata == NULL) {
        errno = EINVAL;
        return -1;
    }

#if defined(_WIN32) || defined(_WIN64)
    if (stat(path, &st) != 0) {
        return -1;
    }
#else
    // The builder needs lstat-style metadata here so symlinks and device nodes
    // keep their own type instead of inheriting the host target's type.
    if (lstat(path, &st) != 0) {
        return -1;
    }
#endif

    type = __platform_fs_mode_to_entry_type((uint32_t)st.st_mode);
    if (type == VaFsEntryType_Unknown) {
        errno = EINVAL;
        return -1;
    }

    vafs_metadata_initialize(metadata);
    vafs_metadata_set_mode(metadata, type, platform_fs_mode_permissions((uint32_t)st.st_mode));
    // Tooling persists host link identity here so mkvafs can collapse shared
    // files into aliases and unmkvafs can rebuild the same relationship later.
    metadata->LinkCount = (uint32_t)st.st_nlink;
    metadata->ObjectId = __platform_fs_make_object_id(&st);
    metadata->Mask |= VaFsMetadataMask_LinkCount | VaFsMetadataMask_ObjectId;

#if !defined(_WIN32) && !defined(_WIN64)
    if (type == VaFsEntryType_CharacterDevice || type == VaFsEntryType_BlockDevice) {
        metadata->Device.Major = (uint32_t)major(st.st_rdev);
        metadata->Device.Minor = (uint32_t)minor(st.st_rdev);
        metadata->Mask |= VaFsMetadataMask_Device;
    }
#endif
    return 0;
}

int platform_fs_directory_exists(const char* path)
{
    struct stat st;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (stat(path, &st)) {
        if (errno == ENOENT) {
            return 0;
        }
        return -1;
    }
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

int platform_fs_create_directory(const char* path, uint32_t permissions)
{
    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

#if defined(_WIN32) || defined(_WIN64)
    (void)permissions;
    return _mkdir(path);
#else
    return mkdir(path, (mode_t)permissions);
#endif
}

int platform_fs_create_hardlink(const char* targetPath, const char* path)
{
    if (targetPath == NULL || path == NULL) {
        errno = EINVAL;
        return -1;
    }

#if defined(_WIN32) || defined(_WIN64)
    if (!CreateHardLinkA(path, targetPath, NULL)) {
        DWORD error = GetLastError();

        // Normalize native errors here so the higher-level extraction policy
        // can treat special-file and hardlink host limitations consistently.
        switch (error) {
            case ERROR_ACCESS_DENIED:
                errno = EACCES;
                break;
            case ERROR_PRIVILEGE_NOT_HELD:
                errno = EPERM;
                break;
            case ERROR_NOT_SUPPORTED:
            case ERROR_INVALID_FUNCTION:
                errno = ENOTSUP;
                break;
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
                errno = ENOENT;
                break;
            case ERROR_ALREADY_EXISTS:
                errno = EEXIST;
                break;
            default:
                errno = EIO;
                break;
        }
        return -1;
    }
    return 0;
#else
    if (link(targetPath, path) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
#endif
}

int platform_fs_create_special(const char* path, const struct VaFsMetadata* metadata)
{
    if (path == NULL || metadata == NULL) {
        errno = EINVAL;
        return -1;
    }

#if defined(_WIN32) || defined(_WIN64)
    (void)path;
    (void)metadata;
    errno = ENOTSUP;
    return -1;
#else
    int    status;
    mode_t permissions;

    permissions = (mode_t)(metadata->Mode & 07777u);
    switch (metadata->Type) {
        case VaFsEntryType_CharacterDevice:
            if ((metadata->Mask & VaFsMetadataMask_Device) == 0) {
                errno = EINVAL;
                return -1;
            }
            status = mknod(
                path,
                S_IFCHR | permissions,
                makedev(metadata->Device.Major, metadata->Device.Minor)
            );
            break;
        case VaFsEntryType_BlockDevice:
            if ((metadata->Mask & VaFsMetadataMask_Device) == 0) {
                errno = EINVAL;
                return -1;
            }
            status = mknod(
                path,
                S_IFBLK | permissions,
                makedev(metadata->Device.Major, metadata->Device.Minor)
            );
            break;
        case VaFsEntryType_Fifo:
            status = mkfifo(path, permissions);
            break;
        default:
            errno = EINVAL;
            return -1;
    }

    if (status != 0 && errno != EEXIST) {
        return -1;
    }
    return platform_fs_chmod(path, metadata->Mode & 07777u);
#endif
}

int platform_fs_chmod(const char* path, uint32_t permissions)
{
    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

#if defined(_WIN32) || defined(_WIN64)
    return _chmod(path, (int)permissions);
#else
    return chmod(path, (mode_t)permissions);
#endif
}