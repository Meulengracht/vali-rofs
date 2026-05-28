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
 * - Contains the implementation of the Vali Initrd Filesystem.
 *   This filesystem is used to store the initrd of the kernel.
 */

#include "private.h"
#include <vafs/vafs.h>
#include <string.h>

static void __set_block_size(
    uint32_t* target,
    uint32_t  blockSize)
{
    // Descriptor and data streams now choose sizes independently, but both use
    // the same on-disk block container and therefore share the same limits.
    if (blockSize < VA_FS_DATA_MIN_BLOCKSIZE || blockSize > VA_FS_DATA_MAX_BLOCKSIZE) {
        VAFS_ERROR("Invalid block size: %u", blockSize);
        return;
    }

    *target = blockSize;
}

void vafs_config_initialize(struct VaFsConfiguration* configuration)
{
    if (configuration == NULL) {
        return;
    }

    // Keep descriptor blocks small by default while preserving the existing
    // data-stream default for file payload throughput.
    configuration->Architecture        = VaFsArchitecture_UNKNOWN;
    configuration->DescriptorBlockSize = VA_FS_DESCRIPTOR_BLOCK_SIZE;
    configuration->DataBlockSize       = VA_FS_DATA_DEFAULT_BLOCKSIZE;
}

void vafs_config_set_architecture(struct VaFsConfiguration* configuration, enum VaFsArchitecture architecture)
{
    if (configuration == NULL) {
        return;
    }

    configuration->Architecture = architecture;
}

void vafs_config_set_descriptor_block_size(struct VaFsConfiguration* configuration, uint32_t blockSize)
{
    if (configuration == NULL) {
        return;
    }

    __set_block_size(&configuration->DescriptorBlockSize, blockSize);
}

void vafs_config_set_data_block_size(struct VaFsConfiguration* configuration, uint32_t blockSize)
{
    if (configuration == NULL) {
        return;
    }

    __set_block_size(&configuration->DataBlockSize, blockSize);
}
