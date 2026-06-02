# VaFS Rootfs V2 API And Format Proposal

This document turns the rootfs metadata requirements into a concrete VaFS v2 proposal.

## Status

- Proposed
- Public API break is allowed
- On-disk format break is allowed
- VaFS v1 readers are expected to reject v2 images via version mismatch

## Design Constraints

- Preserve OCI-relevant filesystem metadata without loss
- Keep path lookup and `stat`-style operations descriptor-stream efficient
- Avoid loading xattrs or Windows security blobs during ordinary directory traversal
- Preserve shared object identity for hardlinks
- Keep the existing split between descriptor and data streams

## Public API V2

API v2 should replace the current permission-centric surface rather than layering compatibility wrappers on top.

### Header Targets

- `libvafs/include/vafs/stat.h`: replace `struct vafs_stat` with a full metadata structure
- `libvafs/include/vafs/directory.h`: make directory enumeration and creation metadata-aware
- `libvafs/include/vafs/file.h`: add file metadata queries
- `libvafs/include/vafs/symlink.h`: keep target access but use the shared metadata structure
- `libvafs/include/vafs/xattr.h`: add a new public header for xattr access

### Core Types

```c
enum VaFsNodeType {
    VaFsNodeType_Unknown = 0,
    VaFsNodeType_File,
    VaFsNodeType_Directory,
    VaFsNodeType_Symlink,
    VaFsNodeType_CharacterDevice,
    VaFsNodeType_BlockDevice,
    VaFsNodeType_Fifo,
    VaFsNodeType_Hardlink,
};

struct VaFsTimestamp {
    int64_t  Seconds;
    uint32_t Nanoseconds;
};

struct VaFsDeviceNumber {
    uint32_t Major;
    uint32_t Minor;
};

struct VaFsWindowsMetadata {
    uint32_t FileAttributes;
    uint32_t ReparseTag;
    uint32_t SecurityDescriptorLength;
};

enum VaFsMetadataMask {
    VaFsMetadata_Mode        = 1u << 0,
    VaFsMetadata_Size        = 1u << 1,
    VaFsMetadata_Uid         = 1u << 2,
    VaFsMetadata_Gid         = 1u << 3,
    VaFsMetadata_LinkCount   = 1u << 4,
    VaFsMetadata_MTime       = 1u << 5,
    VaFsMetadata_ATime       = 1u << 6,
    VaFsMetadata_CTime       = 1u << 7,
    VaFsMetadata_BirthTime   = 1u << 8,
    VaFsMetadata_Device      = 1u << 9,
    VaFsMetadata_Xattrs      = 1u << 10,
    VaFsMetadata_Windows     = 1u << 11,
};

struct VaFsMetadata {
    uint32_t                   Mask;
    enum VaFsNodeType          Type;
    uint32_t                   Mode;
    uint32_t                   Uid;
    uint32_t                   Gid;
    uint32_t                   LinkCount;
    uint64_t                   Size;
    uint64_t                   ObjectId;
    struct VaFsTimestamp       MTime;
    struct VaFsTimestamp       ATime;
    struct VaFsTimestamp       CTime;
    struct VaFsTimestamp       BirthTime;
    struct VaFsDeviceNumber    Device;
    struct VaFsWindowsMetadata Windows;
};

struct VaFsDirectoryEntry {
    const char*          Name;
    enum VaFsNodeType    Type;
    uint64_t             ObjectId;
    uint32_t             MetadataMask;
};

struct VaFsSpecialFile {
    enum VaFsNodeType    Type;
    struct VaFsDeviceNumber Device;
};
```

### Proposed `stat.h`

```c
extern int vafs_path_stat(
    struct VaFs*             vafs,
    const char*              path,
    int                      followLinks,
    struct VaFsMetadata*     metadata);

extern int vafs_file_stat(
    struct VaFsFileHandle*   handle,
    struct VaFsMetadata*     metadata);

extern int vafs_directory_stat(
    struct VaFsDirectoryHandle* handle,
    struct VaFsMetadata*        metadata);

extern int vafs_symlink_stat(
    struct VaFsSymlinkHandle* handle,
    struct VaFsMetadata*      metadata);
```

### Proposed `directory.h`

```c
extern int vafs_directory_read(
    struct VaFsDirectoryHandle* handle,
    struct VaFsDirectoryEntry*  entry);

extern int vafs_directory_create_file(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    const struct VaFsMetadata*  metadata,
    struct VaFsFileHandle**     handleOut);

extern int vafs_directory_create_directory(
    struct VaFsDirectoryHandle*  handle,
    const char*                  name,
    const struct VaFsMetadata*   metadata,
    struct VaFsDirectoryHandle** handleOut);

extern int vafs_directory_create_symlink(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    const char*                 target,
    const struct VaFsMetadata*  metadata);

extern int vafs_directory_create_special(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    const struct VaFsMetadata*  metadata,
    const struct VaFsSpecialFile* special);

extern int vafs_directory_create_hardlink(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    uint64_t                    objectId);
```

