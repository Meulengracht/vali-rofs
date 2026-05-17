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

#include "utils.h"

#include <errno.h>

#if defined(_WIN32) || defined(_WIN64)
#include "dirent_win32.h"
#include <direct.h>
#include <io.h>
#else
#include <sys/stat.h>
#endif

int platform_fs_mode_is_file(uint32_t mode)
{
    return S_ISREG(mode);
}

int platform_fs_mode_is_symlink(uint32_t mode)
{
    return S_ISLNK(mode);
}

int platform_fs_mode_is_directory(uint32_t mode)
{
    return S_ISDIR(mode);
}

uint32_t platform_fs_mode_permissions(uint32_t mode)
{
    return mode & 0777;
}

int platform_fs_directory_exists(const char* path)
{
    struct stat st;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (stat(path, &st)) {
        if (errno == ENOENT) {
            return 0;
        }
        return -1;
    }
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

int platform_fs_create_directory(const char* path, uint32_t permissions)
{
    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

#if defined(_WIN32) || defined(_WIN64)
    (void)permissions;
    return _mkdir(path);
#else
    return mkdir(path, (mode_t)permissions);
#endif
}

int platform_fs_chmod(const char* path, uint32_t permissions)
{
    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

#if defined(_WIN32) || defined(_WIN64)
    return _chmod(path, (int)permissions);
#else
    return chmod(path, (mode_t)permissions);
#endif
}