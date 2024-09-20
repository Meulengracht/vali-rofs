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
 * VaFs Builder
 * - Contains the implementation of the VaFs.
 *   This filesystem is used to store the initrd of the kernel.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vafs/vafs.h>
#include <vafs/directory.h>
#include <vafs/file.h>
#include "utils/platform.h"

#if defined(_WIN32) || defined(_WIN64)
#include <assert.h>
#include "dirent_win32.h"
#include "ntifs_win32.h"
#include <sys/stat.h>

// Ntdll function pointers
static sRtlNtStatusToDosError        pRtlNtStatusToDosError;
static sNtQueryInformationFile       pNtQueryInformationFile;
static sNtQueryVolumeInformationFile pNtQueryVolumeInformationFile;
static HMODULE                       hNtdll;

int __win32_init(void)
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

void __win32_cleanup(void)
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
    int                  target_len;
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
        /* Real symlink */
        w_target = reparse_data->SymbolicLinkReparseBuffer.PathBuffer +
            (reparse_data->SymbolicLinkReparseBuffer.SubstituteNameOffset /
            sizeof(WCHAR));
        w_target_len =
            reparse_data->SymbolicLinkReparseBuffer.SubstituteNameLength /
            sizeof(WCHAR);

        /* Real symlinks can contain pretty much everything, but the only thing we
        * really care about is undoing the implicit conversion to an NT namespaced
        * path that CreateSymbolicLink will perform on absolute paths. If the path
        * is win32-namespaced then the user must have explicitly made it so, and
        * we better just return the unmodified reparse data. */
        if (w_target_len >= 4 &&
            w_target[0] == L'\\' &&
            w_target[1] == L'?' &&
            w_target[2] == L'?' &&
            w_target[3] == L'\\') {
            /* Starts with \??\ */
            if (w_target_len >= 6 &&
                ((w_target[4] >= L'A' && w_target[4] <= L'Z') ||
                (w_target[4] >= L'a' && w_target[4] <= L'z')) &&
                w_target[5] == L':' &&
                (w_target_len == 6 || w_target[6] == L'\\')) {
                /* \??\<drive>:\ */
                w_target += 4;
                w_target_len -= 4;

            } else if (w_target_len >= 8 &&
                        (w_target[4] == L'U' || w_target[4] == L'u') &&
                        (w_target[5] == L'N' || w_target[5] == L'n') &&
                        (w_target[6] == L'C' || w_target[6] == L'c') &&
                        w_target[7] == L'\\') {
                /* \??\UNC\<server>\<share>\ - make sure the final path looks like
                * \\<server>\<share>\ */
                w_target += 6;
                w_target[0] = L'\\';
                w_target_len -= 6;
            }
        }
    } else if (reparse_data->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
        /* Junction. */
        w_target = reparse_data->MountPointReparseBuffer.PathBuffer +
            (reparse_data->MountPointReparseBuffer.SubstituteNameOffset /
            sizeof(WCHAR));
        w_target_len = reparse_data->MountPointReparseBuffer.SubstituteNameLength /
            sizeof(WCHAR);

        /* Only treat junctions that look like \??\<drive>:\ as symlink. Junctions
        * can also be used as mount points, like \??\Volume{<guid>}, but that's
        * confusing for programs since they wouldn't be able to actually
        * understand such a path when returned by uv_readlink(). UNC paths are
        * never valid for junctions so we don't care about them. */
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

        /* Remove leading \??\ */
        w_target += 4;
        w_target_len -= 4;
    } else if (reparse_data->ReparseTag == IO_REPARSE_TAG_APPEXECLINK) {
        /* String #3 in the list has the target filename. */
        w_target = reparse_data->AppExecLinkReparseBuffer.StringList;
        /* The StringList buffer contains a list of strings separated by "\0",   */
        /* with "\0\0" terminating the list. Move to the 3rd string in the list: */
        for (int i = 0; i < 2; ++i) {
            size_t len = wcslen(w_target);
            if (len == 0) {
                errno = ENOTSUP;
                goto cleanup;
            }
            w_target += len + 1;
        }
        w_target_len = wcslen(w_target);
        if (w_target_len == 0) {
            errno = ENOTSUP;
            goto cleanup;
        }
        /* Make sure it is an absolute path. */
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

    /* If needed, compute the length of the target. */
    if (symlinkBufferOut != NULL || symlinkLengthOut != NULL) {
        /* Compute the length of the target. */
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

    /* If requested, allocate memory and convert to UTF8. */
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

    // Buffer overflow (a warning status code) is expected here.
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

    // Buffer overflow (a warning status code) is expected here.
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

    // we do not care about these
    statbuf->st_gid = 0;
    statbuf->st_uid = 0;
    statbuf->st_rdev = 0;

    // determine mode
    statbuf->st_mode = 0;
    if (fileInformation.BasicInformation.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        statbuf->st_mode |= S_IFLNK;
        if (__readlink_handle(handle, NULL, &statbuf->st_size) != 0) {
            return -1;
        }
    }
    else if (fileInformation.BasicInformation.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        statbuf->st_mode |= _S_IFDIR;
        statbuf->st_size = 0;
    }
    else {
        statbuf->st_mode |= _S_IFREG;
        statbuf->st_size = fileInformation.StandardInformation.EndOfFile.QuadPart;
    }

    if (fileInformation.BasicInformation.FileAttributes & FILE_ATTRIBUTE_READONLY) {
        statbuf->st_mode |= _S_IREAD | (_S_IREAD >> 3) | (_S_IREAD >> 6);
    } else {
        statbuf->st_mode |= (_S_IREAD | _S_IWRITE) | ((_S_IREAD | _S_IWRITE) >> 3) |
        ((_S_IREAD | _S_IWRITE) >> 6);
    }

    statbuf->st_ino   = fileInformation.InternalInformation.IndexNumber.QuadPart;
    statbuf->st_nlink = fileInformation.StandardInformation.NumberOfLinks;
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

        // If the file is not a reparse point, then open it without the
        // reparse point flag
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

int __read_symlink(const char* path, char** bufferOut)
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

int __ministat(
    const char* path,
    uint32_t*   filemode)
{
    struct stat st;
    if (__stat(path, &st, 0) != 0) {
        return -1;
    }
    *filemode = st.st_mode;
    return 0;
}

#define __is_file(mode) S_ISREG(mode)
#define __is_symlink(mode) S_ISLNK(mode)
#define __is_directory(mode) S_ISDIR(mode)
#define __perms(mode) (mode & 0777)
#else
#include <dirent.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <unistd.h>

int __read_symlink(const char* path, char** bufferOut)
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

int __ministat(
    const char* path,
    uint32_t*   filemode)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    *filemode = st.st_mode;
    return 0;
}

#define __is_file(mode) S_ISREG(mode)
#define __is_symlink(mode) S_ISLNK(mode)
#define __is_directory(mode) S_ISDIR(mode)
#define __perms(mode) (mode & 0777)
#endif

struct progress_context {
    struct list file_list;
    int         disabled;

    int files;
    int directories;
    int symlinks;

    int files_total;
    int directories_total;
    int symlinks_total;
};

extern int __install_filter(struct VaFs* vafs, const char* filterName);

// Prints usage format of this program
static void __show_help(void)
{
    printf("usage: mkvafs [options] dir/files ...\n"
           "    --arch              {i386,amd64,arm,arm64,rv32,rv64}\n"
           "    --compression       {aplib}\n"
           "    --out               A path to where the disk image should be written to\n"
           "    --v,vv              Enables extra tracing output for debugging\n");
}


static enum VaFsArchitecture __get_vafs_arch(
    const char* arch)
{
    if (!strcmp(arch, "x86") || !strcmp(arch, "i386"))
        return VaFsArchitecture_X86;
    else if (!strcmp(arch, "x64") || !strcmp(arch, "amd64"))
        return VaFsArchitecture_X64;
    else if (strcmp(arch, "arm") == 0)
        return VaFsArchitecture_ARM;
    else if (strcmp(arch, "arm64") == 0)
        return VaFsArchitecture_ARM64;
    else {
        fprintf(stderr, "mkvafs: unknown architecture '%s'\n", arch);
        exit(-1);
    }
}

static const char* __get_filename(
    const char* path)
{
    const char* filename = (const char*)strrchr(path, __PATH_SEPARATOR);
    if (filename == NULL)
        filename = path;
    else
        filename++;
    return filename;
}

static void __write_progress(const char* prefix, struct progress_context* context)
{
    static int last = 0;
    int        current;
    int        total;
    int        progress;

    if (context->disabled) {
        return;
    }

    total   = context->files_total + context->directories_total + context->symlinks_total;
    current = context->files + context->directories + context->symlinks;
    progress = (current * 100) / total;

    printf("\33[2K\r%-20.20s [%d%%]", prefix, progress);
    if (context->files_total) {
        printf(" %i/%i files", context->files, context->files_total);
    }
    if (context->directories_total) {
        printf(" %i/%i dirs", context->directories, context->directories_total);
    }
    if (context->symlinks_total) {
        printf(" %i/%i symlinks", context->symlinks, context->symlinks_total);
    }
    fflush(stdout);
}

static int __write_file(
    struct VaFsDirectoryHandle* directoryHandle,
    const char*                 path,
    const char*                 filename,
    uint32_t                    permissions)
{
    struct VaFsFileHandle* fileHandle;
    FILE*                  file;
    long                   fileSize;
    void*                  fileBuffer;
    int                    status;

    // create the VaFS file
    status = vafs_directory_create_file(directoryHandle, filename, permissions, &fileHandle);
    if (status) {
        fprintf(stderr, "mkvafs: failed to create file '%s'\n", filename);
        return -1;
    }

    if ((file = fopen(path, "rb")) == NULL) {
        fprintf(stderr, "mkvafs: unable to open file %s\n", path);
        return -1;
    }

    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    if (fileSize) {
        fileBuffer = malloc(fileSize);
        if (fileBuffer == NULL) {
            fprintf(stderr, "mkvafs: failed to allocate memory for file '%s'\n", filename);
            fclose(file);
            return -1;
        }

        rewind(file);
        fread(fileBuffer, 1, fileSize, file);

        // write the file to the VaFS file
        vafs_file_write(fileHandle, fileBuffer, fileSize);
        free(fileBuffer);
    }
    fclose(file);

    status = vafs_file_close(fileHandle);
    if (status) {
        fprintf(stderr, "mkvafs: failed to close file '%s'\n", filename);
        return -1;
    }
    return 0;
}

static int __write_directory(
    struct progress_context*    progress,
    struct VaFsDirectoryHandle* directoryHandle,
    const char*                 path)
{
    struct dirent* dp;
    DIR*           dfd;
    int            status = 0;
    char*          filepathBuffer;

    if ((dfd = opendir(path)) == NULL) {
        fprintf(stderr, "mkvafs: can't open initrd folder\n");
        return -1;
    }

    filepathBuffer = malloc(1024);
    if (filepathBuffer == NULL) {
        closedir(dfd);
        errno = ENOMEM;
        return -1;
    }

    while ((dp = readdir(dfd)) != NULL) {
        uint32_t filemode;
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            continue;

        // only append a '/' if not provided
        if (path[strlen(path) - 1] != '/')
            sprintf(filepathBuffer, "%s/%s", path, dp->d_name);
        else
            sprintf(filepathBuffer, "%s%s", path, dp->d_name);
        
        __write_progress(dp->d_name, progress);
        status = __ministat(filepathBuffer, &filemode);
        if (status) {
            fprintf(stderr, "mkvafs: failed to stat file/directory '%s'\n", filepathBuffer);
            break;
        }

        if (__is_directory(filemode)) {
            struct VaFsDirectoryHandle* subdirectoryHandle;
            status = vafs_directory_create_directory(directoryHandle, dp->d_name, __perms(filemode), &subdirectoryHandle);
            if (status) {
                fprintf(stderr, "mkvafs: failed to create directory '%s'\n", dp->d_name);
                continue;
            }

            status = __write_directory(progress, subdirectoryHandle, filepathBuffer);
            if (status != 0) {
                fprintf(stderr, "mkvafs: unable to write directory %s\n", filepathBuffer);
                break;
            }

            status = vafs_directory_close(subdirectoryHandle);
            if (status) {
                fprintf(stderr, "mkvafs: failed to close directory '%s'\n", filepathBuffer);
                break;
            }
            progress->directories++;
        } else if (__is_symlink(filemode)) {
            char* linkpath = NULL;
            status = __read_symlink(filepathBuffer, &linkpath);
            if (status != 0) {
                fprintf(stderr, "mkvafs: failed to read link %s\n", filepathBuffer);
                break;
            }

            status = vafs_directory_create_symlink(directoryHandle, dp->d_name, linkpath);
            free(linkpath);

            if (status != 0) {
                fprintf(stderr, "mkvafs: failed to create symlink %s\n", filepathBuffer);
                break;
            }
            progress->symlinks++;
        } else if (__is_file(filemode)) {
            status = __write_file(directoryHandle, filepathBuffer, dp->d_name, __perms(filemode));
            if (status != 0) {
                fprintf(stderr, "mkvafs: unable to write file %s\n", dp->d_name);
                break;
            }
            progress->files++;
        }
        __write_progress(dp->d_name, progress);
    }

    free(filepathBuffer);
    closedir(dfd);
    return status;
}

struct __options {
    const char*       paths[32];
    int               paths_count;
    const char*       image_path;
    const char*       arch;
    const char*       compression;
    int               git_ignore;
    enum VaFsLogLevel level;
};

static int __read_ignore_file(struct list* filters, const char* path)
{

}

static int __is_excluded(struct list* filters, const char* path)
{

}

static int __add_platform_file_entry(struct list* to, const char* name, enum platform_filetype type, const char* subPath, const char* path)
{
    struct platform_file_entry* entry;

    entry = calloc(1, sizeof(struct platform_file_entry));
    if (entry == NULL) {
        return -1;
    }

    entry->name = strdup(name);
    entry->type = type;
    entry->sub_path = strdup(subPath != NULL ? subPath : name);
    entry->path = strdup(path);

    list_add(to, &entry->list_header);
    return 0;
}

static int __platform_file_entry_copy_to(struct platform_file_entry* entry, struct list* to)
{
    return __add_platform_file_entry(
        to,
        entry->name,
        entry->type,
        entry->sub_path,
        entry->path
    );
}

static int __discover_files_in_directory(struct progress_context* progress, const char* path, int gitIgnore)
{
    int               status;
    struct list       files = LIST_INIT;
    struct list       filters = LIST_INIT;
    struct list_item* it;

    status = platform_getfiles(path, 1, &files);
    if (status) {
        fprintf(stderr, "mkvafs: failed to get files for %s\n", path);
        return -1;
    }

    // first of all, is there a .gitignore in the current path?
    if (gitIgnore) {
        list_foreach(&files, it) {
            struct platform_file_entry* entry = (struct platform_file_entry*)it;
            if (!strcmp(entry->sub_path, ".gitignore")) {
                status = __read_ignore_file(&filters, entry->path);
                if (status) {
                    fprintf(stderr, "mkvafs: failed to read ignore file %s\n", entry->path);
                    platform_getfiles_destroy(&files);
                    return status;
                }
                break;
            }
        }
    }

    // now go through each and count them up, removing as we see fit
    list_foreach(&files, it) {
        struct platform_file_entry* entry = (struct platform_file_entry*)it;
        if (__is_excluded(&filters, entry->sub_path)) {
            continue;
        }

        status = __platform_file_entry_copy_to(entry, &progress->file_list);
        if (status) {
            fprintf(stderr, "mkvafs: failed to allocate memory for file list\n");
            platform_getfiles_destroy(&files);
            return status;
        }

        switch (entry->type) {
            case PLATFORM_FILETYPE_DIRECTORY:
                progress->directories_total++;
                break;
            case PLATFORM_FILETYPE_FILE:
                progress->files_total++;
                break;
            case PLATFORM_FILETYPE_SYMLINK:
                progress->symlinks_total++;
                break;
            default:
                break;
        }
    }
    platform_getfiles_destroy(&files);
    return 0;
}

static int __discover_files(struct progress_context* progress, const char** paths, int count, int gitIgnore)
{
    printf("mkvafs: discovering files\n");
    for (int i = 0; i < count; i++) {
        int      status;
        uint32_t filemode;

        status = __ministat(paths[i], &filemode);
        if (status) {
            fprintf(stderr, "mkvafs: failed to stat %s\n", paths[i]);
            return status;
        }

        if (__is_directory(filemode)) {
            status = __discover_files_in_directory(progress, paths[i], gitIgnore);
            if (status) {
                fprintf(stderr, "mkvafs: failed to discover files in %s\n", paths[i]);
                return status;
            }
            progress->directories_total++;
        } else if (__is_symlink(filemode)) {
            status = __add_platform_file_entry(
                &progress->file_list, __get_filename(paths[i]),
                PLATFORM_FILETYPE_SYMLINK, NULL, paths[i]
            );
            if (status) {
                fprintf(stderr, "mkvafs: failed to allocate memory for %s\n", paths[i]);
                return status;
            }
            progress->symlinks_total++;
        } else if (__is_file(filemode)) {
            status = __add_platform_file_entry(
                &progress->file_list, __get_filename(paths[i]),
                PLATFORM_FILETYPE_FILE, NULL, paths[i]
            );
            if (status) {
                fprintf(stderr, "mkvafs: failed to allocate memory for %s\n", paths[i]);
                return status;
            }
            progress->files_total++;
        }
    }

    printf("mkvafs: %i directories, %i files and %i symlinks\n",
        progress->directories_total,
        progress->files_total,
        progress->symlinks_total
    );
}

static int __create_image(struct __options* opts)
{
    struct VaFsDirectoryHandle* directoryHandle;
    struct VaFs*                vafsHandle;
    struct VaFsConfiguration    configuration;
    int                         status;
    struct progress_context     progressContext = { 
        LIST_INIT,
        0
    };

    // disable progress if we have debug output
    if (opts->level > VaFsLogLevel_Warning) {
        progressContext.disabled = 1;
    }

    status = __discover_files(&progressContext, &opts->paths[0], opts->paths_count, opts->git_ignore);
    if (status) {
        fprintf(stderr, "mkvafs: cannot create image\n");
        return status;
    }

    vafs_config_initialize(&configuration);
    vafs_config_set_architecture(&configuration, __get_vafs_arch(opts->arch));
    status = vafs_create(opts->image_path, &configuration, &vafsHandle);
    if (status) {
        fprintf(stderr, "mkvafs: cannot create vafs output file: %s\n", opts->image_path);
        return -1;
    }

    // Was a compression requested?
    if (opts->compression != NULL) {
        status = __install_filter(vafsHandle, opts->compression);
        if (status) {
            fprintf(stderr, "mkvafs: cannot set compression: %s\n", opts->compression);
            return -1;
        }
    }

    status = vafs_directory_open(vafsHandle, "/", &directoryHandle);
    if (status) {
        fprintf(stderr, "mkvafs: cannot open root directory: /\n");
        return -1;
    }

    for (int i = 0; i < pathCount; i++) {
        uint32_t filemode;

        __write_progress(paths[i], &progressContext);
        status = __ministat(paths[i], &filemode);
        if (status) {
            fprintf(stderr, "mkvafs: cannot stat file/directory: %s\n", paths[i]);
            return -1;
        }

        if (__is_directory(filemode)) {
            status = __write_directory(&progressContext, directoryHandle, paths[i]);
            if (status != 0) {
                fprintf(stderr, "mkvafs: unable to write directory %s\n", paths[i]);
                break;
            }
            progressContext.directories++;
        } else if (__is_symlink(filemode)) {
            char* linkpath = NULL;
            status = __read_symlink(paths[i], &linkpath);
            if (status != 0) {
                fprintf(stderr, "mkvafs: failed to read link %s\n", paths[i]);
                break;
            }

            status = vafs_directory_create_symlink(directoryHandle, paths[i], linkpath);
            free(linkpath);

            if (status != 0) {
                fprintf(stderr, "mkvafs: failed to create symlink %s\n", paths[i]);
                break;
            }
            progressContext.symlinks++;
        } else if (__is_file(filemode)) {
            status = __write_file(directoryHandle, paths[i], __get_filename(paths[i]), __perms(filemode));
            if (status != 0) {
                fprintf(stderr, "mkvafs: unable to write file %s\n", paths[i]);
                break;
            }
            progressContext.files++;
        }
        __write_progress(paths[i], &progressContext);

    }
    
    if (!progressContext.disabled) {
        printf("\n");
    }

    status = vafs_directory_close(directoryHandle);
    if (status) {
        fprintf(stderr, "mkvafs: failed to close root directory handle\n");
    }

    status = vafs_close(vafsHandle);
    if (status) {
        fprintf(stderr, "mkvafs: failed to finalize image\n");
    }
}

static void __parse_options(struct __options* opts, int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--arch") && (i + 1) < argc) {
            opts->arch = argv[++i];
        } else if (!strcmp(argv[i], "--compression") && (i + 1) < argc) {
            opts->compression = argv[++i];
        } else if (!strcmp(argv[i], "--out") && (i + 1) < argc) {
            opts->image_path = argv[++i];
        } else if (!strcmp(argv[i], "--v")) {
            opts->level = VaFsLogLevel_Info;
        } else if (!strcmp(argv[i], "--vv")) {
            opts->level = VaFsLogLevel_Debug;
        } else if (!strcmp(argv[i], "--git-ignore")) {
            opts->git_ignore = 1;
        } else {
            opts->paths[opts->paths_count++] = argv[i];
        }
    }
}

int main(int argc, char *argv[])
{
    int status;

    struct __options opts = { 
        { NULL },
        0,
        NULL,
        NULL,
        NULL,
        0,
        VaFsLogLevel_Warning
    };
    __parse_options(&opts, argc, argv);

    // validate parameters
    if (opts.arch == NULL || !opts.paths_count) {
        __show_help();
        return -1;
    }
    vafs_log_initalize(opts.level);

#if defined(_WIN32) || defined(_WIN64)
    status = __win32_init();
    if (status) {
        fprintf(stderr, "mkvafs: cannot load ntdll functions required on windows\n");
        return -1;
    }
#endif

    status = __create_image(&opts);

#if defined(_WIN32) || defined(_WIN64)
    __win32_cleanup();
#endif
    return status;
}
