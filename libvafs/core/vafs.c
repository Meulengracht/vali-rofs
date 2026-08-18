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
 * - Contains the implementation of the Vali Container Filesystem. maximum block size for data blocks is 1mb
 *   This filesystem is used to store the initrd of the kernel.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "crc.h"
#include "core.h"

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

