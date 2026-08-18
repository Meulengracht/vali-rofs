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

#ifndef __VAFS_CORE_PRIVATE_H_
#define __VAFS_CORE_PRIVATE_H_

#include <stdint.h>

#include <vafs/types.h>
#include "../format/format.h"
#include "../fs/xattr.h"

// Forward declarations
struct VaFsStream;
struct VaFsStreamDevice;
struct VaFsLookupCache;

// Feature discovery stays bounded because images only carry a small set of
// global capabilities, and a fixed upper limit keeps header handling simple.
#define VA_FS_MAX_FEATURES 16

// The default block size for the descriptor stream is 8kb.
// Both descriptor and data streams currently use the same supported range.
#define VA_FS_DESCRIPTOR_BLOCK_SIZE  (8 * 1024)
#define VA_FS_DATA_MIN_BLOCKSIZE     (8 * 1024)
#define VA_FS_DATA_DEFAULT_BLOCKSIZE (128 * 1024)
#define VA_FS_DATA_MAX_BLOCKSIZE     (1024 * 1024)

// Logging macros
#define VAFS_ERROR(...)  vafs_log_message(VaFsLogLevel_Error, "libvafs: " __VA_ARGS__)
#define VAFS_WARN(...)   vafs_log_message(VaFsLogLevel_Warning, "libvafs: " __VA_ARGS__)
#define VAFS_INFO(...)   vafs_log_message(VaFsLogLevel_Info, "libvafs: " __VA_ARGS__)
#define VAFS_DEBUG(...)  vafs_log_message(VaFsLogLevel_Debug, "libvafs: " __VA_ARGS__)

// Feature GUID for the optional cold xattr section.
#define VA_FS_FEATURE_XATTRS { 0x6D0DB4A6, 0x2F7C, 0x4A8E, { 0x8D, 0x55, 0x62, 0x93, 0xB0, 0x35, 0x74, 0xE1 } }

enum VaFsMode {
    VaFsMode_Read,
    VaFsMode_Write
};

struct VaFs {
    VaFsHeader_t               Header;
    enum VaFsMode              Mode;
    struct VaFsFeatureOverview Overview;

    // Features present
    struct VaFsFeatureHeader** Features;
    int                        FeatureCount;
    
    // The file stream device
    struct VaFsStreamDevice* ImageDevice;

    // The following two streams are either tied up to the
    // the image device (reading), or to a temporary device (writing).
    struct VaFsStreamDevice* DescriptorDevice;
    struct VaFsStream*       DescriptorStream;
    struct VaFsStreamDevice* DataDevice;
    struct VaFsStream*       DataStream;

    struct VaFsDirectory*   RootDirectory;
    struct VaFsLookupCache* LookupCache;
    struct VaFsXattrStore   XattrStore;
};

// Static guids used for feature discovery and validation.
extern const struct VaFsGuid g_overviewGuid;
extern const struct VaFsGuid g_filterGuid;

/**
 * @brief Initializes the VaFS library.
 */
extern void vafs_init(void);

/**
 * @brief Emits a formatted log message through the library logging backend.
 *
 * @param[In] level  Severity level for the message.
 * @param[In] format `printf`-style format string.
 * @param[In] ...    Format arguments.
 */
extern void vafs_log_message(
    enum VaFsLogLevel level,
    const char*       format,
    ...);

#endif //!__VAFS_CORE_PRIVATE_H_
