# VaFS Rootfs Metadata Requirements

This document defines the metadata a real VaFS rootfs image must preserve in order to behave like a container-grade filesystem image rather than a minimal initrd bundle.

## Status

- This is the normative metadata profile for rootfs-capable VaFS images.
- The current VaFS v1 format does not meet this profile because it only persists file or directory permission bits and symlink targets in the core descriptor set.
- Backward compatibility with legacy VaFS metadata is not required for this profile.

## Goal

The preserved metadata set must be sufficient to round-trip a root filesystem between:

- a host directory tree
- a VaFS rootfs image
- an OCI-compatible filesystem representation

without silently dropping metadata that changes runtime behavior inside a container.

## Required Vs Optional

The table below defines the minimum preservation rules.

| Metadata | Status | Requirement |
| --- | --- | --- |
| Entry type | Required | Must distinguish regular files, directories, symbolic links, character devices, block devices, and FIFOs. |
| Symlink target | Required | Must preserve the exact link target bytes. |
| UID and GID | Required for POSIX-targeted images | Must preserve exact numeric owner and group identifiers for every entry. |
| Windows ownership equivalent | Required for Windows-targeted images, optional otherwise | Must preserve owner SID, group SID, and the raw security descriptor when the image targets Windows container semantics. A synthesized POSIX uid or gid is not sufficient as the only source of truth. |
| Mode | Required | Must preserve full mode bits, including file type bits, rwx permissions, setuid, setgid, and sticky bits. |
| Modification time | Required | Must preserve `mtime` for every entry. Sub-second precision should be retained when the source filesystem exposes it. |
| Access time | Optional | May be preserved, but it is not required for OCI compatibility and should not be treated as content-significant. |
| Change time | Optional | May be preserved for audit or debugging, but it is not required for OCI compatibility and is often not stable across extraction. |
| Birth or creation time | Optional for POSIX-targeted images, required for Windows-targeted images | Must be preserved when targeting Windows semantics because OCI Windows layers define creation time handling. |
| Extended attributes | Required where supported by the source | Must preserve xattr name and value pairs losslessly, including empty values and namespace prefixes. This includes security-sensitive xattrs such as `security.capability` and LSM labels when present. |
| Hardlinks | Required | Must preserve hardlink relationships for non-directory entries so multiple paths can reference the same logical inode payload. |
| Device major or minor numbers | Required when entry type is character or block device | Must preserve device identity losslessly. |
| Directory metadata | Required | Directories must carry the same ownership, mode, `mtime`, and xattr rules as other entries. |
| OCI runtime config fields | Out of scope | `Env`, `Cmd`, `Entrypoint`, `User`, `WorkingDir`, `Volumes`, history, and similar OCI config fields are not filesystem metadata and must not be embedded as VaFS rootfs metadata. |

## Required Semantics

### Ownership

- Every persisted entry must have ownership metadata.
- POSIX-targeted images must store numeric `uid` and `gid` values exactly.
- Windows-targeted images must store the Windows ownership model explicitly rather than collapsing it into synthetic `uid` and `gid` defaults.
- If an extractor cannot apply ownership on the current host, it must still be able to read and report the stored metadata.

### Mode

- Mode must be stored as the full permission word, not only the low nine permission bits.
- File type bits are part of the required mode semantics.
- Setuid, setgid, and sticky bits are required because they affect runtime behavior in real rootfs images.

### Timestamps

- `mtime` is mandatory because OCI layer compatibility requires it.
- `atime`, `ctime`, and creation time are optional unless a platform profile makes them mandatory.
- When a timestamp cannot be applied on extraction, the extractor must fail clearly or emit an explicit partial-restore warning. Silent loss is not acceptable for required fields.

### Extended Attributes

- Xattrs must be stored losslessly as uninterpreted name plus value pairs.
- The image format must not special-case only a few namespaces and drop the rest.
- The following are especially important for container correctness and should be treated as high-priority test cases:
  - `security.capability`
  - `security.selinux`
  - `trusted.*`
  - `user.*`
  - `system.posix_acl_access`
  - `system.posix_acl_default`

### Hardlinks

- Hardlinks must be modeled explicitly rather than duplicated as separate regular files.
- A hardlinked file must preserve shared identity semantics inside the image.
- Directory hardlinks are not a supported payload type beyond normal filesystem-generated `.` and `..` semantics.

### Special Files Policy

