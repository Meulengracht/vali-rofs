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
 * - Contains the implementation of the Vali Initrd Filesystem. maximum block size for data blocks is 1mb
 *   This filesystem is used to store the initrd of the kernel.
 */

#include "crc.h"
#include <errno.h>
#include "private.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const struct VaFsGuid g_overviewGuid  = VA_FS_FEATURE_OVERVIEW;
const struct VaFsGuid g_filterGuid    = VA_FS_FEATURE_FILTER;
static int            g_initialized   = 0;

void vafs_init(void)
{
    // CRC state is process-global, so initialize it lazily once.
    if (g_initialized) {
        return;
    }

    crc_init();
    g_initialized = 1;
}

static int __initialize_root(
    struct VaFs* vafs)
{
    // Read mode reopens the persisted root descriptor, while write mode starts
    // from an empty in-memory root that will later be serialized. The root is
    // treated as a lazy dependency so callers can open a filesystem and only
    // materialize the tree when they actually traverse it.
    if (vafs->Mode == VaFsMode_Read) {
        return vafs_directory_open_root(vafs, &vafs->Header.RootDescriptor, &vafs->RootDirectory);
    }
    else {
        return vafs_directory_create_root(vafs, &vafs->RootDirectory);
    }
}

int __vafs_ensure_root_open(
    struct VaFs* vafs)
{
    if (vafs == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (vafs->RootDirectory != NULL) {
        return 0;
    }
    return __initialize_root(vafs);
}
