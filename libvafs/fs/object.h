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

#ifndef __VAFS_FS_OBJECT_H_
#define __VAFS_FS_OBJECT_H_

#include <stdint.h>

#include <vafs/stat.h>

#include "../format/format.h"

enum VaFsFileState {
    VaFsFileState_Open,
    VaFsFileState_Read,
    VaFsFileState_Write
};

struct VaFsFileHandle {
    struct VaFsFile*         File;
    enum VaFsFileState       State;
    struct VaFsStreamReader* Reader;
    uint32_t                 Position;
};

struct VaFsFile {
    struct VaFs*         VaFs;
    VaFsFileDescriptor_t Descriptor;
    const char*          Name;
    struct VaFsMetadata  Stat;
    struct VaFsXattrSet* Xattrs;
    int                  StatCached;
    int                  XattrsLoaded;
};

struct VaFsSymlink {
    struct VaFs*            VaFs;
    VaFsSymlinkDescriptor_t Descriptor;
    const char*             Name;
    const char*             Target;
    struct VaFsMetadata     Stat;
    struct VaFsXattrSet*    Xattrs;
    int                     StatCached;
    int                     XattrsLoaded;
};

struct VaFsSpecial {
    struct VaFs*            VaFs;
    VaFsSpecialDescriptor_t Descriptor;
    const char*             Name;
    struct VaFsMetadata     Stat;
    struct VaFsXattrSet*    Xattrs;
    int                     StatCached;
    int                     XattrsLoaded;
};

struct VaFsHardlink {
    struct VaFs*             VaFs;
    VaFsHardlinkDescriptor_t Descriptor;
    const char*              Name;
};

/**
 * @brief Allocates a file handle wrapper for an already-resolved file entry.
 *
 * @param[In] fileEntry File entry to attach to the new handle.
 * @return A newly allocated handle on success, otherwise `NULL` with `errno` set.
 */
extern struct VaFsFileHandle* vafs_file_create_handle(
    struct VaFsFile* fileEntry);

/**
 * @brief Destroys a file entry object and releases its owned memory.
 *
 * @param[In] file File entry to destroy.
 */
extern void vafs_file_destroy(
    struct VaFsFile* file);

/**
 * @brief Destroys a symlink entry object and releases its owned memory.
 *
 * @param[In] symlink Symlink entry to destroy.
 */
extern void vafs_symlink_destroy(
    struct VaFsSymlink* symlink);

/**
 * @brief Completes the canonical metadata fields required for one entry.
 *
 * Descriptor writers and create paths both run through this helper so entry
 * type, size, and fallback link-count behavior stay identical everywhere.
 *
 * @param[In,Out] metadata Metadata structure to normalize.
 * @param[In]     type     Final entry type.
 * @param[In]     size     Final logical size for the entry.
 */
extern void __finalize_entry_metadata(
    struct VaFsMetadata* metadata,
    enum VaFsEntryType   type,
    uint64_t             size);

/**
 * @brief Returns non-zero when the supplied entry type is one of the supported special-node kinds.
 *
 * @param[In] type Entry type to inspect.
 * @return Non-zero for supported special entry types, otherwise zero.
 */
extern int __is_special_entry_type(
    enum VaFsEntryType type);

/**
 * @brief Initializes a file descriptor from already-normalized metadata.
 *
 * @param[Out] descriptor File descriptor to initialize.
 * @param[In]  metadata   Metadata snapshot to copy into the descriptor.
 */
extern void __initialize_file_descriptor(
    VaFsFileDescriptor_t*    descriptor,
    const struct VaFsMetadata* metadata);

/**
 * @brief Initializes a symlink descriptor from already-normalized metadata.
 *
 * @param[Out] descriptor Symlink descriptor to initialize.
 * @param[In]  metadata   Metadata snapshot to copy into the descriptor.
 */
extern void __initialize_symlink_descriptor(
    VaFsSymlinkDescriptor_t*  descriptor,
    const struct VaFsMetadata* metadata);

/**
 * @brief Initializes a special-entry descriptor from already-normalized metadata.
 *
 * @param[Out] descriptor Special-entry descriptor to initialize.
 * @param[In]  metadata   Metadata snapshot to copy into the descriptor.
 */
extern void __initialize_special_descriptor(
    VaFsSpecialDescriptor_t*  descriptor,
    const struct VaFsMetadata* metadata);

#endif //!__VAFS_FS_OBJECT_H_
