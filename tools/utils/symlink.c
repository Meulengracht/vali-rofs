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
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#include <assert.h>
#include <sys/stat.h>

#include "dirent_win32.h"
#include "ntifs_win32.h"

static sRtlNtStatusToDosError        pRtlNtStatusToDosError;
static sNtQueryInformationFile       pNtQueryInformationFile;
static sNtQueryVolumeInformationFile pNtQueryVolumeInformationFile;
static HMODULE                       hNtdll;

int symlink_utils_init(void)
{
    hNtdll = GetModuleHandleA("ntdll.dll");
    if (hNtdll == NULL) {
        return -1;
    }

    pRtlNtStatusToDosError = (sRtlNtStatusToDosError)GetProcAddress(
        hNtdll,
        "RtlNtStatusToDosError");
    if (pRtlNtStatusToDosError == NULL) {
        return -1;
    }

    pNtQueryInformationFile = (sNtQueryInformationFile)GetProcAddress(
        hNtdll,
        "NtQueryInformationFile");
    if (pNtQueryInformationFile == NULL) {
        return -1;
    }

    pNtQueryVolumeInformationFile = (sNtQueryVolumeInformationFile)
        GetProcAddress(hNtdll, "NtQueryVolumeInformationFile");
    if (pNtQueryVolumeInformationFile == NULL) {
        return -1;
    }
    return 0;
}

void symlink_utils_cleanup(void)
{
    if (hNtdll != NULL) {
        FreeLibrary(hNtdll);
        hNtdll = NULL;
    }
}

