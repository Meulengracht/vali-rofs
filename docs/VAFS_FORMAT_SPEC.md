# VaFS On-Disk Format Specification

**Version:** 1.0.0 (0x00010000)
**Date:** March 2026
**Status:** Implemented

## Table of Contents

1. [Introduction](#introduction)
2. [Overview](#overview)
3. [Image Header](#image-header)
4. [Block Addressing](#block-addressing)
5. [Descriptor and Data Streams](#descriptor-and-data-streams)
6. [Descriptor Types](#descriptor-types)
7. [Feature Records](#feature-records)
8. [Architecture Support](#architecture-support)
9. [Versioning](#versioning)
10. [Binary Layout Diagrams](#binary-layout-diagrams)

## Introduction

VaFS (Vali Filesystem) is a read-only filesystem format originally designed for Vali/MollenOS initrd images. It features:

- **Compact structure**: Efficient storage with optional compression
- **Block-based organization**: Separate descriptor and data streams with configurable block sizes
- **Extensible features**: GUID-based feature system for optional functionality
- **Platform flexibility**: Support for multiple architectures and custom storage backends
- **Integrity checks**: CRC32 validation for all block data

## Overview

A VaFS image consists of the following sequential components:

```
┌─────────────────────────────────────┐
│         VaFS Header                  │  (52 bytes typical)
├─────────────────────────────────────┤
│      Feature Records (optional)      │  (variable size)
├─────────────────────────────────────┤
│      Descriptor Stream               │  (variable size)
│  ┌─────────────────────────────┐   │
│  │  VaFsStreamHeader            │   │
│  ├─────────────────────────────┤   │
│  │  Block Data                  │   │
│  ├─────────────────────────────┤   │
│  │  Block Headers Array         │   │
│  └─────────────────────────────┘   │
├─────────────────────────────────────┤
│         Data Stream                  │  (variable size)
│  ┌─────────────────────────────┐   │
│  │  VaFsStreamHeader            │   │
│  ├─────────────────────────────┤   │
│  │  Block Data                  │   │
│  ├─────────────────────────────┤   │
│  │  Block Headers Array         │   │
│  └─────────────────────────────┘   │
└─────────────────────────────────────┘
```

## Image Header

The VaFS header is the first structure in the image and contains metadata about the filesystem.

### VaFsHeader Structure

```c
struct VaFsHeader {
    uint32_t            Magic;                  // 0x3144524D ("MRD1" little-endian)
    uint32_t            Version;                // 0x00010000 (version 1.0.0.0)
    uint32_t            Architecture;           // Target architecture code
    uint16_t            FeatureCount;           // Number of feature records
    uint16_t            Reserved;               // Reserved for future use (0)
    uint32_t            Attributes;             // Filesystem attributes/flags
    uint32_t            DescriptorBlockOffset;  // Byte offset to descriptor stream
    uint32_t            DataBlockOffset;        // Byte offset to data stream
    VaFsBlockPosition   RootDescriptor;         // Location of root directory
};
```

**Size:** Typically 36-52 bytes (platform-dependent due to structure packing)

### Field Descriptions

| Field | Type | Description |
|-------|------|-------------|
| `Magic` | `uint32_t` | Magic number identifying VaFS images. Must be `0x3144524D` |
| `Version` | `uint32_t` | Format version. Current version is `0x00010000` (1.0.0.0) |
| `Architecture` | `uint32_t` | Target architecture code (see [Architecture Support](#architecture-support)) |
| `FeatureCount` | `uint16_t` | Number of feature records following the header (max 16) |
| `Reserved` | `uint16_t` | Reserved bytes, must be zero |
| `Attributes` | `uint32_t` | Filesystem attributes and flags (currently unused) |
| `DescriptorBlockOffset` | `uint32_t` | Absolute byte offset from start of image to descriptor stream |
| `DataBlockOffset` | `uint32_t` | Absolute byte offset from start of image to data stream |
| `RootDescriptor` | `VaFsBlockPosition` | Block position of the root directory descriptor |

### Constants

```c
#define VA_FS_MAGIC                  0x3144524D  // Magic number
#define VA_FS_VERSION                0x00010000  // Version 1.0.0.0
#define VA_FS_MAX_FEATURES           16          // Maximum feature count
#define VA_FS_DESCRIPTOR_BLOCK_SIZE  (8 * 1024)  // Fixed 8KB blocks
```

## Block Addressing

VaFS uses a two-component addressing scheme to locate data within streams.

### VaFsBlockPosition Structure

```c
struct VaFsBlockPosition {
    uint32_t Index;   // Block index within the stream
    uint32_t Offset;  // Byte offset within the block
};
```

**Size:** 8 bytes

### Addressing Rules

- **Block Index**: Zero-based index into the stream's block array
- **Block Offset**: Byte offset within the uncompressed block data
- **Invalid Block**: `0xFFFF` (sentinel value)
- **Invalid Offset**: `0xFFFFFFFF` (sentinel value)

### Block Type

```c
typedef uint32_t vafsblock_t;  // 32-bit block index
```

This allows addressing up to 4,294,967,296 blocks per stream.

## Descriptor and Data Streams

VaFS organizes data into two independent block-based streams:

1. **Descriptor Stream**: Contains filesystem structure (directories, file metadata)
2. **Data Stream**: Contains actual file data

### Stream Structure

Each stream consists of:

```
┌─────────────────────────────────────┐  Offset 0
│      VaFsStreamHeader                │  (16 bytes)
├─────────────────────────────────────┤
│                                      │
│      Block Data                      │
│      (potentially compressed)        │
│                                      │
├─────────────────────────────────────┤  StreamHeader.BlockHeadersOffset
│                                      │
│      Block Headers Array             │
│      (BlockHeader[BlockHeadersCount])│
│                                      │
└─────────────────────────────────────┘
```

### VaFsStreamHeader Structure

```c
struct VaFsStreamHeader {
    uint32_t Magic;                 // 0x314D5356 ("VSM1" little-endian)
    uint32_t BlockSize;             // Uncompressed block size in bytes
    uint32_t BlockHeadersOffset;    // Offset to block headers array
    uint32_t BlockHeadersCount;     // Number of blocks in stream
};
```

**Size:** 16 bytes

### Field Descriptions

| Field | Type | Description |
|-------|------|-------------|
| `Magic` | `uint32_t` | Stream magic number. Must be `0x314D5356` |
| `BlockSize` | `uint32_t` | Size of each uncompressed block in bytes |
| `BlockHeadersOffset` | `uint32_t` | Byte offset (from stream start) to block headers array |
| `BlockHeadersCount` | `uint32_t` | Total number of blocks in this stream |

### BlockHeader Structure

Each block has a header describing its location and properties:

```c
struct BlockHeader {
    uint32_t LengthOnDisk;  // Compressed size on disk (bytes)
    uint32_t Offset;        // Offset to block data (from stream start)
    uint32_t Crc;           // CRC32 of uncompressed block data
    uint16_t Flags;         // Block flags (reserved)
    uint16_t Reserved;      // Reserved for future use
};
```

**Size:** 16 bytes per block

### Field Descriptions

| Field | Type | Description |
|-------|------|-------------|
| `LengthOnDisk` | `uint32_t` | Actual size of (potentially compressed) block data on disk |
| `Offset` | `uint32_t` | Byte offset from stream start to this block's data |
| `Crc` | `uint32_t` | CRC32 checksum of uncompressed block data |
| `Flags` | `uint16_t` | Block flags (currently unused) |
| `Reserved` | `uint16_t` | Reserved for future use |

### Block Size Constraints

#### Descriptor Stream
- **Fixed Size**: 8 KB (8,192 bytes)
- All descriptor blocks are exactly 8 KB when uncompressed

#### Data Stream
- **Minimum**: 8 KB (8,192 bytes)
- **Default**: 128 KB (131,072 bytes)
- **Maximum**: 1 MB (1,048,576 bytes)
- Configurable per image via `VaFsConfiguration.DataBlockSize`

### CRC Validation

- **Polynomial**: `0x04c11db7L` (standard CRC-32)
- **Initial Value**: `0xFFFFFFFF`
- Computed over **uncompressed** block data
- Verified when loading blocks from disk

### Stream Constants

```c
#define STREAM_MAGIC                  0x314D5356  // "VSM1"
#define STREAM_CACHE_SIZE             32          // Block cache size
#define VA_FS_DATA_MIN_BLOCKSIZE      (8 * 1024)
#define VA_FS_DATA_DEFAULT_BLOCKSIZE  (128 * 1024)
#define VA_FS_DATA_MAX_BLOCKSIZE      (1024 * 1024)
```

## Descriptor Types

VaFS supports three types of filesystem entries: files, directories, and symbolic links.

### Base Descriptor

All descriptors start with a common header:

```c
struct VaFsDescriptor {
    uint16_t Type;    // Descriptor type code
    uint16_t Length;  // Total descriptor size (including variable data)
};
```

**Size:** 4 bytes

### Descriptor Type Codes

```c
#define VA_FS_DESCRIPTOR_TYPE_FILE      0x01
#define VA_FS_DESCRIPTOR_TYPE_DIRECTORY 0x02
#define VA_FS_DESCRIPTOR_TYPE_SYMLINK   0x03
```

### File Descriptor

Describes a regular file.

```c
struct VaFsFileDescriptor {
    VaFsDescriptor    Base;         // Type = 0x01
    VaFsBlockPosition Data;         // Location of file data in data stream
    uint32_t          FileLength;   // Uncompressed file size (bytes)
    uint32_t          Permissions;  // File permissions/mode
    // Followed by: name (variable length, no null terminator)
};
```

**Fixed Size:** 20 bytes + name length

#### Field Descriptions

| Field | Type | Description |
|-------|------|-------------|
| `Base` | `VaFsDescriptor` | Common descriptor header (Type=0x01) |
| `Data` | `VaFsBlockPosition` | Block position of file data in data stream |
| `FileLength` | `uint32_t` | Uncompressed file size in bytes |
| `Permissions` | `uint32_t` | UNIX-style permission bits |

**Note:** The file name follows immediately after the fixed structure. The name length can be calculated as: `Base.Length - sizeof(VaFsFileDescriptor)`. The name is **not** null-terminated.

### Directory Descriptor

Describes a directory containing other entries.

```c
struct VaFsDirectoryDescriptor {
    VaFsDescriptor    Base;         // Type = 0x02
    VaFsBlockPosition Descriptor;   // Location of directory contents
    uint32_t          Permissions;  // Directory permissions/mode
    // Followed by: name (variable length, no null terminator)
};
```

**Fixed Size:** 16 bytes + name length

#### Field Descriptions

| Field | Type | Description |
|-------|------|-------------|
| `Base` | `VaFsDescriptor` | Common descriptor header (Type=0x02) |
| `Descriptor` | `VaFsBlockPosition` | Block position of directory contents in descriptor stream |
| `Permissions` | `uint32_t` | UNIX-style permission bits |

#### Directory Contents Structure

At the block position specified by `Descriptor`, the following structure is stored:

```c
struct VaFsDirectoryHeader {
    uint32_t Count;  // Number of entries in this directory
};
```

**Size:** 4 bytes

Following the header are `Count` consecutive descriptor structures (files, directories, or symlinks).

### Symlink Descriptor

Describes a symbolic link.

```c
struct VaFsSymlinkDescriptor {
    VaFsDescriptor Base;          // Type = 0x03
    uint16_t       NameLength;    // Length of symlink name (bytes)
    uint16_t       TargetLength;  // Length of target path (bytes)
    // Followed by: name (NameLength bytes) + target (TargetLength bytes)
};
```

**Fixed Size:** 8 bytes + NameLength + TargetLength

#### Field Descriptions

| Field | Type | Description |
|-------|------|-------------|
| `Base` | `VaFsDescriptor` | Common descriptor header (Type=0x03) |
| `NameLength` | `uint16_t` | Length of the symlink's name in bytes |
| `TargetLength` | `uint16_t` | Length of the target path in bytes |

**Note:** The symlink name immediately follows the fixed structure, followed by the target path. Neither string is null-terminated.

### Name Constraints

```c
#define VAFS_PATH_MAX  4096  // Maximum path length
#define VAFS_NAME_MAX  255   // Maximum name component length
```

## Feature Records

VaFS supports extensible features identified by GUIDs. Features are optional and provide additional metadata or functionality.

### Feature Structure

All features begin with a common header:

```c
struct VaFsFeatureHeader {
    struct VaFsGuid Guid;    // 128-bit feature identifier
    uint32_t        Length;  // Total feature size (including header)
};
```

**Size:** 20 bytes

### GUID Structure

```c
struct VaFsGuid {
    uint32_t Data1;      // 4 bytes
    uint16_t Data2;      // 2 bytes
    uint16_t Data3;      // 2 bytes
    uint8_t  Data4[8];   // 8 bytes
};
```

**Size:** 16 bytes

**Format:** Standard UUID/GUID format (RFC 4122 compatible)

### Standard Features

#### Feature Overview (VA_FS_FEATURE_OVERVIEW)

**GUID:** `{B1382352-4BC7-45D2-B759-615A42D4452A}`

Provides statistics about the filesystem contents.

```c
struct VaFsFeatureOverview {
    struct VaFsFeatureHeader Header;
    uint64_t                 TotalSizeUncompressed;  // Total uncompressed size
    struct {
        uint32_t Files;        // Number of files
        uint32_t Directories;  // Number of directories
        uint32_t Symlinks;     // Number of symlinks
    } Counts;
};
```

**Size:** 36 bytes

#### Feature Filter (VA_FS_FEATURE_FILTER)

**GUID:** `{99C25D91-FA99-4A71-9CB5-961AA93DDFBB}`

Indicates that data filtering (compression/encryption) is applied to streams.

**Note:** This feature marks the presence of filtering but does not contain the actual filter implementation. The filter operations must be provided at runtime.

#### Feature Filter Operations (VA_FS_FEATURE_FILTER_OPS)

**GUID:** `{17BC0212-7DF3-4BDD-9924-5AC813BE7249}`

Runtime-only feature (not persisted to disk) that provides filter encode/decode function pointers.

```c
struct VaFsFeatureFilterOps {
    struct VaFsFeatureHeader Header;
    VaFsFilterEncodeFunc     Encode;  // Compression function
    VaFsFilterDecodeFunc     Decode;  // Decompression function
};
```

**Note:** This feature is installed at runtime and is never written to the image.

### Filter Function Signatures

```c
typedef int(*VaFsFilterEncodeFunc)(
    void*     Input,
    uint32_t  InputLength,
    void**    Output,        // Allocated by function
    uint32_t* OutputLength
);

typedef int(*VaFsFilterDecodeFunc)(
    void*     Input,
    uint32_t  InputLength,
    void*     Output,        // Pre-allocated buffer
    uint32_t* OutputLength
);
```

### Feature Limits

- **Maximum Features**: 16 per image
- Features must be contiguous immediately following the VaFS header
- Total feature size is implicitly: `DescriptorBlockOffset - sizeof(VaFsHeader)`

## Architecture Support

VaFS images can be tagged for specific CPU architectures or marked as architecture-independent.

### Architecture Codes

```c
enum VaFsArchitecture {
    VaFsArchitecture_UNKNOWN  = 0x0000,  // Unknown/unspecified
    VaFsArchitecture_X86      = 0x8086,  // Intel x86 (32-bit)
    VaFsArchitecture_X64      = 0x8664,  // x86-64 (64-bit)
    VaFsArchitecture_ARM      = 0xA12B,  // ARM 32-bit
    VaFsArchitecture_ARM64    = 0xAA64,  // ARM 64-bit (AArch64)
    VaFsArchitecture_RISCV32  = 0x5032,  // RISC-V 32-bit
    VaFsArchitecture_RISCV64  = 0x5064,  // RISC-V 64-bit
    VaFsArchitecture_ALL      = 0xDEAD,  // All architectures
};
```

### Usage

The `Architecture` field in `VaFsHeader` allows images to be validated against the target platform. Images with `VaFsArchitecture_ALL` are platform-independent.

## Versioning

### Current Version

- **Version Number**: `0x00010000`
- **Encoding**: `0x00[major][minor][patch]` (hexadecimal BCD)
- **Interpretation**: Version 1.0.0.0
  - Major: `0x0001` (1)
  - Minor: `0x0000` (0)

### Version Validation

Images are validated during the open operation:

1. **Magic Check**: `header.Magic == 0x3144524D`
2. **Version Check**: `header.Version == 0x00010000`

If either check fails, the image is rejected.

### Future Compatibility

The current implementation requires an **exact version match**. Future versions may:

- Support backward compatibility with older versions
- Use the version field to enable/disable features
- Interpret the `Reserved` and `Attributes` fields based on version

### Assumptions

1. **Format Stability**: Version 1.0.0.0 is considered stable
2. **No Partial Compatibility**: Readers must support the exact version
3. **Feature Extensibility**: New functionality should use the feature system rather than version bumps where possible
4. **Endianness**: All multi-byte integers are stored in the host's native byte order (typically little-endian on supported platforms)

## Binary Layout Diagrams

### Complete Image Layout

```
Offset (Hex)  Content                                Size
═══════════════════════════════════════════════════════════════════
0x0000        ┌───────────────────────────────────┐
              │ Magic (0x3144524D)                │  4 bytes
              ├───────────────────────────────────┤
              │ Version (0x00010000)              │  4 bytes
              ├───────────────────────────────────┤
              │ Architecture                       │  4 bytes
              ├───────────────────────────────────┤
              │ FeatureCount | Reserved           │  2 + 2 bytes
              ├───────────────────────────────────┤
              │ Attributes                         │  4 bytes
              ├───────────────────────────────────┤
              │ DescriptorBlockOffset             │  4 bytes
              ├───────────────────────────────────┤
              │ DataBlockOffset                   │  4 bytes
              ├───────────────────────────────────┤
              │ RootDescriptor.Index              │  4 bytes
              ├───────────────────────────────────┤
              │ RootDescriptor.Offset             │  4 bytes
              └───────────────────────────────────┘  = ~36 bytes

              ┌───────────────────────────────────┐
              │ Feature Record 0                   │  20+ bytes
              │  ├─ GUID (16 bytes)                │
              │  ├─ Length (4 bytes)               │
              │  └─ Feature Data (variable)        │
              ├───────────────────────────────────┤
              │ Feature Record 1...N               │  variable
              └───────────────────────────────────┘

DescriptorBlockOffset:
              ┌───────────────────────────────────┐
              │ DESCRIPTOR STREAM                  │
              ├───────────────────────────────────┤
              │ StreamHeader.Magic (0x314D5356)   │  4 bytes
              ├───────────────────────────────────┤
              │ StreamHeader.BlockSize (8192)     │  4 bytes
              ├───────────────────────────────────┤
              │ StreamHeader.BlockHeadersOffset   │  4 bytes
              ├───────────────────────────────────┤
              │ StreamHeader.BlockHeadersCount    │  4 bytes
              └───────────────────────────────────┘  = 16 bytes

              ┌───────────────────────────────────┐
              │ Block 0 Data                       │  ≤ 8KB
              ├───────────────────────────────────┤
              │ Block 1 Data                       │  ≤ 8KB
              ├───────────────────────────────────┤
              │ ...                                │
              ├───────────────────────────────────┤
              │ Block N Data                       │  ≤ 8KB
              └───────────────────────────────────┘

              ┌───────────────────────────────────┐
              │ BlockHeaders Array                 │
              │  ┌─────────────────────────────┐  │
              │  │ Block 0 Header              │  │  16 bytes
              │  │  ├─ LengthOnDisk            │  │  4 bytes
              │  │  ├─ Offset                  │  │  4 bytes
              │  │  ├─ Crc                     │  │  4 bytes
              │  │  ├─ Flags                   │  │  2 bytes
              │  │  └─ Reserved                │  │  2 bytes
              │  ├─────────────────────────────┤  │
              │  │ Block 1 Header...           │  │  16 bytes each
              │  └─────────────────────────────┘  │
              └───────────────────────────────────┘

DataBlockOffset:
              ┌───────────────────────────────────┐
              │ DATA STREAM                        │
              ├───────────────────────────────────┤
              │ StreamHeader (same structure)     │  16 bytes
              ├───────────────────────────────────┤
              │ Block Data (8KB-1MB blocks)       │  variable
              ├───────────────────────────────────┤
              │ BlockHeaders Array                │  16 bytes × count
              └───────────────────────────────────┘

═══════════════════════════════════════════════════════════════════
```

### Descriptor Stream Block Contents

```
Block N in Descriptor Stream (uncompressed, 8KB):
┌──────────────────────────────────────────────────────────┐
│ Directory Header (if this is directory content)          │
│  ├─ Count (4 bytes)                                      │  4 bytes
│  └─                                                       │
├──────────────────────────────────────────────────────────┤
│ Descriptor Entry 0                                        │
│  ├─ Type | Length (2+2 bytes)                            │  4 bytes
│  ├─ [Type-specific fields]                               │  12-16 bytes
│  └─ Name (variable, no null term)                        │  variable
├──────────────────────────────────────────────────────────┤
│ Descriptor Entry 1...                                    │
│                                                           │
├──────────────────────────────────────────────────────────┤
│ ...                                                       │
│                                                           │
└──────────────────────────────────────────────────────────┘
```

### File Descriptor Layout

```
VaFsFileDescriptor (on-disk, variable size):
╔═══════════════════════════════════════════════════════════╗
║ Offset │ Field                │ Size     │ Description     ║
╠═══════════════════════════════════════════════════════════╣
║ +0x00  │ Base.Type            │ 2 bytes  │ 0x0001         ║
║ +0x02  │ Base.Length          │ 2 bytes  │ Total length   ║
║ +0x04  │ Data.Index           │ 4 bytes  │ Block index    ║
║ +0x08  │ Data.Offset          │ 4 bytes  │ Block offset   ║
║ +0x0C  │ FileLength           │ 4 bytes  │ File size      ║
║ +0x10  │ Permissions          │ 4 bytes  │ Mode bits      ║
║ +0x14  │ Name[0...N]          │ variable │ No null term   ║
╚═══════════════════════════════════════════════════════════╝
Total: 20 + name_length bytes
```

### Directory Descriptor Layout

```
VaFsDirectoryDescriptor (on-disk, variable size):
╔═══════════════════════════════════════════════════════════╗
║ Offset │ Field                │ Size     │ Description     ║
╠═══════════════════════════════════════════════════════════╣
║ +0x00  │ Base.Type            │ 2 bytes  │ 0x0002         ║
║ +0x02  │ Base.Length          │ 2 bytes  │ Total length   ║
║ +0x04  │ Descriptor.Index     │ 4 bytes  │ Block index    ║
║ +0x08  │ Descriptor.Offset    │ 4 bytes  │ Block offset   ║
║ +0x0C  │ Permissions          │ 4 bytes  │ Mode bits      ║
║ +0x10  │ Name[0...N]          │ variable │ No null term   ║
╚═══════════════════════════════════════════════════════════╝
Total: 16 + name_length bytes
```

### Symlink Descriptor Layout

```
VaFsSymlinkDescriptor (on-disk, variable size):
╔═══════════════════════════════════════════════════════════╗
║ Offset │ Field                │ Size     │ Description     ║
╠═══════════════════════════════════════════════════════════╣
║ +0x00  │ Base.Type            │ 2 bytes  │ 0x0003         ║
║ +0x02  │ Base.Length          │ 2 bytes  │ Total length   ║
║ +0x04  │ NameLength           │ 2 bytes  │ Name size      ║
║ +0x06  │ TargetLength         │ 2 bytes  │ Target size    ║
║ +0x08  │ Name[0...N]          │ variable │ No null term   ║
║ +0x08+N│ Target[0...M]        │ variable │ No null term   ║
╚═══════════════════════════════════════════════════════════╝
Total: 8 + name_length + target_length bytes
```

### Block Position Encoding

```
VaFsBlockPosition (8 bytes):
┌─────────────────────┬─────────────────────┐
│   Index (4 bytes)   │   Offset (4 bytes)  │
│   Block number      │   Byte in block     │
└─────────────────────┴─────────────────────┘

Example: Block 5, Offset 1024
┌─────────────────────┬─────────────────────┐
│     0x00000005      │     0x00000400      │
└─────────────────────┴─────────────────────┘
```

### Feature Record Layout

```
VaFsFeatureOverview (36 bytes):
╔═══════════════════════════════════════════════════════════╗
║ Offset │ Field                │ Size     │ Value          ║
╠═══════════════════════════════════════════════════════════╣
║ +0x00  │ Header.Guid.Data1    │ 4 bytes  │ 0xB1382352    ║
║ +0x04  │ Header.Guid.Data2    │ 2 bytes  │ 0x4BC7        ║
║ +0x06  │ Header.Guid.Data3    │ 2 bytes  │ 0x45D2        ║
║ +0x08  │ Header.Guid.Data4    │ 8 bytes  │ {...}         ║
║ +0x10  │ Header.Length        │ 4 bytes  │ 36            ║
║ +0x14  │ TotalSizeUncompressed│ 8 bytes  │ Variable      ║
║ +0x1C  │ Counts.Files         │ 4 bytes  │ Variable      ║
║ +0x20  │ Counts.Directories   │ 4 bytes  │ Variable      ║
║ +0x24  │ Counts.Symlinks      │ 4 bytes  │ Variable      ║
╚═══════════════════════════════════════════════════════════╝
```

## Implementation Notes

### Platform Considerations

1. **Structure Packing**: All on-disk structures use `VAFS_ONDISK_STRUCT` macro to ensure consistent packing across platforms
2. **Endianness**: The current implementation assumes native byte order (typically little-endian)
3. **Alignment**: Structure alignment may vary by platform; always use provided structure definitions

### Reading Algorithm

To read a file from a VaFS image:

1. Parse `VaFsHeader` at offset 0
2. Validate magic number and version
3. Load descriptor stream from `DescriptorBlockOffset`
4. Navigate to root using `RootDescriptor` block position
5. Traverse directory hierarchy using `VaFsDirectoryDescriptor.Descriptor` positions
6. For target file, read `VaFsFileDescriptor`
7. Load file data from data stream at `Data` block position

### Writing Algorithm

To create a VaFS image:

1. Initialize `VaFsHeader` with magic, version, and architecture
2. Write features immediately after header
3. Create descriptor stream with 8KB blocks
4. Create data stream with configured block size (8KB-1MB)
5. Write directory structure to descriptor stream
6. Write file data to data stream
7. Finalize streams (write block headers)
8. Update header with stream offsets and root position

### Validation Checklist

When implementing a VaFS reader/writer:

- ✓ Verify magic number (0x3144524D)
- ✓ Verify version (0x00010000)
- ✓ Verify stream magic (0x314D5356)
- ✓ Validate block indices are within range
- ✓ Validate block offsets are within block size
- ✓ Verify CRC32 of uncompressed blocks
- ✓ Respect descriptor block size (8KB)
- ✓ Respect data block size constraints (8KB-1MB)
- ✓ Handle variable-length names correctly
- ✓ Do not expect null terminators on names

## Reference Implementation

The reference implementation is available in the `libvafs` directory of this repository:

- `libvafs/private.h` - On-disk structure definitions
- `libvafs/vafs.h` - Public API and feature definitions
- `libvafs/stream.c` - Stream implementation
- `libvafs/directory.c` - Directory handling

## Appendix: Constants Summary

```c
// Magic numbers
#define VA_FS_MAGIC              0x3144524D  // "MRD1"
#define VA_FS_VERSION            0x00010000  // Version 1.0.0.0
#define STREAM_MAGIC             0x314D5356  // "VSM1"

// Descriptor types
#define VA_FS_DESCRIPTOR_TYPE_FILE      0x01
#define VA_FS_DESCRIPTOR_TYPE_DIRECTORY 0x02
#define VA_FS_DESCRIPTOR_TYPE_SYMLINK   0x03

// Block sizes
#define VA_FS_DESCRIPTOR_BLOCK_SIZE  (8 * 1024)     // 8KB
#define VA_FS_DATA_MIN_BLOCKSIZE     (8 * 1024)     // 8KB
#define VA_FS_DATA_DEFAULT_BLOCKSIZE (128 * 1024)   // 128KB
#define VA_FS_DATA_MAX_BLOCKSIZE     (1024 * 1024)  // 1MB

// Limits
#define VA_FS_MAX_FEATURES       16      // Max feature records
#define VA_FS_INVALID_BLOCK      0xFFFF
#define VA_FS_INVALID_OFFSET     0xFFFFFFFF
#define VAFS_PATH_MAX            4096    // Max path length
#define VAFS_NAME_MAX            255     // Max name length

// CRC
#define CRC_BEGIN                0xFFFFFFFF
#define POLYNOMIAL               0x04c11db7L
```

---

**End of Specification**
