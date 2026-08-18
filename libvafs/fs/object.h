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

#endif //!__VAFS_FS_OBJECT_H_
