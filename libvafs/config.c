/**
 * Copyright 2022, Philip Meulengracht
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
 * - Contains the implementation of the Vali Initrd Filesystem.
 *   This filesystem is used to store the initrd of the kernel.
 */

#include "private.h"
#include <vafs/vafs.h>
#include <string.h>

void vafs_config_initialize(struct VaFsConfiguration* configuration)
{
    if (configuration == NULL) {
        return;
    }

    configuration->Architecture = VaFsArchitecture_UNKNOWN;
    configuration->BlockSize    = 0;
}

void vafs_config_set_architecture(struct VaFsConfiguration* configuration, enum VaFsArchitecture architecture)
{
    if (configuration == NULL) {
        return;
    }

    configuration->Architecture = architecture;
}

void vafs_config_set_block_size(struct VaFsConfiguration* configuration, uint32_t blockSize)
{
    if (configuration == NULL) {
        return;
    }

    if (blockSize < VA_FS_DATA_MIN_BLOCKSIZE || blockSize > VA_FS_DATA_MAX_BLOCKSIZE) {
        VAFS_ERROR("Invalid block size: %d", blockSize);
        return;
    }

    configuration->BlockSize = blockSize;
}
