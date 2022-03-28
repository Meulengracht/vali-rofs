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
        if (reparse_data->AppExecLinkReparseBuffer.StringCount < 3) {
            errno = ENOTSUP;
            goto cleanup;
        }
        w_target = reparse_data->AppExecLinkReparseBuffer.StringList;
        /* The StringList buffer contains a list of strings separated by "\0",   */
        /* with "\0\0" terminating the list. Move to the 3rd string in the list: */
        for (i = 0; i < 2; ++i) {
        len = wcslen(w_target);
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

    handle = CreateFileA(path, 0, 0, NULL,
        OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );
    if (handle == INVALID_HANDLE_VALUE) {
        errno = ENOENT;
        return -1;
    }

    status = __readlink_handle(handle, linkBuffer, &targetLength);
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

int __is_file(
    const char* path)
{
    struct stat st;
    if (__stat(path, &st, 0) != 0) {
        fprintf(stderr, "mkvafs: stat failed for '%s'\n", path);
        return 0;
    }
    return S_ISREG(st.st_mode);
}

int __is_symlink(
    const char* path)
{
    struct stat st;
    if (__stat(path, &st, 1) != 0) {
        fprintf(stderr, "mkvafs: lstat failed for '%s'\n", path);
        return 0;
    }
    return S_ISLNK(st.st_mode);
}

static int __is_directory(
    const char* path)
{
    struct stat st;
    if (__stat(path, &st, 0) != 0) {
        fprintf(stderr, "mkvafs: stat failed for '%s'\n", path);
        return 0;
    }
    return S_ISDIR(st.st_mode);
}

#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

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

    if (readlink(path, buffer, 1024) == -1) {
        free(buffer);
        return -1;
    }

    *bufferOut = buffer;
    return 0;
}

int __is_file(
    const char* path)
{
    struct stat st;
    if (lstat(path, &st) != 0) {
        fprintf(stderr, "mkvafs: stat failed for '%s'\n", path);
        return 0;
    }
    return S_ISREG(st.st_mode);
}

int __is_symlink(
    const char* path)
{
    struct stat st;
    if (lstat(path, &st) != 0) {
        fprintf(stderr, "mkvafs: stat failed for '%s'\n", path);
        return 0;
    }
    return S_ISLNK(st.st_mode);
}

static int __is_directory(
    const char* path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "mkvafs: stat failed for '%s'\n", path);
        return 0;
    }
    return S_ISDIR(st.st_mode);
}

#endif

struct progress_context {
    int disabled;

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
    const char* filename = (const char*)strrchr(path, '/');
    if (filename == NULL)
        filename = path;
    else
        filename++;
    return filename;
}

