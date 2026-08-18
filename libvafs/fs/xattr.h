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

#ifndef __VAFS_FS_XATTR_H_
#define __VAFS_FS_XATTR_H_

#include <stdint.h>

// For VaFsBlockPosition_t
#include "../format/format.h"

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

#endif //!__VAFS_FS_XATTR_H_