- Character devices, block devices, and FIFOs are required payload types for Linux-style rootfs images.
- Character and block devices must preserve major and minor numbers.
- Socket nodes must not be serialized as ordinary persistent payloads for a full rootfs snapshot. The packer should reject them or omit them under an explicit policy switch because they are runtime communication endpoints, not durable image content.
- If the target platform cannot materialize special files during extraction, the image must still retain the metadata so another extractor or runtime can apply it later.

## Container-Relevant Metadata

Container correctness depends on more than plain file bytes.

The following are part of the rootfs metadata contract and therefore belong in VaFS:

- ownership
- mode bits
- `mtime`
- xattrs
- hardlink relationships
- device node identity
- symlink targets

The following are container-image metadata, not rootfs metadata, and should remain outside the VaFS filesystem image itself:

- OCI image config fields such as `Env`, `Cmd`, `Entrypoint`, `User`, `WorkingDir`, and `Volumes`
- OCI history entries
- OCI manifest annotations
- layer ordering and non-filesystem manifest metadata

If VaFS is used together with OCI artifacts, these values should live in the OCI config or manifest alongside the VaFS blob, not inside the filesystem metadata layer.

## Compatibility Strategy

Backward compatibility with the current minimal VaFS metadata model is not required.

The compatibility priority is:

1. Preserve OCI filesystem semantics without loss for required metadata.
2. Fail closed when a reader or extractor cannot understand or apply required metadata.
3. Allow legacy VaFS readers to reject rootfs-capable images rather than silently mounting them with degraded metadata.

As a result, a rootfs-capable VaFS format revision should be treated as a breaking format step rather than as a best-effort extension of the current descriptor set.

## OCI Compliance Notes

For OCI compatibility, VaFS must preserve the filesystem attributes OCI layers require for additions and modifications where the source platform supports them:

- `uid`
- `gid`
- `mode`
- `mtime`
- `xattrs`
- symlink targets
- hardlink references
- supported special file types

Additional OCI guidance for VaFS:

- A full VaFS rootfs snapshot is not the same thing as an OCI diff layer. Whiteouts and opaque whiteouts are layer-delta constructs and should only be represented when VaFS is explicitly modeling a changeset rather than a complete root filesystem.
- Windows-targeted OCI compatibility requires preserving Windows-specific metadata such as file attributes, raw security descriptors, mountpoint or junction semantics, and creation time.
- Images that cannot round-trip the required OCI filesystem attributes must not be described as OCI-compatible rootfs images.

## Implementation Direction

Because backward compatibility is not a requirement, the rootfs-capable format should treat these metadata fields as first-class image data rather than optional best-effort adornments.

At minimum, the next format revision must add first-class storage for:

- ownership per entry
- full mode words per entry
- timestamp records
- xattr records
- hardlink identity records
- special-file descriptors, including device numbers
- Windows ownership and security metadata when targeting Windows

Until those fields exist in the on-disk format, VaFS should be considered a compact read-only image format, not a complete rootfs-preserving container filesystem image format.

## Mapping Onto The Current API

The current public API is still shaped around the original initrd use case: compact descriptors, permission-only metadata, and separate file, directory, and symlink helpers.

### Current Coverage

| Requirement | Current API surface | Current status |
| --- | --- | --- |
| Basic entry type discovery | `vafs_directory_read`, `struct VaFsEntry` | Covered for file, directory, and symlink only. No special-file or hardlink entry type is exposed. |
| Symlink target | `vafs_symlink_target`, `vafs_directory_read_symlink` | Covered. |
| File size | `vafs_file_length`, `vafs_path_stat` | Covered for regular files. |
| Mode bits | `vafs_file_permissions`, `vafs_directory_permissions`, `vafs_path_stat` | Partially covered. Only stored permission bits are exposed, and symlink mode is synthesized rather than persisted. |
| UID or GID | No public API | Missing. |
| Timestamps | No public API | Missing. |
| Xattrs | No public API | Missing. |
| Hardlinks | No public API | Missing. |
| Special files | No public API | Missing. |
| Windows ownership or security metadata | No public API | Missing. |
| Image-global capability discovery | `vafs_feature_add`, `vafs_feature_query` | Present, but suitable only for image-global features, not per-entry metadata. |

### Current API Gaps

The current shape creates four practical limits:

