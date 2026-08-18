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

#ifndef __VAFS_FS_DIRECTORY_H_
#define __VAFS_FS_DIRECTORY_H_

#include <vafs/stat.h>

#include "../cache/hashtable.h"
#include "../format/format.h"

// Read-mode directories start in a lightweight open state and only transition
// to loaded once their child descriptors have been materialized.
enum VaFsDirectoryState {
    VaFsDirectoryState_Open,
    VaFsDirectoryState_Loaded
};

struct VaFsDirectory {
    struct VaFs*              VaFs;
    VaFsDirectoryDescriptor_t Descriptor;
    // Only the root directory needs its own descriptor position persisted back
    // to the image header because every other directory descriptor is anchored
    // by its parent entry.
    VaFsBlockPosition_t       DescriptorPosition;
    const char*               Name;
    struct VaFsMetadata       Stat;
    struct VaFsXattrSet*      Xattrs;
    int                       StatCached;
    int                       XattrsLoaded;
};

struct VaFsDirectoryReader {
    struct VaFsDirectory       Base;
    enum VaFsDirectoryState    State;
    struct VaFsDirectoryEntry* Entries;
    struct VaFsStreamReader*   Reader;
    // Small read-mode directories use this sorted view for binary search by name.
    struct VaFsDirectoryEntry** Index;
    // Very large directories build a secondary name index to avoid binary-search overhead.
    hashtable_t                NameIndex;
    size_t                     EntryCount;
    int                        IndexDirty;
    int                        NameIndexInitialized;
};

struct VaFsDirectoryWriter {
    struct VaFsDirectory       Base;
    struct VaFsDirectoryEntry* Entries;
    // Writer mode keeps the linked list as the source of truth and rebuilds this cache on demand.
    struct VaFsDirectoryEntry** Index;
    size_t                     EntryCount;
    int                        IndexDirty;
};

struct VaFsDirectoryBuilder {
    struct VaFsDirectory* Directory;
    int                   Index;
};

#endif //!__VAFS_FS_DIRECTORY_H_