static int __readlink_handle(HANDLE handle, char** symlinkBufferOut, uint64_t* symlinkLengthOut)
{
    REPARSE_DATA_BUFFER* reparse_data;
    WCHAR*               w_target;
    DWORD                w_target_len;
    char*                target;
    int                  target_len = 0;
    DWORD                bytes;
    char*                buffer;
    int                  status = -1;

    buffer = malloc(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
    if (buffer == NULL) {
        errno = ENOMEM;
        return -1;
    }

    if (!DeviceIoControl(handle, FSCTL_GET_REPARSE_POINT, NULL, 0, buffer,
            MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &bytes, NULL)) {
        errno = EIO;
        goto cleanup;
    }

    reparse_data = (REPARSE_DATA_BUFFER*)buffer;
    if (reparse_data->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
        w_target = reparse_data->SymbolicLinkReparseBuffer.PathBuffer +
            (reparse_data->SymbolicLinkReparseBuffer.SubstituteNameOffset /
            sizeof(WCHAR));
        w_target_len =
            reparse_data->SymbolicLinkReparseBuffer.SubstituteNameLength /
            sizeof(WCHAR);

        if (w_target_len >= 4 &&
            w_target[0] == L'\\' &&
            w_target[1] == L'?' &&
            w_target[2] == L'?' &&
            w_target[3] == L'\\') {
            if (w_target_len >= 6 &&
                ((w_target[4] >= L'A' && w_target[4] <= L'Z') ||
                (w_target[4] >= L'a' && w_target[4] <= L'z')) &&
                w_target[5] == L':' &&
                (w_target_len == 6 || w_target[6] == L'\\')) {
                w_target += 4;
                w_target_len -= 4;

            } else if (w_target_len >= 8 &&
                        (w_target[4] == L'U' || w_target[4] == L'u') &&
                        (w_target[5] == L'N' || w_target[5] == L'n') &&
                        (w_target[6] == L'C' || w_target[6] == L'c') &&
                        w_target[7] == L'\\') {
                w_target += 6;
                w_target[0] = L'\\';
                w_target_len -= 6;
            }
        }
    } else if (reparse_data->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
        w_target = reparse_data->MountPointReparseBuffer.PathBuffer +
            (reparse_data->MountPointReparseBuffer.SubstituteNameOffset /
            sizeof(WCHAR));
        w_target_len = reparse_data->MountPointReparseBuffer.SubstituteNameLength /
            sizeof(WCHAR);

        if (!(w_target_len >= 6 &&
            w_target[0] == L'\\' &&
            w_target[1] == L'?' &&
            w_target[2] == L'?' &&
            w_target[3] == L'\\' &&
            ((w_target[4] >= L'A' && w_target[4] <= L'Z') ||
            (w_target[4] >= L'a' && w_target[4] <= L'z')) &&
            w_target[5] == L':' &&
            (w_target_len == 6 || w_target[6] == L'\\'))) {
            errno = ENOTSUP;
            goto cleanup;
        }

        w_target += 4;
        w_target_len -= 4;
    } else if (reparse_data->ReparseTag == IO_REPARSE_TAG_APPEXECLINK) {
        w_target = reparse_data->AppExecLinkReparseBuffer.StringList;
        for (int i = 0; i < 2; ++i) {
            size_t len = wcslen(w_target);
            if (len == 0) {
                errno = ENOTSUP;
                goto cleanup;
            }
            w_target += len + 1;
        }
        w_target_len = (DWORD)wcslen(w_target);
        if (w_target_len == 0) {
            errno = ENOTSUP;
            goto cleanup;
        }
        if (!(w_target_len >= 3 &&
            ((w_target[0] >= L'a' && w_target[0] <= L'z') ||
            (w_target[0] >= L'A' && w_target[0] <= L'Z')) &&
            w_target[1] == L':' &&
            w_target[2] == L'\\')) {
            errno = ENOTSUP;
            goto cleanup;
        }

    } else {
        errno = ENOTSUP;
        goto cleanup;
    }

    if (symlinkBufferOut != NULL || symlinkLengthOut != NULL) {
        target_len = WideCharToMultiByte(CP_UTF8,
            0,
            w_target,
            w_target_len,
            NULL,
            0,
            NULL,
            NULL);
        if (target_len == 0) {
            goto cleanup;
        }
    }

    if (symlinkBufferOut != NULL) {
        int r;
        target = (char*)malloc(target_len + 1);
        if (target == NULL) {
            errno = ENOMEM;
            goto cleanup;
        }

        r = WideCharToMultiByte(CP_UTF8,
            0,
            w_target,
            w_target_len,
            target,
            target_len,
            NULL,
            NULL);
        assert(r == target_len);
        target[target_len] = '\0';

        *symlinkBufferOut = target;
    }

    if (symlinkLengthOut != NULL) {
        *symlinkLengthOut = target_len;
    }

    status = 0;

cleanup:
    free(buffer);
    return status;
}

static int __stat_handle(HANDLE handle, struct stat* statbuf)
{
    FILE_ALL_INFORMATION       fileInformation;
    FILE_FS_VOLUME_INFORMATION volumeInformation;
    NTSTATUS                   ntStatus;
    IO_STATUS_BLOCK            ioStatus;

    ntStatus = pNtQueryInformationFile(handle,
        &ioStatus,
        &fileInformation,
        sizeof(FILE_ALL_INFORMATION),
        FileAllInformation
    );
    if (NT_ERROR(ntStatus)) {
        SetLastError(pRtlNtStatusToDosError(ntStatus));
        errno = ENOSYS;
        return -1;
    }

    ntStatus = pNtQueryVolumeInformationFile(handle,
        &ioStatus,
        &volumeInformation,
        sizeof(FILE_FS_VOLUME_INFORMATION),
        FileFsVolumeInformation
    );
    if (ioStatus.Status == STATUS_NOT_IMPLEMENTED) {
        statbuf->st_dev = 0;
    } else if (NT_ERROR(ntStatus)) {
        SetLastError(pRtlNtStatusToDosError(ntStatus));
        errno = ENOSYS;
        return -1;
    } else {
        statbuf->st_dev = volumeInformation.VolumeSerialNumber;
    }

    statbuf->st_gid = 0;
    statbuf->st_uid = 0;
    statbuf->st_rdev = 0;

    statbuf->st_mode = 0;
    if (fileInformation.BasicInformation.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        uint64_t targetSize;

        statbuf->st_mode |= S_IFLNK;
        if (__readlink_handle(handle, NULL, &targetSize) != 0) {
            return -1;
        }
        statbuf->st_size = (_off_t)targetSize;
    }
    else if (fileInformation.BasicInformation.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        statbuf->st_mode |= _S_IFDIR;
        statbuf->st_size = 0;
    }
    else {
        statbuf->st_mode |= _S_IFREG;
        statbuf->st_size = (_off_t)fileInformation.StandardInformation.EndOfFile.QuadPart;
    }

    if (fileInformation.BasicInformation.FileAttributes & FILE_ATTRIBUTE_READONLY) {
        statbuf->st_mode |= _S_IREAD | (_S_IREAD >> 3) | (_S_IREAD >> 6);
    } else {
        statbuf->st_mode |= (_S_IREAD | _S_IWRITE) | ((_S_IREAD | _S_IWRITE) >> 3) |
        ((_S_IREAD | _S_IWRITE) >> 6);
    }

    statbuf->st_ino   = (_ino_t)fileInformation.InternalInformation.IndexNumber.QuadPart;
    statbuf->st_nlink = (short)fileInformation.StandardInformation.NumberOfLinks;
    return 0;
}

static int __stat(const char* path, struct stat* statbuf, int openSymlink)
{
    HANDLE handle;
    DWORD  flags;
    int    status;

    flags = FILE_FLAG_BACKUP_SEMANTICS;
    if (openSymlink) {
        flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    }

    handle = CreateFileA(path,
        FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        flags,
        NULL
    );
    if (handle == INVALID_HANDLE_VALUE) {
        errno = ENOENT;
        return -1;
    }

    status = __stat_handle(handle, statbuf);
    CloseHandle(handle);

    if (status) {
        DWORD error = GetLastError();

        if (openSymlink && error == ERROR_SYMLINK_NOT_SUPPORTED) {
            return __stat(path, statbuf, 0);
        }
    }
    return status;
}

static int __readlink(const char* path, char* linkBuffer, size_t maxLength)
{
    HANDLE   handle;
    uint64_t targetLength = maxLength;
    int      status;
    char*    linkBufferResult;

    handle = CreateFileA(path, 0, 0, NULL,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );
    if (handle == INVALID_HANDLE_VALUE) {
        errno = ENOENT;
        return -1;
    }

    status = __readlink_handle(handle, &linkBufferResult, &targetLength);
    if (!status) {
        strncpy(linkBuffer, linkBufferResult, maxLength);
        free(linkBufferResult);
    }
    CloseHandle(handle);
    return status;
}

int symlink_utils_create(const char* target, const char* path)
{
    int status;

    if (target == NULL || path == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = CreateSymbolicLinkA(path, target, 0);
    if (status == FALSE) {
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            return 0;
        }
        return -1;
    }
    return 0;
}

int symlink_utils_read(const char* path, char** bufferOut)
{
    char* buffer;

    if (path == NULL || bufferOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    buffer = (char*)malloc(1024);
    if (buffer == NULL) {
        errno = ENOMEM;
        return -1;
    }

    if (__readlink(path, buffer, 1024) == -1) {
        free(buffer);
        return -1;
    }

    *bufferOut = buffer;
    return 0;
}

int symlink_utils_ministat(const char* path, uint32_t* filemode)
{
    struct stat st;

    if (__stat(path, &st, 0) != 0) {
        return -1;
    }

    *filemode = st.st_mode;
    return 0;
}

char* symlink_utils_abspath(const char* path)
{
    char* fullPath;
    DWORD result;

    fullPath = calloc(1, MAX_PATH);
    if (fullPath == NULL) {
        return NULL;
    }

    result = GetFullPathName(path, MAX_PATH, fullPath, NULL);
    if (!result) {
        free(fullPath);
        return NULL;
    }
    return fullPath;
}

#else

#include <linux/limits.h>
#include <sys/stat.h>
#include <unistd.h>

int symlink_utils_init(void)
{
    return 0;
}

void symlink_utils_cleanup(void)
{
}

int symlink_utils_create(const char* target, const char* path)
{
    int status;

    if (target == NULL || path == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = symlink(target, path);
    if (status) {
        if (errno == EEXIST) {
            return 0;
        }
        return -1;
    }
    return 0;
}

int symlink_utils_read(const char* path, char** bufferOut)
{
    char* buffer;

    if (path == NULL || bufferOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    buffer = (char*)calloc(1, PATH_MAX);
    if (buffer == NULL) {
        errno = ENOMEM;
    }

    if (readlink(path, buffer, PATH_MAX - 1) == -1) {
        free(buffer);
        return -1;
    }

    *bufferOut = buffer;
    return 0;
}

int symlink_utils_ministat(const char* path, uint32_t* filemode)
{
    struct stat st;

    if (stat(path, &st) != 0) {
        return -1;
    }
    *filemode = st.st_mode;
    return 0;
}

char* symlink_utils_abspath(const char* path)
{
    return realpath(path, NULL);
}

#endif