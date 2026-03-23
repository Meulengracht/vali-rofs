# VaFS Library Architecture

This document describes the architecture of the VaFS (Vali Filesystem) library and its associated tools. VaFS is a read-only filesystem designed for Vali/MollenOS with a focus on modularity, flexibility, and extensibility.

## Table of Contents

1. [Overview](#overview)
2. [Component Relationships](#component-relationships)
3. [Stream Devices](#stream-devices)
4. [Image Open/Create Flow](#image-opencreate-flow)
5. [Traversal and Path Resolution](#traversal-and-path-resolution)
6. [FUSE Mount Support](#fuse-mount-support)
7. [Extraction Flow](#extraction-flow)

## Overview

The VaFS project consists of several key components that work together:

- **libvafs**: The core library that implements the VaFS format reading and writing
- **mkvafs**: Command-line tool for creating VaFS images from directories
- **unmkvafs**: Command-line tool for extracting VaFS images to directories
- **vafs (FUSE)**: FUSE filesystem driver for mounting VaFS images

### Architecture Layers

```
┌─────────────────────────────────────────────────────────────┐
│                    Applications                              │
│  ┌──────────┐    ┌───────────┐    ┌──────────────────┐     │
│  │  mkvafs  │    │ unmkvafs  │    │  vafs (FUSE)     │     │
│  └──────────┘    └───────────┘    └──────────────────┘     │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────┴─────────────────────────────────┐
│                      libvafs API                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │   vafs.h     │  │ directory.h  │  │   file.h     │      │
│  │   (core)     │  │  (traversal) │  │   (I/O)      │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────┴─────────────────────────────────┐
│                   Internal Components                        │
│  ┌───────────┐  ┌──────────┐  ┌─────────────────────────┐  │
│  │  Streams  │  │  Device  │  │    Block Cache          │  │
│  │           │  │  Layer   │  │                         │  │
│  └───────────┘  └──────────┘  └─────────────────────────┘  │
└───────────────────────────┬─────────────────────────────────┘
                            │
┌───────────────────────────┴─────────────────────────────────┐
│                   Storage Backends                           │
│  ┌──────────┐  ┌──────────┐  ┌────────────────────────┐    │
│  │   File   │  │  Memory  │  │  Custom (VaFsOperations)│    │
│  └──────────┘  └──────────┘  └────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

## Component Relationships

### libvafs

The core library provides:

- **Image Management**: Create and open VaFS images
- **Entry Types**: Files, directories, and symbolic links
- **Filtering**: Optional compression/encryption through encode/decode functions
- **Stream-based I/O**: Efficient block-based data access
- **Feature System**: Extensible metadata system (overview, filters, etc.)

Key files:
- `libvafs/vafs.c`: Core filesystem management (libvafs/vafs.c:1-619)
- `libvafs/private.h`: Internal data structures (libvafs/private.h:1-462)
- `libvafs/include/vafs/vafs.h`: Public API (libvafs/include/vafs/vafs.h:1-298)

### mkvafs

Creates VaFS images from filesystem directories. The tool:

1. Scans the input directory recursively
2. Creates a VaFS image with all files, directories, and symlinks
3. Supports filters for compression/encryption
4. Handles platform-specific attributes (permissions, symlinks)

Implementation: `tools/mkvafs.c`

### unmkvafs

Extracts VaFS images to filesystem directories. The tool:

1. Opens a VaFS image
2. Recursively reads all entries
3. Recreates the directory structure
4. Extracts files with proper permissions and metadata

Implementation: `tools/unmkvafs.c`

### vafs (FUSE)

FUSE driver for mounting VaFS images. Maps FUSE operations to libvafs API calls.

Implementation: `tools/vafs.c`

## Stream Devices

Stream devices provide an abstraction layer over different storage backends, enabling VaFS to work with files, memory buffers, or custom storage implementations.

### Architecture

```
┌─────────────────────────────────────────────────────────┐
│              VaFsStreamDevice                            │
│  ┌────────────────────────────────────────────────┐    │
│  │         VaFsOperations                          │    │
│  │  • seek(userData, offset, whence) -> position   │    │
│  │  • read(userData, buffer, length, bytesRead)    │    │
│  │  • write(userData, buffer, length, bytesWritten)│    │
│  │  • close(userData) [optional]                   │    │
│  └────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────┘
                        │
        ┌───────────────┼───────────────┐
        │               │               │
┌───────▼──────┐ ┌──────▼─────┐ ┌──────▼─────────┐
│ File Backend │ │Memory Buffer│ │ Custom Backend │
│   (FILE*)    │ │   (char*)   │ │  (user-defined)│
└──────────────┘ └────────────┘ └────────────────┘
```

### Stream Device Types

#### 1. File Backend

Wraps standard C file handles (`FILE*`):

```c
vafs_streamdevice_open_file(path, &device);    // Read-only
vafs_streamdevice_create_file(path, &device);  // Write mode
```

Operations map directly to `fseek()`, `fread()`, `fwrite()`, `fclose()`.

Implementation: `libvafs/streamdevice.c:124-181`

#### 2. Memory Backend

Provides in-memory buffer access:

```c
// Wrap existing buffer (read-only)
vafs_streamdevice_open_memory(buffer, size, &device);

// Create expandable buffer (write mode)
vafs_streamdevice_create_memory(blockSize, &device);
```

Memory backend maintains:
- Buffer pointer
- Capacity (total allocated size)
- Size (valid data size)
- Position (current offset)
- Ownership flag (whether to free buffer on close)

Implementation: `libvafs/streamdevice.c:183-250`

#### 3. Custom Backend

Allows users to provide their own storage implementation:

```c
struct VaFsOperations ops = {
    .seek = my_seek,
    .read = my_read,
    .write = my_write,
    .close = my_close
};
vafs_streamdevice_open_ops(&ops, userData, &device);
```

This enables scenarios like:
- Raw device access
- Network-backed storage
- Encrypted containers
- Loop-back interfaces

Implementation: `libvafs/streamdevice.c:252-271`

### Streams

Built on top of stream devices, streams provide block-based I/O with optional filtering:

```
┌──────────────────────────────────────────────────────┐
│                  VaFsStream                          │
│  ┌────────────────────────────────────────────┐     │
│  │  Stream Header (on-disk)                   │     │
│  │  • Magic: 0x314D5356 (VSM1)                │     │
│  │  • BlockSize                               │     │
│  │  • BlockHeadersOffset                      │     │
│  │  • BlockHeadersCount                       │     │
│  └────────────────────────────────────────────┘     │
│  ┌────────────────────────────────────────────┐     │
│  │  Block Headers Array                       │     │
│  │  • LengthOnDisk (compressed/encrypted)     │     │
│  │  • Offset (in stream)                      │     │
│  │  • CRC (data integrity)                    │     │
│  │  • Flags                                   │     │
│  └────────────────────────────────────────────┘     │
│  ┌────────────────────────────────────────────┐     │
│  │  Block Cache (32 blocks)                   │     │
│  └────────────────────────────────────────────┘     │
│  ┌────────────────────────────────────────────┐     │
│  │  Filter Functions                          │     │
│  │  • Encode (for writing)                    │     │
│  │  • Decode (for reading)                    │     │
│  └────────────────────────────────────────────┘     │
└──────────────────────────────────────────────────────┘
```

Streams provide:
- **Block-based organization**: Data is divided into fixed-size blocks
- **Filtering**: Optional encode/decode for compression or encryption
- **Caching**: LRU cache for frequently accessed blocks
- **CRC validation**: Data integrity checking

Implementation: `libvafs/stream.c:1-638`

### VaFS Instance Structure

Each VaFS instance uses multiple stream devices and streams:

```c
struct VaFs {
    // Header from disk
    VaFsHeader_t Header;

    // Read or write mode
    enum VaFsMode Mode;

    // Primary image device (the .vafs file or buffer)
    struct VaFsStreamDevice* ImageDevice;

    // Descriptor stream (8KB blocks, for directory structures)
    struct VaFsStreamDevice* DescriptorDevice;
    struct VaFsStream*       DescriptorStream;

    // Data stream (configurable blocks 8KB-1MB, for file contents)
    struct VaFsStreamDevice* DataDevice;
    struct VaFsStream*       DataStream;

    // Root directory entry point
    struct VaFsDirectory* RootDirectory;

    // Features and metadata
    struct VaFsFeatureHeader** Features;
    int                        FeatureCount;
    struct VaFsFeatureOverview Overview;
};
```

**Write mode**: DescriptorDevice and DataDevice point to temporary memory buffers. On close, these are copied to the ImageDevice.

**Read mode**: DescriptorDevice and DataDevice point to regions within the ImageDevice (using device offsets).

Implementation: `libvafs/private.h:131-151`

## Image Open/Create Flow

### Creating a VaFS Image

```
Application (mkvafs)
    │
    ├─► vafs_create(path, config, &vafs)
    │       │
    │       ├─► vafs_streamdevice_create_file(path, &device)
    │       │       └─► Opens file in "wb+" mode
    │       │
    │       ├─► Create temporary memory devices for descriptors/data
    │       │       ├─► vafs_streamdevice_create_memory(&descriptorDevice)
    │       │       └─► vafs_streamdevice_create_memory(&dataDevice)
    │       │
    │       ├─► Initialize streams
    │       │       ├─► vafs_stream_create(descriptorDevice, 0, 8KB, &descriptorStream)
    │       │       └─► vafs_stream_create(dataDevice, 0, blockSize, &dataStream)
    │       │
    │       ├─► Create root directory
    │       │       └─► vafs_directory_create_root(vafs, &rootDirectory)
    │       │
    │       └─► Write header placeholder
    │
    ├─► [Optional] vafs_feature_add(vafs, &filterOps)
    │       └─► Sets encode/decode functions on streams
    │
    ├─► Add directories and files
    │       ├─► vafs_directory_open(vafs, "/path", &dirHandle)
    │       ├─► vafs_directory_create_directory(dirHandle, name, perms, &subDirHandle)
    │       ├─► vafs_directory_create_file(dirHandle, name, perms, &fileHandle)
    │       │       └─► Allocates file descriptor in descriptor stream
    │       ├─► vafs_file_write(fileHandle, buffer, size)
    │       │       ├─► Writes data to data stream (with optional encoding)
    │       │       └─► Updates file descriptor with data position and length
    │       └─► vafs_file_close(fileHandle)
    │
    └─► vafs_close(vafs)
            ├─► Flush root directory to descriptor stream
            │       └─► Recursively writes all directories/files descriptors
            ├─► Finish streams (write block headers)
            │       ├─► vafs_stream_finish(descriptorStream)
            │       └─► vafs_stream_finish(dataStream)
            ├─► Copy streams to image device
            │       ├─► Copy descriptor stream to image at DescriptorBlockOffset
            │       └─► Copy data stream to image at DataBlockOffset
            ├─► Update and write final header
            └─► Close all devices
```

Key points:

1. **Two-phase write**: Data is written to memory streams first, then copied to the image on close
2. **Stream separation**: Descriptors (metadata) and data (file contents) are kept in separate streams
3. **Block allocation**: Streams automatically manage block allocation and header tables
4. **Filtering**: If filter operations are installed, data is encoded during write

Implementation: `libvafs/vafs.c:277-397` (create), `libvafs/vafs.c:541-619` (close)

### Opening a VaFS Image

```
Application (unmkvafs / FUSE)
    │
    ├─► vafs_open_file(path, &vafs)
    │       │
    │       ├─► vafs_streamdevice_open_file(path, &imageDevice)
    │       │       └─► Opens file in "rb" mode
    │       │
    │       ├─► Read and validate header
    │       │       ├─► Check magic (0x3144524D)
    │       │       ├─► Check version (0x00010000)
    │       │       └─► Validate architecture if specified
    │       │
    │       ├─► Read features from header
    │       │       └─► Loads feature metadata (overview, filters, etc.)
    │       │
    │       ├─► Open descriptor stream
    │       │       └─► vafs_stream_open(imageDevice, DescriptorBlockOffset, &descriptorStream)
    │       │               ├─► imageDevice becomes descriptorDevice
    │       │               ├─► Reads stream header from offset
    │       │               └─► Loads block headers table
    │       │
    │       ├─► Open data stream
    │       │       └─► vafs_stream_open(imageDevice, DataBlockOffset, &dataStream)
    │       │               ├─► imageDevice becomes dataDevice
    │       │               ├─► Reads stream header from offset
    │       │               └─► Loads block headers table
    │       │
    │       └─► Open root directory
    │               └─► vafs_directory_open_root(vafs, &rootPosition, &rootDirectory)
    │                       └─► Reads root directory descriptor from descriptor stream
    │
    ├─► [Optional] vafs_feature_add(vafs, &filterOps)
    │       └─► Sets decode function on streams for decompression/decryption
    │
    └─► Ready for operations (file/directory access)
```

Key points:

1. **Single device**: All streams (descriptor and data) read from the same image device at different offsets
2. **Lazy loading**: Directory contents are loaded on-demand during traversal
3. **Stream validation**: Each stream header is validated (magic number, structure)
4. **Feature detection**: Features are read from header and can be queried

Implementation: `libvafs/vafs.c:399-530` (open)

### Memory-backed Images

For in-memory operations (e.g., embedded systems, testing):

```c
// Open existing image from memory
vafs_open_memory(buffer, size, &vafs);

// Or use custom operations
struct VaFsOperations ops = { /* ... */ };
vafs_open_ops(&ops, userData, &vafs);
```

These follow the same flow as file-based images but use memory/custom backends.

Implementation: `libvafs/vafs.c:399-530`

## Traversal and Path Resolution

Path resolution in VaFS traverses the directory tree, handling symbolic links and path components.

### Path Resolution Algorithm

```
vafs_path_stat(vafs, "/usr/local/bin/app", followLinks, &stat)
    │
    ├─► Check if root path ("/")
    │       └─► Return root directory stat
    │
    ├─► Start at root directory entries
    │
    └─► Loop: Extract next path component
            │
            ├─► __vafs_pathtoken(path, token)
            │       ├─► Skip leading slashes
            │       ├─► Extract component until '/' or '\0'
            │       └─► Return "usr"
            │
            ├─► Search current directory for token
            │       │
            │       ├─► Found DIRECTORY "usr"
            │       │       ├─► If end of path: return directory stat
            │       │       └─► Else: descend into directory, continue loop
            │       │
            │       ├─► Found SYMLINK "usr"
            │       │       ├─► If !followLinks and end of path: return symlink stat
            │       │       ├─► Else: resolve symlink
            │       │       │       ├─► __vafs_resolve_symlink(base, symlinkTarget, buffer)
            │       │       │       │       ├─► Copy base path to buffer (clean up "//")
            │       │       │       │       ├─► Append symlink target
            │       │       │       │       ├─► Canonicalize: handle "./" and "../"
            │       │       │       │       └─► Return resolved path
            │       │       │       └─► Recursively call vafs_path_stat(resolvedPath)
            │       │       └─► Return result
            │       │
            │       └─► Found FILE "app"
            │               ├─► If not end of path: ENOTDIR error
            │               └─► Else: return file stat
            │
            └─► Not found: ENOENT error
```

### Directory Entry Structure

Directories are stored as linked lists of entries in memory:

```c
struct VaFsDirectoryEntry {
    int Type;  // FILE, DIRECTORY, or SYMLINK
    union {
        struct VaFsFile*      File;
        struct VaFsDirectory* Directory;
        struct VaFsSymlink*   Symlink;
    };
    struct VaFsDirectoryEntry* Link;  // Next entry in directory
};
```

### Opening Files Through Path

```
vafs_file_open(vafs, "/data/config.txt", &handle)
    │
    ├─► Parse path into tokens: ["data", "config.txt"]
    │
    ├─► Start at root directory
    │       └─► entries = vafs->RootDirectory->Entries
    │
    ├─► Process "data"
    │       ├─► Find "data" in entries
    │       ├─► Verify it's a directory
    │       └─► entries = dataDirectory->Entries
    │
    ├─► Process "config.txt"
    │       ├─► Find "config.txt" in entries
    │       ├─► Verify it's a file
    │       └─► file = matched entry
    │
    ├─► Create file handle
    │       ├─► vafs_file_create_handle(file)
    │       ├─► Open data stream at file->Descriptor.Data position
    │       └─► Return handle
    │
    └─► Ready for reading
            └─► vafs_file_read(handle, buffer, size)
                    ├─► vafs_stream_seek(stream, block, offset)
                    ├─► vafs_stream_read(stream, buffer, size, &bytesRead)
                    │       ├─► Check block cache
                    │       ├─► If not cached: read from device
                    │       │       ├─► Read block header
                    │       │       ├─► Read compressed block
                    │       │       ├─► Decode if filter present
                    │       │       ├─► Verify CRC
                    │       │       └─► Cache block
                    │       └─► Copy data from cached block
                    └─► Return bytes read
```

### Symbolic Link Resolution

Symbolic links can be:
- **Relative**: "../../bin/app" (resolved relative to symlink location)
- **Absolute**: "/usr/bin/app" (resolved from root)

The resolution algorithm:

1. Split path at symlink location: `base_path` + `remaining_path`
2. Resolve symlink target relative to `base_path`
3. Append `remaining_path` to resolved target
4. Canonicalize the result (remove `.` and `..`)
5. Recursively resolve the new path

Implementation: `libvafs/utils.c:90-158` (resolve), `libvafs/utils.c:160-264` (path_stat)

### Directory Reading

```
vafs_directory_open(vafs, "/usr/bin", &dirHandle)
    │
    ├─► Resolve path to directory entry
    │
    ├─► Create directory handle
    │       └─► dirHandle->Directory = directory entry
    │           dirHandle->Index = 0
    │
    └─► vafs_directory_read(dirHandle, &entry)
            ├─► Get entry at current index
            ├─► entry->Name = entry name
            ├─► entry->Type = entry type (FILE/DIR/SYMLINK)
            ├─► Increment index
            └─► Return 0, or -1 with ENOENT when done
```

Implementation: `libvafs/directory.c:590-734`

## FUSE Mount Support

The FUSE driver (`tools/vafs.c`) maps FUSE filesystem operations to libvafs API calls.

### FUSE Operation Mapping

```
┌──────────────────────────────────────────────────────────┐
│                   FUSE Operations                         │
└──────────────────────────────────────────────────────────┘
                          │
                          ▼
┌──────────────────────────────────────────────────────────┐
│               Operation Handlers                          │
├──────────────────────────────────────────────────────────┤
│  • open(path, fi)                                         │
│  • read(path, buffer, size, offset, fi)                   │
│  • getattr(path, stat, fi)                                │
│  • readlink(path, buffer, size)                           │
│  • opendir(path, fi)                                      │
│  • readdir(path, buffer, filler, offset, fi, flags)       │
│  • releasedir(path, fi)                                   │
│  • release(path, fi)                                      │
│  • access(path, mode)                                     │
│  • statfs(path, statvfs)                                  │
│  • lseek(path, offset, whence, fi)                        │
└──────────────────────────────────────────────────────────┘
                          │
                          ▼
┌──────────────────────────────────────────────────────────┐
│                  libvafs API                              │
├──────────────────────────────────────────────────────────┤
│  • vafs_file_open(vafs, path, &handle)                    │
│  • vafs_file_read(handle, buffer, size)                   │
│  • vafs_path_stat(vafs, path, followLinks, &stat)         │
│  • vafs_symlink_open/target/close                         │
│  • vafs_directory_open/read/close                         │
│  • vafs_file_seek/close                                   │
└──────────────────────────────────────────────────────────┘
```

### Mount Flow

```
$ vafs --image=filesystem.vafs /mnt/point

main(argc, argv)
    │
    ├─► Parse command-line options
    │       └─► Extract: image path, mount point
    │
    ├─► vafs_open_file(imagePath, &vafs)
    │       ├─► Open VaFS image
    │       ├─► Read header and features
    │       ├─► Open descriptor/data streams
    │       └─► Load root directory
    │
    ├─► [Optional] Install filter operations
    │       └─► __handle_filter(vafs)
    │               └─► vafs_feature_add(vafs, &filterOps)
    │
    └─► fuse_main(args, &operations, vafs)
            ├─► Register FUSE operations
            ├─► Mount filesystem at mount point
            └─► Enter event loop
                    │
                    └─► Process FUSE requests
                            ├─► Kernel VFS layer
                            ├─► FUSE kernel module
                            ├─► libfuse in user space
                            └─► Operation handlers
```

Implementation: `tools/vafs.c:476-522` (main)

### Operation Details

#### 1. File Open

```
FUSE: open("/path/to/file.txt", O_RDONLY)
    │
    └─► __vafs_open(path, fi)
            ├─► Validate: O_RDONLY only (read-only FS)
            ├─► vafs_file_open(vafs, path, &handle)
            │       └─► (See "Opening Files Through Path" above)
            ├─► Store handle in fi->fh
            └─► Return 0
```

Implementation: `tools/vafs.c:86-105`

#### 2. File Read

```
FUSE: read(fd, buffer, 4096, offset=8192)
    │
    └─► __vafs_read(path, buffer, count, offset, fi)
            ├─► Get handle from fi->fh
            ├─► If offset != 0: vafs_file_seek(handle, offset, SEEK_SET)
            ├─► vafs_file_read(handle, buffer, count)
            │       ├─► Seek to block containing offset
            │       ├─► Read from stream (with decode filter if present)
            │       └─► Return bytes read
            └─► Return count
```

Implementation: `tools/vafs.c:144-168`

#### 3. Get Attributes (stat)

```
FUSE: stat("/path")
    │
    └─► __vafs_getattr(path, stat, fi)
            ├─► If fi->fh exists: use file handle directly
            │       ├─► stat->st_mode = vafs_file_permissions(handle)
            │       ├─► stat->st_size = vafs_file_length(handle)
            │       └─► Return 0
            ├─► Else: use path resolution
            │       ├─► vafs_path_stat(vafs, path, followLinks=0, &vstat)
            │       ├─► stat->st_mode = vstat.mode (S_IFREG/S_IFDIR/S_IFLNK + perms)
            │       ├─► stat->st_size = vstat.size
            │       └─► Return 0
            └─► Set other fields: st_blksize=512, st_nlink=1
```

Implementation: `tools/vafs.c:181-213`

#### 4. Read Symbolic Link

```
FUSE: readlink("/path/to/symlink")
    │
    └─► __vafs_readlink(path, buffer, size)
            ├─► vafs_symlink_open(vafs, path, &handle)
            ├─► vafs_symlink_target(handle, buffer, size)
            │       └─► Copies symlink target to buffer
            ├─► vafs_symlink_close(handle)
            └─► Return 0
```

Implementation: `tools/vafs.c:223-238`

#### 5. Open Directory

```
FUSE: opendir("/path")
    │
    └─► __vafs_opendir(path, fi)
            ├─► vafs_directory_open(vafs, path, &handle)
            │       ├─► Resolve path to directory
            │       ├─► Load directory entries
            │       └─► Create directory handle
            ├─► Store handle in fi->fh
            └─► Return 0
```

Implementation: `tools/vafs.c:295-309`

#### 6. Read Directory

```
FUSE: readdir(dirfd)
    │
    └─► __vafs_readdir(path, buffer, filler, offset, fi, flags)
            ├─► Get handle from fi->fh
            ├─► filler(buffer, ".", NULL, 0, 0)
            ├─► filler(buffer, "..", NULL, 0, 0)
            ├─► Loop:
            │       ├─► vafs_directory_read(handle, &entry)
            │       │       └─► Returns next entry or ENOENT
            │       ├─► filler(buffer, entry.Name, NULL, 0, 0)
            │       └─► Continue until ENOENT
            └─► Return 0
```

Implementation: `tools/vafs.c:326-374`

#### 7. Release (Close) File

```
FUSE: close(fd)
    │
    └─► __vafs_release(path, fi)
            ├─► Get handle from fi->fh
            ├─► vafs_file_close(handle)
            │       └─► Frees file handle and associated resources
            ├─► Clear fi->fh
            └─► Return 0
```

Implementation: `tools/vafs.c:270-285`

### Complete Request Flow Example

```
User: cat /mnt/point/config.txt

    1. Kernel VFS: lookup("/mnt/point/config.txt")
           └─► FUSE: __vafs_getattr("/config.txt")
                   └─► vafs_path_stat(vafs, "/config.txt", 0, &stat)
                           └─► Returns: S_IFREG | 0644, size=1024

    2. Kernel VFS: open("/mnt/point/config.txt", O_RDONLY)
           └─► FUSE: __vafs_open("/config.txt", fi)
                   └─► vafs_file_open(vafs, "/config.txt", &handle)
                           └─► Returns handle, stored in fi->fh

    3. Kernel VFS: read(fd, buffer, 4096, offset=0)
           └─► FUSE: __vafs_read("/config.txt", buffer, 4096, 0, fi)
                   ├─► Get handle from fi->fh
                   ├─► vafs_file_read(handle, buffer, 1024)
                   │       ├─► vafs_stream_seek(dataStream, block, offset)
                   │       ├─► vafs_stream_read(dataStream, buffer, 1024)
                   │       │       ├─► Check cache for block
                   │       │       ├─► Read from device if not cached
                   │       │       ├─► Decode with filter if present
                   │       │       └─► Return data
                   │       └─► Returns 1024 bytes
                   └─► Returns 1024

    4. Kernel VFS: close(fd)
           └─► FUSE: __vafs_release("/config.txt", fi)
                   └─► vafs_file_close(handle)
                           └─► Frees handle
```

### Read-Only Enforcement

The FUSE driver enforces read-only access:

- `open()`: Rejects any flags other than O_RDONLY (tools/vafs.c:93-96)
- `write()`, `truncate()`, `unlink()`, `mkdir()`, etc.: Not implemented (return ENOSYS)
- `statfs()`: Returns ST_RDONLY flag (tools/vafs.c:420)

## Extraction Flow

The `unmkvafs` tool extracts VaFS images to filesystem directories.

### Extraction Process

```
$ unmkvafs filesystem.vafs ./output

main(argc, argv)
    │
    ├─► Parse command-line options
    │       ├─► image_path = "filesystem.vafs"
    │       ├─► out_path = "./output"
    │       └─► progress, log level options
    │
    ├─► vafs_open_file(image_path, &vafs)
    │       └─► (See "Opening a VaFS Image" above)
    │
    ├─► [Optional] Install filter operations
    │       └─► Decode filter for decompression/decryption
    │
    ├─► Create output directory
    │       └─► mkdir(out_path)
    │
    └─► __extract_directory(vafs, "/", out_path, progress)
            │
            └─► Recursive extraction:

                1. vafs_directory_open(vafs, vafs_path, &dirHandle)
                       └─► Opens directory at current VaFS path

                2. Loop: vafs_directory_read(dirHandle, &entry)
                       │
                       ├─► Entry is DIRECTORY
                       │       ├─► Create directory on disk
                       │       │       └─► mkdir(out_path/entry.Name, perms)
                       │       └─► Recursively extract:
                       │               __extract_directory(vafs,
                       │                   vafs_path/entry.Name,
                       │                   out_path/entry.Name,
                       │                   progress)
                       │
                       ├─► Entry is FILE
                       │       ├─► vafs_file_open(vafs, full_vafs_path, &fileHandle)
                       │       ├─► Create file on disk
                       │       │       └─► fopen(out_path/entry.Name, "wb")
                       │       ├─► Read and write in chunks
                       │       │       └─► Loop:
                       │       │               ├─► vafs_file_read(fileHandle, buffer, size)
                       │       │               │       └─► Reads from data stream (with decode)
                       │       │               ├─► fwrite(buffer, size, outFile)
                       │       │               └─► Update progress
                       │       ├─► vafs_file_close(fileHandle)
                       │       ├─► fclose(outFile)
                       │       └─► chmod(out_path/entry.Name, perms)
                       │
                       └─► Entry is SYMLINK
                               ├─► vafs_symlink_open(vafs, full_vafs_path, &symlinkHandle)
                               ├─► vafs_symlink_target(symlinkHandle, target, size)
                               ├─► Create symlink on disk
                               │       └─► symlink(target, out_path/entry.Name)
                               └─► vafs_symlink_close(symlinkHandle)

                3. vafs_directory_close(dirHandle)
```

### Key Features

1. **Recursive traversal**: Directories are extracted recursively, preserving structure
2. **Permissions**: File and directory permissions are preserved where possible
3. **Symbolic links**: Recreated with original targets (absolute or relative)
4. **Progress tracking**: Optional progress reporting for large archives
5. **Filtering**: Automatic decompression/decryption if filters are installed
6. **Platform support**: Cross-platform extraction (Windows symlinks using CreateSymbolicLink)

Implementation: `tools/unmkvafs.c:1-455`

### Data Flow During Extraction

```
VaFS Image File
    │
    ├─► VaFsStreamDevice (file)
    │       └─► ImageDevice
    │               │
    │               ├─► DescriptorStream (at DescriptorBlockOffset)
    │               │       ├─► Read directory descriptors
    │               │       └─► Navigate filesystem structure
    │               │
    │               └─► DataStream (at DataBlockOffset)
    │                       ├─► Read file data blocks
    │                       ├─► Decode with filter (if present)
    │                       ├─► Verify CRC
    │                       └─► Return decoded data
    │
    └─► Output to disk
            ├─► Create directories: mkdir()
            ├─► Write files: fopen(), fwrite(), fclose(), chmod()
            └─► Create symlinks: symlink() / CreateSymbolicLink()
```

### Error Handling

The extraction process handles various error conditions:

- **Missing source**: Image file not found or not readable
- **Permission denied**: Output directory not writable
- **Disk space**: Check available space before extraction
- **Corrupt data**: CRC validation fails, symlink resolution errors
- **Platform limits**: Path length limits, unsupported symlinks on Windows

---

## Summary

The VaFS library architecture provides:

1. **Layered design**: Clear separation between storage backends, streaming, and filesystem operations
2. **Flexibility**: Support for files, memory, and custom storage backends
3. **Extensibility**: Feature system for compression, encryption, and metadata
4. **Efficiency**: Block caching, stream-based I/O, lazy loading
5. **Portability**: Cross-platform support (Linux, Windows, other POSIX systems)
6. **Tool integration**: Library powers creation (mkvafs), extraction (unmkvafs), and mounting (FUSE)

The architecture enables VaFS to be used in diverse scenarios:
- Embedded systems (memory-backed images)
- Desktop systems (FUSE mounting)
- Build systems (archive creation and extraction)
- Custom scenarios (network storage, encrypted containers, etc.)
