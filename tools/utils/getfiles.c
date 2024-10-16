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

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "utils.h"

// include dirent.h for directory operations
#if defined(_WIN32) || defined(_WIN64)
#include <dirent_win32.h>
#else
#include <dirent.h>
#endif

static char* __safe_strdup(const char* str)
{
    char* copy;
    if (str == NULL) {
        return NULL;
    }

    copy = malloc(strlen(str) + 1);
    if (copy == NULL) {
        return NULL;
    }

    strcpy(copy, str);
    return copy;
}

static char* __combine_paths(const char* path1, const char* path2)
{
    char*  combined;
    int    status;
    size_t path1Length;
    size_t path2Length;

    if (path1 == NULL) {
        return __safe_strdup(path2);
    } else if (path2 == NULL) {
        return __safe_strdup(path1);
    }

    path1Length = strlen(path1);
    path2Length = strlen(path2);

    if (path1Length == 0) {
        return __safe_strdup(path2);
    } else if (path2Length == 0) {
        return __safe_strdup(path1);
    }

    if (path2[0] == __PATH_SEPARATOR) {
        path2++;
        path2Length--;
    };

    combined = malloc(path1Length + path2Length + 2);
    if (combined == NULL) {
        return NULL;
    }

    if (path1[path1Length - 1] != __PATH_SEPARATOR) {
        status = sprintf(combined, "%s%c%s", path1, __PATH_SEPARATOR, path2);
    } else {
        status = sprintf(combined, "%s%s", path1, path2);
    }

    if (status < 0) {
        free(combined);
        return NULL;
    }
    return combined;
}

static int __add_file(const struct dirent* dp, const char* path, const char* subPath, struct list* files)
{
    struct platform_file_entry* entry;

    entry = (struct platform_file_entry*)calloc(1, sizeof(struct platform_file_entry));
    if (entry == NULL) {
        return -1;
    }

    entry->name     = __safe_strdup(dp->d_name);
    entry->path     = __safe_strdup(path);
    entry->sub_path = __safe_strdup(subPath);
    if (entry->name == NULL || entry->path == NULL) {
        free(entry->name);
        free(entry->path);
        free(entry->sub_path);
        free(entry);
        return -1;
    }

    switch (dp->d_type) {
        case DT_REG:
            entry->type = PLATFORM_FILETYPE_FILE;
            break;
        case DT_DIR: {
            entry->type = PLATFORM_FILETYPE_DIRECTORY;
        } break;
        case DT_LNK:
            entry->type = PLATFORM_FILETYPE_SYMLINK;
            break;
        default:
            entry->type = PLATFORM_FILETYPE_UNKNOWN;
            break;
    }

    list_add(files, &entry->list_header);
    return 0;
}

static int __read_directory(const char* path, const char* subPath, int recursive, struct list* files)
{
    struct dirent* dp;
    DIR*           d;
    int            status = 0;

    if (!path) {
        errno = EINVAL;
        return -1;
    }

    if ((d = opendir(path)) == NULL) {
        if (errno == ENOENT) {
            return 0;
        }
        return -1;
    }

    while ((dp = readdir(d))) {
        char* combinedPath;
        char* combinedSubPath;

        if (strcmp(dp->d_name,".") == 0 || strcmp(dp->d_name,"..") == 0) {
             continue;
        }

        combinedPath    = __combine_paths(path, dp->d_name);
        combinedSubPath = __combine_paths(subPath, dp->d_name);
        if (!combinedPath || !combinedSubPath) {
            free((void*)combinedPath);
            break;
        }

        if (recursive && dp->d_type == DT_DIR) {
            status = __read_directory(combinedPath, combinedSubPath, recursive, files);
        }
        else {
            status = __add_file(dp, combinedPath, combinedSubPath, files);
        }

        free((void*)combinedPath);
        free((void*)combinedSubPath);
        if (status) {
            break;
        }
    }
    closedir(d);
    return status;
}

int utils_getfiles(const char* path, int recursive, struct list* files)
{
    if (!path || !files) {
        errno = EINVAL;
        return -1;
    }

    return __read_directory(path, NULL, recursive, files);
}

int utils_getfiles_destroy(struct list* files)
{
    struct list_item* item;

    if (!files) {
        errno = EINVAL;
        return -1;
    }

    for (item = files->head; item != NULL;) {
        struct platform_file_entry* entry = (struct platform_file_entry*)item;
        item = item->next;

        free(entry->name);
        free(entry->path);
        free(entry->sub_path);
        free(entry);
    }
    list_init(files);
    return 0;
}
