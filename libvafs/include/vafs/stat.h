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

#ifndef __VAFS_STAT_H__
#define __VAFS_STAT_H__

#include <vafs/vafs.h>
#include <vafs/platform.h>
#include <string.h>

enum VaFsMetadataMask {
    VaFsMetadataMask_Type              = 1u << 0,
    VaFsMetadataMask_Mode              = 1u << 1,
    VaFsMetadataMask_Size              = 1u << 2,
    VaFsMetadataMask_Uid               = 1u << 3,
    VaFsMetadataMask_Gid               = 1u << 4,
    VaFsMetadataMask_LinkCount         = 1u << 5,
    VaFsMetadataMask_ObjectId          = 1u << 6,
    VaFsMetadataMask_MTime             = 1u << 7,
    VaFsMetadataMask_ATime             = 1u << 8,
    VaFsMetadataMask_CTime             = 1u << 9,
    VaFsMetadataMask_BirthTime         = 1u << 10,
    VaFsMetadataMask_Device            = 1u << 11,
    VaFsMetadataMask_XattrCount        = 1u << 12,
    VaFsMetadataMask_WindowsAttributes = 1u << 13,
};

struct VaFsTimestamp {
    int64_t  Seconds;
    uint32_t Nanoseconds;
};

struct VaFsDeviceNumber {
    uint32_t Major;
    uint32_t Minor;
};

struct VaFsMetadata {
    uint32_t                Mask;
    enum VaFsEntryType      Type;
    uint32_t                Mode;
    uint32_t                Uid;
    uint32_t                Gid;
    uint32_t                LinkCount;
    uint64_t                Size;
    uint64_t                ObjectId;
    uint32_t                XattrCount;
    struct VaFsTimestamp    MTime;
    struct VaFsTimestamp    ATime;
    struct VaFsTimestamp    CTime;
    struct VaFsTimestamp    BirthTime;
    struct VaFsDeviceNumber Device;
    uint32_t                WindowsAttributes;
};

static inline void vafs_metadata_initialize(
    struct VaFsMetadata* metadata)
{
    if (metadata == NULL) {
        return;
    }

    // Metadata availability is explicit through Mask bits. Zeroing the whole
    // structure up front avoids advertising unsupported fields just because a
    // caller reused stack storage from an older permission-only code path.
    memset(metadata, 0, sizeof(struct VaFsMetadata));
    metadata->Type = VaFsEntryType_Unknown;
}

static inline void vafs_metadata_set_mode(
    struct VaFsMetadata* metadata,
    enum VaFsEntryType   type,
    uint32_t             mode)
{
    uint32_t typeBits = 0;

    if (metadata == NULL) {
        return;
    }

    switch (type) {
        case VaFsEntryType_File:
        case VaFsEntryType_Hardlink:
            typeBits = S_IFREG;
            break;
        case VaFsEntryType_Directory:
            typeBits = S_IFDIR;
            break;
        case VaFsEntryType_Symlink:
            typeBits = S_IFLNK;
            break;
#if defined(S_IFCHR)
        case VaFsEntryType_CharacterDevice:
            typeBits = S_IFCHR;
            break;
#endif
#if defined(S_IFBLK)
        case VaFsEntryType_BlockDevice:
            typeBits = S_IFBLK;
            break;
#endif
#if defined(S_IFIFO)
        case VaFsEntryType_Fifo:
            typeBits = S_IFIFO;
            break;
#endif
        default:
            break;
    }

    // Older callers think in terms of entry kind plus permission bits because
    // the v1 API split those concepts. Rebuilding the mode word here preserves
    // that calling convention while moving the public contract to full modes.
    metadata->Type = type;
    metadata->Mode = typeBits | (mode & 07777u);
    metadata->Mask |= VaFsMetadataMask_Type | VaFsMetadataMask_Mode;
}

/**
 * @brief Retrieves POSIX-like metadata for a filesystem entry.
 *
 * The returned mode field contains both the entry type bits and the stored permission bits. When
 * followLinks is non-zero, symbolic links in the path are resolved up to the library's symlink
 * depth limit; otherwise the metadata of the link itself is returned.
 *
 * @param vafs        Filesystem handle to query.
 * @param path        Absolute path of the entry.
 * @param followLinks Non-zero to follow symbolic links, 0 to stat the link itself.
 * @param metadata    Receives the resulting metadata.
 * @return int Returns 0 on success, -1 on failure. See errno for details such as EINVAL,
 *             ENOENT, ENOTDIR, or ELOOP.
 */
extern int vafs_path_stat(
    struct VaFs*         vafs,
    const char*          path,
    int                  followLinks,
    struct VaFsMetadata* metadata);

/**
 * @brief Retrieves metadata for an already opened file handle.
 *
 * @param handle   File handle to query.
 * @param metadata Receives the resulting metadata.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_file_stat(
    struct VaFsFileHandle* handle,
    struct VaFsMetadata*   metadata);

/**
 * @brief Retrieves metadata for an already opened directory handle.
 *
 * @param handle   Directory handle to query.
 * @param metadata Receives the resulting metadata.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_directory_stat(
    struct VaFsDirectoryHandle* handle,
    struct VaFsMetadata*        metadata);

/**
 * @brief Retrieves metadata for an already opened symbolic link handle.
 *
 * @param handle   Symbolic link handle to query.
 * @param metadata Receives the resulting metadata.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_symlink_stat(
    struct VaFsSymlinkHandle* handle,
    struct VaFsMetadata*      metadata);

#endif //!__VAFS_STAT_H__
