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
 */

#ifndef __VAFS_PLATFORM_H__
#define __VAFS_PLATFORM_H__

#include "list.h"

// include dirent.h for directory operations
#if defined(_WIN32) || defined(_WIN64)
#define __PATH_SEPARATOR '\\'
#else
#define __PATH_SEPARATOR '/'
#endif

struct platform_string_item {
    struct list_item list_header;
    const char*      value;
};

enum platform_filetype {
    PLATFORM_FILETYPE_DIRECTORY,
    PLATFORM_FILETYPE_FILE,
    PLATFORM_FILETYPE_SYMLINK,
    PLATFORM_FILETYPE_UNKNOWN
};

struct platform_file_entry {
    struct list_item       list_header;
    char*                  name;
    enum platform_filetype type;
    char*                  path;
    char*                  sub_path;
};

#define FILTER_FOLDCASE 0x1

/**
 * @brief checks a given filter against the given text
 */
extern int strfilter(const char* filter, const char* text, int flags);

/**
 * @brief Retrieves a list of all files (optionally recursive) in the given path
 * and stores them in the list. 
 * The list of entries will be in the format of list<platform_file_entry>.
 * Call platform_getfiles_destroy() on the list to clean it up and correctly
 * free resources allocated with the call.
 */
extern int platform_getfiles(const char* path, int recursive, struct list* files);
extern int platform_getfiles_destroy(struct list* files);

#endif //!__VAFS_PLATFORM_H__
