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

#ifndef __VAFS_CORE_PRIVATE_H_
#define __VAFS_CORE_PRIVATE_H_

#include <stdint.h>

#include <vafs/types.h>

// Forward declarations
struct VaFsStream;
struct VaFsStreamReader;
struct VaFsStreamDevice;
struct VaFsLookupCache;
typedef uint32_t vafsblock_t;

// Image headers begin with a fixed magic so readers can reject unrelated
// files before trying to interpret any packed descriptor layout.
#define VA_FS_MAGIC       0x3144524D
// Descriptor bodies now persist full entry metadata, so older readers must
// reject these images instead of interpreting the expanded fixed records as
// variable-length name payloads.
#define VA_FS_VERSION     0x00030000

// Sentinel positions mark metadata that has not been assigned a final stream
// coordinate yet, or optional sections that are absent from an image.
#define VA_FS_INVALID_BLOCK  0xFFFF
#define VA_FS_INVALID_OFFSET 0xFFFFFFFF
#define VA_FS_INVALID_XATTR_INDEX 0xFFFFFFFFu

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

// The packed structs below define the stable on-disk image layout.
VAFS_ONDISK_STRUCT(VaFsBlockPosition, {
    vafsblock_t Index;
    uint32_t    Offset;
});

// Stream layouts are owned by the outer image header. A stream writes block
// payloads first, appends its block index, and reports the final absolute
// offsets here so the stream itself never needs to patch a forward pointer.
VAFS_ONDISK_STRUCT(VaFsStreamLayout, {
    uint32_t BlockSize;
    uint32_t DataOffset;
    uint32_t DataLength;
    uint32_t IndexOffset;
    uint32_t IndexCount;
    uint32_t Reserved;
});

VAFS_ONDISK_STRUCT(VaFsHeader, {
    uint32_t            Magic;
    uint32_t            Version;
    uint32_t            Architecture;
    uint16_t            FeatureCount;
    uint16_t            Reserved;
    uint32_t            Attributes;
    VaFsStreamLayout_t  DescriptorStream;
    VaFsStreamLayout_t  DataStream;
    VaFsBlockPosition_t RootDescriptor;
});

// Descriptor type tags identify which fixed record body follows each generic
// descriptor header in the descriptor stream.
#define VA_FS_DESCRIPTOR_TYPE_FILE      0x01
#define VA_FS_DESCRIPTOR_TYPE_DIRECTORY 0x02
#define VA_FS_DESCRIPTOR_TYPE_SYMLINK   0x03
#define VA_FS_DESCRIPTOR_TYPE_SPECIAL   0x04
#define VA_FS_DESCRIPTOR_TYPE_HARDLINK  0x05

VAFS_ONDISK_STRUCT(VaFsDescriptor, {
    uint16_t Type;
    uint16_t Length; // Length of the descriptor
});

VAFS_ONDISK_STRUCT(VaFsDescriptorTimestamp, {
    int64_t  Seconds;
    uint32_t Nanoseconds;
});

VAFS_ONDISK_STRUCT(VaFsDescriptorMetadata, {
    uint32_t                  Mask;
    uint32_t                  Mode;
    uint32_t                  Uid;
    uint32_t                  Gid;
    uint32_t                  LinkCount;
    uint32_t                  XattrCount;
    // Hot descriptors carry only the xattr-set index so common metadata reads
    // do not drag the colder variable-length xattr payload into every entry.
    uint32_t                  XattrIndex;
    uint64_t                  ObjectId;
    VaFsDescriptorTimestamp_t MTime;
    VaFsDescriptorTimestamp_t ATime;
    VaFsDescriptorTimestamp_t CTime;
    VaFsDescriptorTimestamp_t BirthTime;
    uint32_t                  DeviceMajor;
    uint32_t                  DeviceMinor;
    uint32_t                  WindowsAttributes;
});

VAFS_ONDISK_STRUCT(VaFsFileDescriptor, {
    VaFsDescriptor_t    Base;
    VaFsBlockPosition_t Data;
    uint32_t            FileLength;
    VaFsDescriptorMetadata_t Metadata;
});

VAFS_ONDISK_STRUCT(VaFsDirectoryDescriptor, {
    VaFsDescriptor_t    Base;
    VaFsBlockPosition_t Descriptor;
    VaFsDescriptorMetadata_t Metadata;
});

VAFS_ONDISK_STRUCT(VaFsDirectoryHeader, {
    uint32_t Count;
});

VAFS_ONDISK_STRUCT(VaFsSymlinkDescriptor, {
    VaFsDescriptor_t         Base;
    uint16_t                 NameLength;
    uint16_t                 TargetLength;
    VaFsDescriptorMetadata_t Metadata;
});

VAFS_ONDISK_STRUCT(VaFsSpecialDescriptor, {
    VaFsDescriptor_t         Base;
    uint16_t                 EntryType;
    uint16_t                 Reserved;
    VaFsDescriptorMetadata_t Metadata;
});

VAFS_ONDISK_STRUCT(VaFsHardlinkDescriptor, {
    VaFsDescriptor_t Base;
    uint16_t         NameLength;
    uint16_t         Reserved;
    uint64_t         ObjectId;
});

VAFS_ONDISK_STRUCT(VaFsXattrSetDescriptor, {
    uint32_t Count;
});

VAFS_ONDISK_STRUCT(VaFsXattrRecordDescriptor, {
    uint16_t NameLength;
    uint16_t Reserved;
    uint32_t ValueLength;
});

VAFS_ONDISK_STRUCT(VaFsFeatureXattrs, {
    struct VaFsFeatureHeader Header;
    // The feature anchors the cold xattr section because entry descriptors only
    // know which set they want, not where the section itself begins.
    uint32_t                 DescriptorIndex;
    uint32_t                 DescriptorOffset;
    uint32_t                 Count;
});

// Readers keep one scratch object large enough for any fixed entry-descriptor
// body and then fetch variable-length names or targets separately.
typedef union VaFsEntryDescriptorScratch {
    VaFsDescriptor_t          Base;
    VaFsFileDescriptor_t      File;
    VaFsDirectoryDescriptor_t Directory;
    VaFsSymlinkDescriptor_t   Symlink;
    VaFsSpecialDescriptor_t   Special;
    VaFsHardlinkDescriptor_t  Hardlink;
} VaFsEntryDescriptorScratch_t;

enum {
    VA_FS_MAX_DESCRIPTOR_SIZE = sizeof(VaFsEntryDescriptorScratch_t)
};

enum VaFsMode {
    VaFsMode_Read,
    VaFsMode_Write
};

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
 * @brief Materializes the read-mode root directory on first use.
 *
 * Read-mode opens leave the root unopened until a path operation actually
 * needs descriptor bytes so callers can install custom runtime filter
 * callbacks immediately after `vafs_open_*` and before the first decode.
 *
 * @param[In] vafs Filesystem instance whose root should be available.
 * @return 0 when the root is ready, otherwise -1 with `errno` set.
 */
extern int __vafs_ensure_root_open(struct VaFs* vafs);

/**
 * @brief Adds a feature record to a builder-owned image.
 *
 * Internal writer code uses this while assembling optional sections before the
 * outer image header and feature list are emitted.
 */
extern int vafs_builder_add_feature(
    struct VaFs*              vafs,
    struct VaFsFeatureHeader* feature);

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
