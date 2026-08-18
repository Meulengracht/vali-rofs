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
 * - Declares the draft backend contracts for the next-generation VaFS public API.
 */

#ifndef __VAFS_BACKEND_H__
#define __VAFS_BACKEND_H__

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Reader backend operations used to open an existing image.
 *
 * Reader backends must provide stable random access and must be able to report the total image size.
 * The read path depends on these operations before the image can be considered successfully opened.
 */
struct VaFsReaderBackendOps {
    /**
     * @brief Reads bytes from an absolute image offset without mutating shared cursor state.
     *
     * @param userData      User-supplied backend context.
     * @param offset        Absolute byte offset from the start of the image.
     * @param buffer        Output buffer.
     * @param length        Maximum number of bytes to read.
     * @param bytesReadOut  Receives the number of bytes produced.
     * @return int Returns 0 on success, -1 on failure.
     */
    int (*readAt)(void* userData, uint64_t offset, void* buffer, size_t length, size_t* bytesReadOut);

    /**
     * @brief Reports the total byte length of the image.
     *
     * @param userData User-supplied backend context.
     * @param sizeOut  Receives the total image length in bytes.
     * @return int Returns 0 on success, -1 on failure.
     */
    int (*getSize)(void* userData, uint64_t* sizeOut);

    /**
     * @brief Releases backend resources when the reader closes.
     *
     * This callback is optional. When omitted, the reader treats close as a no-op for backend state.
     *
     * @param userData User-supplied backend context.
     * @return int Returns 0 on success, -1 on failure.
     */
    int (*close)(void* userData);
};

/**
 * @brief Builder backend operations used to create a new image.
 *
 * The v3 container is designed so builders can commit images through ordered writes followed by a
 * trailer and footer, so write backends do not need random-access patching for the first slice.
 */
struct VaFsBuilderBackendOps {
    /**
     * @brief Appends bytes to the output image.
     *
     * @param userData          User-supplied backend context.
     * @param buffer            Source bytes to append.
     * @param length            Number of bytes to append.
     * @param bytesWrittenOut   Receives the number of bytes accepted by the backend.
     * @return int Returns 0 on success, -1 on failure.
     */
    int (*write)(void* userData, const void* buffer, size_t length, size_t* bytesWrittenOut);

    /**
     * @brief Flushes any buffered writes to the backing store.
     *
     * This callback is optional. When omitted, the builder treats flush as a no-op.
     *
     * @param userData User-supplied backend context.
     * @return int Returns 0 on success, -1 on failure.
     */
    int (*flush)(void* userData);

    /**
     * @brief Releases backend resources when the builder closes or aborts.
     *
     * This callback is optional. When omitted, the builder treats close as a no-op for backend state.
     *
     * @param userData User-supplied backend context.
     * @return int Returns 0 on success, -1 on failure.
     */
    int (*close)(void* userData);
};

#endif //!__VAFS_BACKEND_H__