int __get_count_recursive(char *path, int* fileCountOut, int* SymlinkCountOut, int* dirCountOut)
{
    struct dirent* direntp;
    DIR*           dir_ptr = NULL;

    if (!path) {
        errno = EINVAL;
        return -1;
    }

    if ((dir_ptr = opendir(path)) == NULL) {
        return -1;
    }

    while ((direntp = readdir(dir_ptr))) {
        if (strcmp(direntp->d_name,".") == 0 || strcmp(direntp->d_name,"..") == 0) {
             continue;
        }

        switch (direntp->d_type) {
            case DT_REG:
                (*fileCountOut)++;
                break;
            case DT_DIR: {
                char* npath;

                (*dirCountOut)++;
                
                npath = malloc(strlen(path) + strlen(direntp->d_name) + 2);
                if (npath == NULL) {
                    errno = ENOMEM;
                    return -1;
                }
                
                sprintf(npath, "%s/%s", path, direntp->d_name);
                
                if (__get_count_recursive(npath, fileCountOut, SymlinkCountOut, dirCountOut) == -1) {
                    free(npath);
                    return -1;
                }

                free(npath);
            } break;
            case DT_LNK:
                (*SymlinkCountOut)++;
                break;
            default:
                break;
        }
    }
    closedir(dir_ptr);
    return 0;
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
    const char*                 filename)
{
    struct VaFsFileHandle* fileHandle;
    FILE*                  file;
    long                   fileSize;
    void*                  fileBuffer;
    int                    status;

    // create the VaFS file
    status = vafs_directory_open_file(directoryHandle, filename, &fileHandle);
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
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            continue;

        // only append a '/' if not provided
        if (path[strlen(path) - 1] != '/')
            sprintf(filepathBuffer, "%s/%s", path, dp->d_name);
        else
            sprintf(filepathBuffer, "%s%s", path, dp->d_name);
        
        __write_progress(dp->d_name, progress);
        if (__is_directory(filepathBuffer)) {
            struct VaFsDirectoryHandle* subdirectoryHandle;
            status = vafs_directory_open_directory(directoryHandle, dp->d_name, &subdirectoryHandle);
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
        } else if (__is_symlink(filepathBuffer)) {
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
        } else if (__is_file(filepathBuffer)) {
            status = __write_file(directoryHandle, filepathBuffer, dp->d_name);
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

int main(int argc, char *argv[])
{
    struct VaFsDirectoryHandle* directoryHandle;
    struct VaFs*                vafsHandle;
    int                         status;
    struct progress_context     progressContext = { 0 };
    struct VaFsConfiguration    configuration;

    // parameters
    char* paths[32];
    char  pathCount = 0;
    char* arch = NULL;
    char* imagePath = "image.vafs"; 
    char* compressionName = NULL;

    // Validate the number of arguments
    // compression
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--arch") && (i + 1) < argc) {
            arch = argv[++i];
        } else if (!strcmp(argv[i], "--compression") && (i + 1) < argc) {
            compressionName = argv[++i];
        } else if (!strcmp(argv[i], "--out") && (i + 1) < argc) {
            imagePath = argv[++i];
        } else if (!strcmp(argv[i], "--v")) {
            vafs_log_initalize(VaFsLogLevel_Info);
            progressContext.disabled = 1;
        } else if (!strcmp(argv[i], "--vv")) {
            vafs_log_initalize(VaFsLogLevel_Debug);
            progressContext.disabled = 1;
        } else {
            paths[pathCount++] = argv[i];
        }
    }

    if (arch == NULL || !pathCount) {
        __show_help();
        return -1;
    }

#if defined(_WIN32) || defined(_WIN64)
    status = __win32_init();
    if (status) {
        fprintf(stderr, "mkvafs: cannot load ntdll functions required on windows\n");
        return -1;
    }
#endif

    vafs_config_initialize(&configuration);
    vafs_config_set_architecture(&configuration, __get_vafs_arch(arch));
    status = vafs_create(imagePath, &configuration, &vafsHandle);
    if (status) {
        fprintf(stderr, "mkvafs: cannot create vafs output file: %s\n", imagePath);
        return -1;
    }

    // Was a compression requested?
    if (compressionName != NULL) {
        status = __install_filter(vafsHandle, compressionName);
        if (status) {
            fprintf(stderr, "mkvafs: cannot set compression: %s\n", compressionName);
            return -1;
        }
    }

    status = vafs_directory_open(vafsHandle, "/", &directoryHandle);
    if (status) {
        fprintf(stderr, "mkvafs: cannot open root directory: /\n");
        return -1;
    }

    printf("mkvafs: counting files\n");
    for (int i = 0; i < pathCount; i++) {
        if (__is_directory(paths[i])) {
            progressContext.directories_total++;
            __get_count_recursive(paths[i],
                &progressContext.files_total,
                &progressContext.symlinks_total, 
                &progressContext.directories_total
            );
        } else if (__is_symlink(paths[i])) {
            progressContext.symlinks_total++;
        } else if (__is_file(paths[i])) {
            progressContext.files_total++;
        }
    }

    printf("mkvafs: writing %i directories, %i files and %i symlinks\n",
        progressContext.directories_total,
        progressContext.files_total,
        progressContext.symlinks_total
    );
    for (int i = 0; i < pathCount; i++) {
        __write_progress(paths[i], &progressContext);
        if (__is_directory(paths[i])) {
            status = __write_directory(&progressContext, directoryHandle, paths[i]);
            if (status != 0) {
                fprintf(stderr, "mkvafs: unable to write directory %s\n", paths[i]);
                break;
            }
            progressContext.directories++;
        } else if (__is_symlink(paths[i])) {
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
        } else if (__is_file(paths[i])) {
            status = __write_file(directoryHandle, paths[i], __get_filename(paths[i]));
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
    status = vafs_close(vafsHandle);

#if defined(_WIN32) || defined(_WIN64)
    __win32_cleanup();
#endif
    return status;
}
