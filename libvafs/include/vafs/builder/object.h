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

#ifndef __VAFS_OBJECT_BUILDER_H__
#define __VAFS_OBJECT_BUILDER_H__

#include <vafs/vafs.h>

// Forward declarations
struct VaFsObjectBuilder;

/**
 * @brief Sets or replaces one extended attribute on an in-progress object.
 *
 * Object builder handles are borrowed views owned by the builder and remain valid until the builder
 * closes or aborts.
 *
 * @param handle     Borrowed object builder handle.
 * @param name       Null-terminated attribute name.
 * @param value      Optional attribute value. May be NULL only when `valueSize` is zero.
 * @param valueSize  Size of `value` in bytes.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_object_builder_setxattr(
    struct VaFsObjectBuilder* handle,
    const char*               name,
    const void*               value,
    size_t                    valueSize);

/**
 * @brief Removes one extended attribute from an in-progress object.
 *
 * @param handle Borrowed object builder handle.
 * @param name   Null-terminated attribute name to remove.
 * @return int Returns 0 on success, -1 on failure.
 */
extern int vafs_object_builder_removexattr(
    struct VaFsObjectBuilder* handle,
    const char*               name);


#endif //!__VAFS_OBJECT_BUILDER_H__
