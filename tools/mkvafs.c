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
#include "utils/utils.h"

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

struct __options {
    const char*       paths[32];
    int               paths_count;
    const char*       image_path;
    const char*       arch;
    const char*       compression;
    int               git_ignore;
    enum VaFsLogLevel level;
};

static int __add_filter(struct list* filters, const char* filter)
{
    struct platform_string_item* item;

    item = calloc(1, sizeof(struct platform_string_item));
    if (item == NULL) {
        return -1;
    }
    item->value = strdup(filter);
    list_add(filters, &item->list_header);
    return 0;
}

static int __read_ignore_file(struct list* filters, const char* path)
{
    FILE* ignore;
    char  line[1024];
    int   status = 0;

    ignore = fopen(path, "r");
    if (ignore == NULL) {
        fprintf(stderr, "mkvafs: failed to open %s for reading\n", path);
        return -1;
    }

    while (fgets(&line[0], sizeof(line), ignore) != NULL) {
        // the filter is newline terminated
        size_t len = strlen(&line[0]);

        // ignore empty lines, or lines that start with a comment
        if (len == 0 || line[0] == '\n' || line[0] == '#') {
            continue;
        }

        if (line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        status = __add_filter(filters, &line[0]);
        if (status) {
            fprintf(stderr, "mkvafs: failed to add filter %s\n", &line[0]);
            break;
        }
    }
    fclose(ignore);
    return status;
}

static int __is_excluded(struct list* filters, const char* path)
{
    struct list_item* i;

    list_foreach(filters, i) {
        struct platform_string_item* filter = (struct platform_string_item*)i;
        if (strfilter(filter->value, path, 0) == 0) {
            // its a match
            return 1;
        }
    }
    return 0;
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

// Maybe rearrange code a bit nicer instead, for now we just need it inline
// here for a single purpose. If we need it somewhere else definitely do this first
#include "../libvafs/cache/hashtable.h"

struct _ignoremap_entry {
    uint64_t    hash;
    const char* path;
    struct list filters;
};

static uint64_t __hash_key(const char* key)
{
    uint32_t    hash = 5381;
    size_t      i    = 0;

    if (key == NULL) {
        return 0;
    }

    while (key[i]) {
        hash = ((hash << 5) + hash) + key[i++];
    }
    return (uint64_t)hash;
}

static uint64_t __ignoremap_hash(const void* elem)
{
    const struct _ignoremap_entry* entry = elem;
    return entry->hash;
}

static int __ignoremap_cmp(const void* lh, const void* rh)
{
    const struct _ignoremap_entry* lent = lh;
    const struct _ignoremap_entry* rent = rh;
    return lent->hash == rent->hash ? 0 : -1;
}


static void __filters_destroy(struct list* filters)
{
    struct list_item* i;
    for (i = filters->head; i != NULL;) {
        struct platform_string_item* item = (struct platform_string_item*)i;
        i = i->next;
        free((char*)item->value);
        free(item);
    }
}

void __ignoremap_free(int index, const void* elem, void* userContext)
{
    struct _ignoremap_entry* entry = (struct _ignoremap_entry*)elem;
    __filters_destroy(&entry->filters);
    free((char*)entry->path);
}

static char* __safe_strdup(const char* str)
{
    char* copy;
    if (str == NULL) {
        return NULL;
    }

    copy = malloc(strlen(str) + 1);
    strcpy(copy, str);
    return copy;
}

static char* __dirpath(const char* str)
{
    char* p;
    char* t;
    
    p = __safe_strdup(str);
    if (p == NULL) {
        return NULL;
    }

    t = strrchr(p, __PATH_SEPARATOR);
    if (t == NULL) {
        return p;
    }

    // terminate it there
    t[0] = '\0';
    return p;
}

static int __discover_filters(hashtable_t* ignoreMap, struct list* files)
{
    struct list_item* it;
    int               status;

    list_foreach(files, it) {
        struct platform_file_entry* entry = (struct platform_file_entry*)it;

        // XXX: support more filters?
        if (!strcmp(entry->name, ".gitignore")) {
            struct _ignoremap_entry ign = { 
                .filters = LIST_INIT
            };

            // For the key, we want to only use the folder part
            ign.path = __dirpath(entry->path);
            ign.hash = __hash_key(ign.path);

            // read all the filters in the ignore file
            status = __read_ignore_file(&ign.filters, entry->path);
            if (status) {
                fprintf(stderr, "mkvafs: failed to read ignore file %s\n", entry->path);
                return status;
            }

            // add to ignore map
            vafs_hashtable_set(ignoreMap, entry);
            break;
        }
    }
    return 0;
}

static struct _ignoremap_entry* __find_filter(hashtable_t* ignoreMap, const char* path)
{
    struct _ignoremap_entry* filter = NULL;
    char*                    pitr = __dirpath(path);

    // find matching ignore file
    while (pitr != NULL) {
        char* tmp;
        void* lookup = vafs_hashtable_get(ignoreMap, &(struct _ignoremap_entry) { 
            .hash = __hash_key(pitr),
        });
        if (lookup != NULL) {
            filter = lookup;
            break;
        }

        tmp = __dirpath(pitr);
        if (tmp == NULL) {
            break;
        }
        free(pitr);
        pitr = tmp;
    }
    free(pitr);
    return filter;
}

static int __discover_files_in_directory(struct progress_context* progress, const char* path, int gitIgnore)
{
    int               status = 0;
    struct list       files = LIST_INIT;
    struct list_item* it;
    hashtable_t       ignoreMap;

    // Initialize the hashtable for ignore files. We do this whether we
    // need it or not.
    status = vafs_hashtable_construct(
        &ignoreMap, 0, sizeof(struct _ignoremap_entry), 
        __ignoremap_hash, __ignoremap_cmp
    );
    if (status) {
        return status;
    }

    status = utils_getfiles(path, 1, &files);
    if (status) {
        fprintf(stderr, "mkvafs: failed to get files for %s\n", path);
        return -1;
    }

    // If requested, fill the ignore map with filters based on any .ignore file
    // we find.
    if (gitIgnore) {
        status = __discover_filters(&ignoreMap, &files);
        if (status) {
            goto cleanup;
        }
    }

    // now go through each and filter them against any matching ignore file
    list_foreach(&files, it) {
        struct platform_file_entry* entry = (struct platform_file_entry*)it;
        struct _ignoremap_entry*    ignent;
        
        // is it allowed by the ignore map?
        ignent = __find_filter(&ignoreMap, entry->sub_path);
        if (ignent != NULL) {
            if (__is_excluded(&ignent->filters, entry->sub_path)) {
                continue;
            }
        }

        status = __platform_file_entry_copy_to(entry, &progress->file_list);
        if (status) {
            fprintf(stderr, "mkvafs: failed to allocate memory for file list\n");
            goto cleanup;
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

cleanup:
    utils_getfiles_destroy(&files);
    vafs_hashtable_enumerate(&ignoreMap, __ignoremap_free, NULL);
    vafs_hashtable_destroy(&ignoreMap);
    return status;
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

static struct VaFsDirectoryHandle* __get_directory_handle(struct VaFs* vafs, const char* base, const char* relative)
{
    struct VaFsDirectoryHandle* handle;
    
    char        temp[4096] = { 0 };
    char        full[4096] = { 0 };
    char*       last;
    char*       st;
    const char* token = relative;

    if (vafs_directory_open(vafs, "/", &handle)) {
        fprintf(stderr, "mkvafs: failed to open image root directory\n");
        return NULL;
    }

    last = strrchr(relative, __PATH_SEPARATOR);
    if (last == NULL || last == relative) {
        return handle;
    }

    // setup full
    strcpy(&full[0], base);
    if (full[strlen(base)] != __PATH_SEPARATOR) {
        full[strlen(base)] = __PATH_SEPARATOR;
    }

    // copy first token
    st = strchr(token, __PATH_SEPARATOR);
    memcpy(&temp[0], token, (size_t)(st - token));
    temp[(size_t)(st - token)] = 0;
    strcat(&full[0], &temp[0]);

    for (;;) {
        struct VaFsDirectoryHandle* next;
        uint32_t                    filemode;
        int                         status;

        if (vafs_directory_open_directory(handle, &temp[0], &next)) {
            status = __ministat(&full[0], &filemode);
            if (status) {
                fprintf(stderr, "mkvafs: failed to stat %s\n", &full[0]);
                return NULL;
            }

            status = vafs_directory_create_directory(handle, &temp[0], __perms(filemode), &next);
            if (status) {
                fprintf(stderr, "mkvafs: failed to create directory %s\n", &temp[0]);
                return NULL;
            }
        }

        // yay, next token
        handle = next;
        token = st + 1;

        st = strchr(token, __PATH_SEPARATOR);
        if (st == NULL) {
            // no more, this is the last directory
            break;
        }

        memcpy(&temp[0], token, (size_t)(st - token));
        temp[(size_t)(st - token)] = 0;
        strcat(&full[0], "/");
        strcat(&full[0], &temp[0]);
    }
    return handle;
}

static int __create_image(struct __options* opts)
{
    struct VaFs*             vafsHandle;
    struct VaFsConfiguration configuration;
    int                      status;
    struct list_item*        it;
    struct progress_context  progressContext = { 
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

    // ensure there will be content to actually write
    if (progressContext.files_total == 0 && progressContext.symlinks_total == 0) {
        fprintf(stderr, "mkvafs: skipping image creation due to no files being created\n");
        return -1;
    }

    vafs_config_initialize(&configuration);
    vafs_config_set_architecture(&configuration, __get_vafs_arch(opts->arch));
    status = vafs_create(opts->image_path, &configuration, &vafsHandle);
    if (status) {
        fprintf(stderr, "mkvafs: cannot create vafs output file: %s\n", opts->image_path);
        return status;
    }

    // Was a compression requested?
    if (opts->compression != NULL) {
        status = __install_filter(vafsHandle, opts->compression);
        if (status) {
            fprintf(stderr, "mkvafs: cannot set compression: %s\n", opts->compression);
            vafs_close(vafsHandle);
            return status;
        }
    }

    list_foreach(&progressContext.file_list, it) {
        struct platform_file_entry* entry = (struct platform_file_entry*)it;
        struct VaFsDirectoryHandle* directoryHandle;
        
        if (entry->type == PLATFORM_FILETYPE_DIRECTORY) {
            // we do squad for directories, they get created automatically
            // if there is any content in them
            progressContext.directories++;
        }

        __write_progress(entry->path, &progressContext);

        directoryHandle = __get_directory_handle(vafsHandle, "", entry->sub_path);
        if (directoryHandle == NULL) {
            fprintf(stderr, "mkvafs: failed to get internal directory handle for %s\n", entry->sub_path);
            break;
        }

        if (entry->type == PLATFORM_FILETYPE_SYMLINK) {
            char* linkpath = NULL;
            status = __read_symlink(entry->path, &linkpath);
            if (status != 0) {
                fprintf(stderr, "mkvafs: failed to read link %s\n", entry->path);
                break;
            }

            status = vafs_directory_create_symlink(directoryHandle, entry->path, linkpath);
            free(linkpath);

            if (status != 0) {
                fprintf(stderr, "mkvafs: failed to create symlink %s\n", entry->path);
                break;
            }
            progressContext.symlinks++;
        } else if (entry->type == PLATFORM_FILETYPE_FILE) {
            uint32_t filemode;
            status = __ministat(entry->path, &filemode);
            if (status) {
                fprintf(stderr, "mkvafs: cannot stat file/directory: %s\n", entry->path);
                break;
            }

            status = __write_file(directoryHandle, entry->path, __get_filename(entry->path), __perms(filemode));
            if (status != 0) {
                fprintf(stderr, "mkvafs: unable to write file %s\n", entry->path);
                break;
            }
            progressContext.files++;
        }
        __write_progress(entry->path, &progressContext);
    }
    
    if (!progressContext.disabled) {
        printf("\n");
    }

    if (vafs_close(vafsHandle)) {
        fprintf(stderr, "mkvafs: failed to finalize image\n");
    }
    return status;
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
