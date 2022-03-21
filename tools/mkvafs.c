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
#include <dirent_win32.h>
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
                
                npath = malloc(strlen(path)+strlen(direntp->d_name)+2);
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

    printf("\33[2K\r%s [%d%%] %i/%i files, %i/%i dirs",
        prefix, progress,
        context->files, context->files_total,
        context->directories, context->directories_total
    );
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
    fileBuffer = malloc(fileSize);
    rewind(file);
    fread(fileBuffer, 1, fileSize, file);
    fclose(file);

    // write the file to the VaFS file
    status = vafs_file_write(fileHandle, fileBuffer, fileSize);
    free(fileBuffer);
    
    if (status) {
        fprintf(stderr, "mkvafs: failed to write file '%s'\n", filename);
        return -1;
    }

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
    int            status;
    char*          filepathBuffer;

    if ((dfd = opendir(path)) == NULL) {
        fprintf(stderr, "mkvafs: can't open initrd folder\n");
        return -1;
    }

    filepathBuffer = malloc(512);
    while ((dp = readdir(dfd)) != NULL) {
        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
            continue;

        // only append a '/' if not provided
        if (path[strlen(path) - 1] != '/')
            sprintf(filepathBuffer, "%s/%s", path, dp->d_name);
        else
            sprintf(filepathBuffer, "%s%s", path, dp->d_name);
        
        __write_progress(filepathBuffer, progress);
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
            char* linkpath;
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
        __write_progress(filepathBuffer, progress);
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
        }
        else if (!strcmp(argv[i], "--compression") && (i + 1) < argc) {
            compressionName = argv[++i];
        }
        else if (!strcmp(argv[i], "--out") && (i + 1) < argc) {
            imagePath = argv[++i];
        }
        else if (!strcmp(argv[i], "--v")) {
            vafs_log_initalize(VaFsLogLevel_Info);
            progressContext.disabled = 1;
        }
        else if (!strcmp(argv[i], "--vv")) {
            vafs_log_initalize(VaFsLogLevel_Debug);
            progressContext.disabled = 1;
        }
        else {
            paths[pathCount++] = argv[i];
        }
    }

    if (arch == NULL || !pathCount) {
        __show_help();
        return -1;
    }

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
            char* linkpath;
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
    return vafs_close(vafsHandle);
}