1. `struct vafs_stat` is too small. It only reports `mode` and `size`, which is not enough for rootfs-grade metadata.
2. The create APIs are permission-centric. `vafs_directory_create_file`, `vafs_directory_create_directory`, and `vafs_directory_create_symlink` do not accept ownership, timestamps, xattrs, or special-file data.
3. Enumeration is name plus type only. `vafs_directory_read` cannot cheaply surface inode identity, link count, or metadata presence bits.
4. The feature system is the wrong granularity for per-entry metadata. Feature records work for image-wide policies or section discovery, but not for per-file ownership, xattrs, or Windows security blobs.

### Suggested API Evolution

Because API backward compatibility is not required, the next revision should replace the current public surface rather than adding wrapper APIs beside it. The concrete breaking proposal is documented in [VAFS_ROOTFS_V2_API_FORMAT_PROPOSAL.md](VAFS_ROOTFS_V2_API_FORMAT_PROPOSAL.md).

Recommended additions:

```c
struct VaFsTimestamp {
  int64_t  Seconds;
  uint32_t Nanoseconds;
};

struct VaFsDeviceId {
  uint32_t Major;
  uint32_t Minor;
};

enum VaFsMetadataMask {
  VaFsMetadata_Mode        = 1u << 0,
  VaFsMetadata_Size        = 1u << 1,
  VaFsMetadata_UidGid      = 1u << 2,
  VaFsMetadata_MTime       = 1u << 3,
  VaFsMetadata_ATime       = 1u << 4,
  VaFsMetadata_CTime       = 1u << 5,
  VaFsMetadata_BTime       = 1u << 6,
  VaFsMetadata_NLink       = 1u << 7,
  VaFsMetadata_Device      = 1u << 8,
  VaFsMetadata_XattrCount  = 1u << 9,
  VaFsMetadata_Windows     = 1u << 10
};

struct VaFsMetadata {
  uint32_t             Mask;
  uint32_t             Mode;
  uint32_t             Uid;
  uint32_t             Gid;
  uint32_t             NLink;
  uint64_t             Size;
  uint64_t             ObjectId;
  struct VaFsTimestamp MTime;
  struct VaFsTimestamp ATime;
  struct VaFsTimestamp CTime;
  struct VaFsTimestamp BirthTime;
  struct VaFsDeviceId  Device;
  uint32_t             XattrCount;
  uint32_t             WindowsAttributes;
};
```

Read-path additions:

```c
extern int vafs_path_stat(
  struct VaFs*        vafs,
  const char*         path,
  int                 followLinks,
  struct VaFsMetadata* metadata);

extern int vafs_file_stat(
  struct VaFsFileHandle* handle,
  struct VaFsMetadata*   metadata);

extern int vafs_directory_stat(
  struct VaFsDirectoryHandle* handle,
  struct VaFsMetadata*        metadata);

extern int vafs_symlink_stat(
  struct VaFsSymlinkHandle* handle,
  struct VaFsMetadata*      metadata);
```

Write-path additions:

```c
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
  const struct VaFsMetadata*  metadata);

extern int vafs_directory_create_hardlink(
  struct VaFsDirectoryHandle* handle,
  const char*                 name,
  uint64_t                    objectId);
```

Variable-length metadata should use dedicated APIs instead of forcing everything into `VaFsMetadata`:

```c
extern int vafs_path_listxattr(
  struct VaFs*  vafs,
  const char*   path,
  char*         buffer,
  size_t        bufferSize,
  size_t*       bytesWritten);

extern int vafs_path_getxattr(
  struct VaFs*  vafs,
  const char*   path,
  const char*   name,
  void*         value,
  size_t        valueSize,
  size_t*       bytesWritten);

extern int vafs_path_setxattr(
  struct VaFs*  vafs,
  const char*   path,
  const char*   name,
  const void*   value,
  size_t        valueSize);
```

### API Replacement Plan

- Replace `struct vafs_stat` with `struct VaFsMetadata`.
- Remove permission-only helper APIs such as `vafs_file_permissions` and `vafs_directory_permissions` from the v2 public surface.
- Reuse the existing top-level function names where practical, but with the richer metadata-aware signatures shown above.
- Make `vafs_directory_read` return a richer directory-entry structure that includes `ObjectId` and a metadata-present mask.
- Keep `vafs_feature_add` and `vafs_feature_query` as image-global mechanisms for rootfs profiles, platform targeting, and section layout discovery. They should not be used as per-entry metadata containers.

## Descriptor-Stream Storage Strategy

The current descriptor stream is optimized for compact traversal: directory payloads contain a small header followed by inline descriptors and variable-length name or target bytes. That shape is good for path lookup and lazy directory loading, but it becomes inefficient if large optional metadata is inlined into every entry.