### Proposed `file.h`

```c
extern size_t vafs_file_read(
    struct VaFsFileHandle* handle,
    void*                  buffer,
    size_t                 size);

extern int vafs_file_seek(
    struct VaFsFileHandle* handle,
    long                   offset,
    int                    whence);

extern uint64_t vafs_file_length(
    struct VaFsFileHandle* handle);
```

The file data APIs remain familiar, but metadata comes from `vafs_file_stat` rather than from separate permission-only helpers.

### Proposed `symlink.h`

```c
extern int vafs_symlink_target(
    struct VaFsSymlinkHandle* handle,
    void*                     buffer,
    size_t                    size);
```

Symlink metadata comes from `vafs_symlink_stat`.

### Proposed `xattr.h`

```c
extern int vafs_path_listxattr(
    struct VaFs*     vafs,
    const char*      path,
    char*            buffer,
    size_t           bufferSize,
    size_t*          bytesWritten);

extern int vafs_path_getxattr(
    struct VaFs*     vafs,
    const char*      path,
    const char*      name,
    void*            value,
    size_t           valueSize,
    size_t*          bytesWritten);

extern int vafs_path_setxattr(
    struct VaFs*     vafs,
    const char*      path,
    const char*      name,
    const void*      value,
    size_t           valueSize);
```

### API Removal And Simplification

API v2 should remove these v1-specific concepts from the public surface:

- `struct vafs_stat`
- `vafs_file_permissions`
- `vafs_directory_permissions`
- permission-only create signatures
- enumeration that returns only name plus type

The new API should use the existing names where practical, but with the richer v2 signatures. There is no need to keep compatibility wrappers such as `statx` or `_ex` variants.

## On-Disk Format V2

Format v2 should keep the descriptor stream and data stream model, but organize descriptor-side data into explicit hot and cold sections.

### Image Header

Version mismatch is enough to reject older readers, so the existing image magic can remain unchanged while the version moves to `0x00020000`.

```c
VAFS_ONDISK_STRUCT(VaFsHeaderV2, {
    uint32_t            Magic;
    uint32_t            Version;              // 0x00020000
    uint32_t            Architecture;
    uint16_t            FeatureCount;
    uint16_t            SectionCount;
    uint32_t            Attributes;
    uint32_t            DescriptorBlockOffset;
    uint32_t            DataBlockOffset;
    VaFsBlockPosition_t RootDirectory;
    uint32_t            SectionTableOffset;   // Relative to descriptor stream start
});
```

### Section Table

```c
enum VaFsSectionTypeV2 {
    VaFsSectionType_HotDirectories = 1,
    VaFsSectionType_ObjectTable,
    VaFsSectionType_Sidecars,
    VaFsSectionType_XattrSets,
    VaFsSectionType_BlobTable,
    VaFsSectionType_WindowsSecurity,
};

VAFS_ONDISK_STRUCT(VaFsSectionDescriptorV2, {
    uint16_t Type;
    uint16_t Flags;
    uint32_t Offset;   // Relative to descriptor stream start
    uint32_t Length;
});
```

The section table makes descriptor-stream layout explicit without overloading image-global feature records with per-section offsets.

### Common Disk Types

```c
VAFS_ONDISK_STRUCT(VaFsTimestampDisk, {
    int64_t  Seconds;
    uint32_t Nanoseconds;
});

VAFS_ONDISK_STRUCT(VaFsBlobRefV2, {
    uint32_t Offset;
    uint32_t Length;
});
```

### Hot Directory Records

Directory traversal should stay hot-path friendly. Each directory payload contains only the metadata needed for lookup and ordinary `stat`-style queries.

```c
VAFS_ONDISK_STRUCT(VaFsDirectoryRecordV2, {
    uint32_t EntryCount;
    uint32_t EntryBytes;
});

VAFS_ONDISK_STRUCT(VaFsHotEntryV2, {
    uint16_t           Type;
    uint16_t           Flags;
    uint16_t           NameLength;
    uint16_t           InlineLength;
    uint32_t           MetadataMask;
    uint64_t           ObjectId;
    uint32_t           Mode;
    uint32_t           Uid;
    uint32_t           Gid;
    VaFsTimestampDisk_t MTime;
    uint32_t           ReferenceIndex;
    uint32_t           ReferenceOffset;
    uint32_t           SidecarIndex;
});
```

Hot-entry interpretation:

- For directories, `ReferenceIndex` and `ReferenceOffset` are the child directory `VaFsBlockPosition`.
- For files, symlinks, special files, and hardlinks, `ReferenceIndex` is the object-table index and `ReferenceOffset` is unused.
- `SidecarIndex` is `UINT32_MAX` when no cold metadata is present.
- `InlineLength` is optional space for short inline payloads, primarily short symlink targets.

The entry bytes are serialized as:

1. `VaFsHotEntryV2`
2. name bytes
3. optional inline payload bytes

### Object Table

The object table owns data payload references and shared object identity. This is where hardlinks converge.

