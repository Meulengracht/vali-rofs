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
 * - Declares the draft backend contracts for the next-generation VaFS public API.
 */

#ifndef __VAFS_TYPES_H__
#define __VAFS_TYPES_H__

#include <stddef.h>
#include <stdint.h>
#include <vafs/platform.h>

#define VAFS_PATH_MAX 4096
#define VAFS_NAME_MAX 255

/**
 * List of builtin features for the filesystem
 * VA_FS_FEATURE_OVERVIEW   - Overview of the filesystem
 * VA_FS_FEATURE_FILTER     - Stream filter policies for descriptor/data streams
 */
#define VA_FS_FEATURE_OVERVIEW   { 0xB1382352, 0x4BC7, 0x45D2, { 0xB7, 0x59, 0x61, 0x5A, 0x42, 0xD4, 0x45, 0x2A } }
#define VA_FS_FEATURE_FILTER     { 0x99C25D91, 0xFA99, 0x4A71, { 0x9C, 0xB5, 0x96, 0x1A, 0xA9, 0x3D, 0xDF, 0xBB } }

/**
 * @brief Verbosity levels for library logging.
 */
enum VaFsLogLevel {
    VaFsLogLevel_Error,
    VaFsLogLevel_Warning,
    VaFsLogLevel_Info,
    VaFsLogLevel_Debug
};

/**
 * @brief Target architecture constraints stored in the image header.
 */
enum VaFsArchitecture {
    VaFsArchitecture_UNKNOWN = 0,
    VaFsArchitecture_X86 = 0x8086,
    VaFsArchitecture_X64 = 0x8664,
    VaFsArchitecture_ARM = 0xA12B,
    VaFsArchitecture_ARM64 = 0xAA64,
    VaFsArchitecture_RISCV32 = 0x5032,
    VaFsArchitecture_RISCV64 = 0x5064,
    VaFsArchitecture_ALL = 0xDEAD,
};

/**
 * @brief Path-resolution flags accepted by lookup-style APIs.
 */
enum VaFsLookupFlags {
    VaFsLookup_None     = 0,
    VaFsLookup_NoFollow = 1u << 0,
};

/**
 * @brief Identifies a filesystem feature.
 */
VAFS_ONDISK_STRUCT(VaFsGuid, {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
});

/**
 * @brief Common header for persisted feature payloads.
 */
VAFS_ONDISK_STRUCT(VaFsFeatureHeader, {
    struct VaFsGuid Guid;
    uint32_t        Length; // Length of the entire feature data including this header
});

/**
 * @brief Built-in overview feature describing image size and entry counts.
 */
VAFS_ONDISK_STRUCT(VaFsFeatureOverview, {
    struct VaFsFeatureHeader Header;
    uint64_t                 TotalSizeUncompressed;
    
    // Individual entry counts
    struct {
        uint32_t Files;
        uint32_t Directories;
        uint32_t Symlinks;
    } Counts;
});

/**
 * @brief Public entry kinds returned by directory enumeration.
 */
enum VaFsEntryType {
    VaFsEntryType_Unknown,
    VaFsEntryType_File,
    VaFsEntryType_Directory,
    VaFsEntryType_Symlink,
    VaFsEntryType_CharacterDevice,
    VaFsEntryType_BlockDevice,
    VaFsEntryType_Fifo,
    VaFsEntryType_Hardlink,
};

/**
 * @brief Describes a single directory entry returned by vafs_directory_read.
 */
struct VaFsEntry {
    const char*        Name;
    enum VaFsEntryType Type;
    uint64_t           ObjectId;
    uint32_t           MetadataMask;
};

/**
 * @brief Persisted encoding policy for descriptor and data streams.
 */
VAFS_ONDISK_STRUCT(VaFsFeatureEncoding, {
    struct VaFsFeatureHeader Header;
    char                     DescriptorEncoding[8];
    char                     DataEncoding[8];
});

#endif //!__VAFS_TYPES_H__