The design goal for a rootfs-capable format should be:

- keep hot lookup and stat metadata in compact descriptor records
- move sparse or bulky metadata into cold sidecar records
- preserve a stable object identity for hardlinks
- avoid forcing directory traversal to read xattrs or Windows security blobs

### Hot Metadata

The following fields are needed often enough that they should remain inline in entry descriptors or immediately reachable without a second metadata fetch:

- entry type
- name length and name bytes
- object identity
- file size or child-directory pointer
- full mode word
- uid and gid for POSIX-targeted images
- `mtime`
- a metadata-present bitmask
- an optional sidecar reference for cold metadata

This keeps `lookup`, `getattr`, `access`, and most FUSE-style hot paths on the fast path.

### Cold Metadata

The following fields should live in sidecar records referenced from the hot descriptor rather than inline in every directory entry:

- `atime`
- `ctime`
- birth or creation time when not part of the active platform profile
- xattr sets
- ACLs or LSM labels when represented separately from xattrs
- Windows file attributes
- Windows owner SID and group SID
- Windows raw security descriptors
- mountpoint or junction flags

These fields are either optional, platform-specific, or large enough that inlining them would waste descriptor-stream space and pollute the block cache used for normal path traversal.

### Hardlink Storage

Hardlinks are the strongest argument for introducing a stable object identity instead of treating every directory entry as a fully independent file object.

For rootfs-capable images, every non-directory entry should have a stable `ObjectId` or inode-like identifier.

Two reasonable layouts are possible:

1. Preferred layout: directory entries reference a shared object record that owns file payload references and optional metadata.
2. Transitional layout: directory entries stay mostly self-contained, but hardlink entries become a distinct descriptor type that references another entry's `ObjectId`.

The preferred layout is cleaner because xattrs, link count, and security metadata naturally belong to the shared object, not to each individual name.

### Special Files

Special files should use dedicated descriptor or object layouts rather than overloading the regular-file descriptor shape.

At minimum, the format needs explicit representations for:

- character devices
- block devices
- FIFOs
- hardlink entries

Character and block device records must include major and minor numbers in the hot metadata, because that data is small and required whenever the entry is materialized.

### Descriptor-Stream Layout

To keep descriptor-stream access efficient, the rootfs format should logically split the descriptor stream into hot and cold regions even if both regions still live inside the same physical stream.

Recommended logical layout:

1. image header
2. image-global feature records
3. hot directory and entry descriptors
4. shared object table, if used
5. cold sidecar metadata records
6. descriptor-stream block header table

This ordering keeps path traversal concentrated in the early, compact part of the stream and pushes large optional blobs toward the cold tail of the descriptor stream.

### Serialization Strategy

The current writer flushes directory trees recursively in a single pass. That works for the minimal format, but it is a poor fit for cross-referenced object ids and optional metadata blobs.

For rootfs metadata, the writer should switch to a two-pass finalize step:

1. Walk the in-memory tree and assign object ids, compute metadata masks, detect hardlink groups, and size every hot and cold record.
2. Emit the descriptor stream using the precomputed layout so every descriptor can point directly at its sidecar records without backpatching.

This two-pass approach has three benefits:

- optional metadata can be grouped by kind or locality
- hardlink and shared-object references are known before serialization starts
- hot directory descriptors can stay compact even when cold metadata is large

### Efficient Use Of Descriptor Blocks

The current stream writer already has two properties that should be preserved:

- descriptor and data streams use different block sizes
- incompressible blocks can be stored raw with a stored-block flag

That means the rootfs design should take advantage of the existing stream machinery instead of fighting it:

- keep hot descriptors compact so small descriptor blocks remain effective for lookup-heavy workloads
- group cold metadata blobs together so their larger and less predictable payloads do not fragment hot directory blocks
- allow large xattr or Windows security records to compress when beneficial and to fall back to stored blocks when compression is ineffective

### Feature-System Role

The feature system should describe image-global layout and policy, not individual entry metadata.

For example, feature records are a good place to declare:

- that the image uses the rootfs metadata profile
- whether the image carries POSIX ownership, Windows ownership, or both
- where shared object tables or cold metadata sections begin
- whether the image represents a full rootfs snapshot or a changeset-oriented variant

They are not a good place to store xattrs, timestamps, or ownership for individual files because the feature table is global, bounded, and not aligned with directory traversal.