```c
enum VaFsObjectKindV2 {
    VaFsObjectKind_File = 1,
    VaFsObjectKind_Symlink,
    VaFsObjectKind_CharacterDevice,
    VaFsObjectKind_BlockDevice,
    VaFsObjectKind_Fifo,
};

VAFS_ONDISK_STRUCT(VaFsObjectRecordV2, {
    uint16_t Type;
    uint16_t Flags;
    uint32_t ColdMask;
    uint64_t ObjectId;
    uint32_t LinkCount;
    uint32_t SidecarIndex;
    uint32_t Payload0;
    uint32_t Payload1;
    uint32_t Payload2;
    uint32_t Payload3;
});
```

Object payload interpretation:

- Regular file:
  - `Payload0` and `Payload1` are the data-stream `VaFsBlockPosition`
  - `Payload2` and `Payload3` form the 64-bit file size
- Symlink without inline target:
  - `Payload0` and `Payload1` are a `VaFsBlobRefV2` into the blob table
- Character or block device:
  - `Payload0` is major
  - `Payload1` is minor
- FIFO:
  - no payload words are required

Hardlink entries in directories point at an existing object-table record through `ReferenceIndex` and share the same `ObjectId`.

### Sidecar Records

Cold, sparse, or bulky metadata should be placed in sidecars instead of bloating every hot directory entry.

```c
VAFS_ONDISK_STRUCT(VaFsSidecarRecordV2, {
    uint32_t            Mask;
    uint32_t            XattrSetIndex;
    uint32_t            SecurityBlobIndex;
    uint32_t            WindowsOwnerBlobIndex;
    VaFsTimestampDisk_t ATime;
    VaFsTimestampDisk_t CTime;
    VaFsTimestampDisk_t BirthTime;
    uint32_t            WindowsFileAttributes;
    uint32_t            WindowsReparseTag;
});
```

Recommended sidecar usage:

- `ATime`, `CTime`, and `BirthTime`
- xattr references
- Windows file attributes
- Windows reparse metadata
- Windows owner, group, and security descriptor blob references

### Xattr Sets

```c
VAFS_ONDISK_STRUCT(VaFsXattrSetV2, {
    uint32_t Count;
});

VAFS_ONDISK_STRUCT(VaFsXattrRecordV2, {
    uint16_t NameLength;
    uint16_t Flags;
    uint32_t ValueLength;
});
```

Each xattr set is followed by `Count` xattr records, and each record is followed by:

1. xattr name bytes
2. xattr value bytes

The writer should deduplicate identical xattr sets by content so common labels or capability sets do not multiply descriptor-stream usage unnecessarily.

### Blob Table

The blob table stores variable-length payloads that are too large or too cold to keep inline:

- long symlink targets
- Windows owner SIDs
- Windows group SIDs
- Windows raw security descriptors
- any future opaque metadata payloads

### Read Path Expectations

The layout is designed so the common operations stay cheap:

- `lookup`: read only hot directory records
- `stat`: satisfy mode, uid, gid, size, type, object id, and `mtime` from the hot entry
- `open file`: resolve one object-table record to locate the data-stream payload
- `readlink`: use inline target bytes when present, otherwise resolve the object-table record or blob table
- `getxattr`: follow `SidecarIndex` to the xattr set only when requested

This preserves the current VaFS strength of compact, lazy descriptor traversal while allowing the richer metadata set.

### Write Path Expectations

The v2 writer should use a two-pass finalize step.

Pass 1:

- walk the tree
- assign object ids
- detect hardlink groups
- compute hot metadata masks
- build or deduplicate xattr sets and security blobs
- size every hot, object, sidecar, and blob record

Pass 2:

- emit hot directory records first
- emit object table records next
- emit sidecars and xattr sets after that
- emit blob tables last in the descriptor stream
- finalize the descriptor stream and then the data stream

This keeps the hot region compact and prevents backpatch-heavy serialization.

### Descriptor-Stream Packing Rules

To use descriptor blocks efficiently:

- keep hot directory records dense and locality-friendly
- keep object records fixed-size for predictable indexing
- group sidecars by type so cold metadata compresses well
- let large xattr or security blobs fall into later descriptor blocks where compression can help and stored-block fallback remains available

The existing stream machinery already supports separate descriptor and data block sizes plus stored raw blocks for incompressible payloads. V2 should rely on those mechanisms instead of inventing a second metadata transport.

## Recommended First Implementation Slice

The first implementation slice should focus on the minimum v2 set that unlocks Linux-style rootfs fidelity without taking on every optional feature at once:

1. replace `struct vafs_stat` with `struct VaFsMetadata`
2. extend directory enumeration with `ObjectId` and `MetadataMask`
3. add object ids, uid, gid, full mode, `mtime`, and hardlink support to the on-disk format
4. add sidecar-backed xattr storage
5. add special-file object records for character devices, block devices, and FIFOs

Windows ownership and raw security-descriptor support can follow in the next slice using the same sidecar and blob-table mechanisms.