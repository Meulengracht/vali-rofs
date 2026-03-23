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
 * VaFS Filter Support
 */

#ifndef __VAFS_FILTER_H__
#define __VAFS_FILTER_H__

#include <vafs/vafs.h>

/**
 * @brief Handles filter installation for an opened VaFS image.
 *        Queries the image for filter information and installs
 *        the appropriate decompression handlers.
 *
 * @param vafs The VaFS instance
 * @return int 0 on success, -1 on error
 */
extern int __handle_filter(struct VaFs* vafs);

/**
 * @brief Install a specific filter for compression during image creation.
 *
 * @param vafs The VaFS instance
 * @param filterName The filter name ("brieflz" or "aplib")
 * @return int 0 on success, -1 on error
 */
extern int __install_filter(struct VaFs* vafs, const char* filterName);

#endif // __VAFS_FILTER_H__
