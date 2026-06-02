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
 * VaFs Extractor
 * - Contains the implementation of the VaFs.
 *   This filesystem is used to store the initrd of the kernel.
 */

/* Suppress MSVC deprecation warnings for POSIX functions */
#if defined(_MSC_VER)
#pragma warning(disable:4996)
#endif

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vafs/vafs.h>
#include <vafs/directory.h>
#include <vafs/file.h>
#include "utils/utils.h"

struct __options {
    const char*       image_path;
    const char*       out_path;
    int               no_progress;
    enum VaFsLogLevel level;
};

struct progress_context {
    int disabled;

    int files;
    int directories;
    int symlinks;

    int files_total;
    int directories_total;
    int symlinks_total;

    // Hardlink extraction is two-phase because directory enumeration order is
    // not guaranteed to materialize the payload-bearing path before its alias.
    struct list extracted_objects;
    struct list pending_hardlinks;
};

struct extracted_object {
    struct list_item list_header;
    uint64_t         objectId;
    char*            path;
};

struct pending_hardlink {
    struct list_item list_header;
    uint64_t         objectId;
    char*            path;
};

extern int __handle_filter(struct VaFs* vafs);

static struct VaFsGuid g_overviewGuid = VA_FS_FEATURE_OVERVIEW;

// Prints usage format of this program
static void __show_help(void)
{
    printf("usage: unmkvafs [options] image\n"
           "    --out               A path to where the disk image should be extracted to\n"
           "    --v,vv              Enables extra tracing output for debugging\n");
}

static const char* __get_relative_path(
    const char* root,
    const char* path)
{
    const char* relative = path;
    if (strncmp(path, root, strlen(root)) == 0)
        relative = path + strlen(root);
    return relative;
}

static char* __combine_image_path(
    const char* parent,
    const char* name)
{
    char* buffer;

    if (parent == NULL || name == NULL) {
        errno = EINVAL;
        return NULL;
    }

    buffer = malloc(strlen(parent) + strlen(name) + 2);
    if (buffer == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    if (strcmp(parent, "/") == 0) {
        sprintf(buffer, "/%s", name);
    } else {
        sprintf(buffer, "%s/%s", parent, name);
    }
    return buffer;
}

static int __try_export_xattrs(
    struct VaFs* vafsHandle,
    const char*  imagePath,
    const char*  outputRoot,
    const char*  hostPath,
    int          followLinks)
{
    int status;

    status = platform_fs_export_xattrs(vafsHandle, imagePath, hostPath, followLinks);
    if (status == 0) {
        return 0;
    }

    // Extraction should preserve image xattrs when possible, but host support
    // and privilege gaps must not prevent the rest of the tree from unpacking.
    if (platform_fs_xattr_error_is_nonfatal(errno)) {
        fprintf(stderr,
            "unmkvafs: warning: unable to restore xattrs for '%s' (%s)\n",
            __get_relative_path(outputRoot, hostPath),
            strerror(errno));
        return 0;
    }

    fprintf(stderr,
        "unmkvafs: failed to restore xattrs for '%s' (%s)\n",
        __get_relative_path(outputRoot, hostPath),
        strerror(errno));
    return -1;
}

static int __is_nonfatal_special_create_error(
    int error)
{
    return error == EACCES ||
        error == EPERM ||
        error == ENOTSUP ||
        error == ENOSYS;
}

static int __is_nonfatal_hardlink_create_error(
    int error)
{
    // Match the special-file policy: privilege- or platform-limited host
    // recreation should warn and continue instead of discarding the full image.
    return error == EACCES ||
        error == EPERM ||
        error == ENOTSUP ||
        error == ENOSYS;
}

static struct extracted_object* __find_extracted_object(
    struct progress_context* progress,
    uint64_t                 objectId)
{
    struct list_item* it;

    list_foreach(&progress->extracted_objects, it) {
        struct extracted_object* object = (struct extracted_object*)it;
        if (object->objectId == objectId) {
            return object;
        }
    }
    return NULL;
}

static int __remember_extracted_object(
    struct progress_context* progress,
    uint64_t                 objectId,
    const char*              path)
{
    struct extracted_object* object;

    if (__find_extracted_object(progress, objectId) != NULL) {
        return 0;
    }

    object = calloc(1, sizeof(struct extracted_object));
    if (object == NULL) {
        return -1;
    }

    object->objectId = objectId;
    object->path = strdup(path);
    if (object->path == NULL) {
        free(object);
        return -1;
    }

    list_add(&progress->extracted_objects, &object->list_header);
    return 0;
}

static int __queue_pending_hardlink(
    struct progress_context* progress,
    uint64_t                 objectId,
    const char*              path)
{
    struct pending_hardlink* hardlink;

    hardlink = calloc(1, sizeof(struct pending_hardlink));
    if (hardlink == NULL) {
        return -1;
    }

    hardlink->objectId = objectId;
    hardlink->path = strdup(path);
    if (hardlink->path == NULL) {
        free(hardlink);
        return -1;
    }

    list_add(&progress->pending_hardlinks, &hardlink->list_header);
    return 0;
}

static int __register_extracted_file(
    struct VaFs*             vafsHandle,
    struct progress_context* progress,
    const char*              imagePath,
    const char*              outputPath)
{
    struct VaFsMetadata metadata;

    if (vafs_path_stat(vafsHandle, imagePath, 1, &metadata) != 0) {
        return -1;
    }

    if (metadata.Type != VaFsEntryType_File ||
        (metadata.Mask & VaFsMetadataMask_ObjectId) == 0 ||
        metadata.ObjectId == 0 ||
        metadata.LinkCount <= 1) {
        return 0;
    }

    // Only concrete file extracts can satisfy later aliases; hardlink entries
    // themselves never carry the payload that future siblings should target.
    return __remember_extracted_object(progress, metadata.ObjectId, outputPath);
}

static int __create_pending_hardlinks(
    struct progress_context* progress,
    const char*              root)
{
    struct list_item* it;

    // Replay aliases after the tree walk so images remain extractable even
    // when an alias appears before the first payload-bearing path.
    for (it = progress->pending_hardlinks.head; it != NULL; it = it->next) {
        struct pending_hardlink* hardlink = (struct pending_hardlink*)it;
        struct extracted_object* target = __find_extracted_object(progress, hardlink->objectId);
        int                      status;

        if (target == NULL) {
            errno = ENOENT;
            fprintf(stderr,
                "unmkvafs: failed to resolve hardlink target for '%s'\n",
                __get_relative_path(root, hardlink->path));
            return -1;
        }

        status = platform_fs_create_hardlink(target->path, hardlink->path);
        if (status != 0) {
            int createError = errno;

            if (__is_nonfatal_hardlink_create_error(createError)) {
                fprintf(stderr,
                    "unmkvafs: warning: skipping hardlink '%s' (%s)\n",
                    __get_relative_path(root, hardlink->path),
                    strerror(createError));
                continue;
            }

            fprintf(stderr,
                "unmkvafs: failed to create hardlink '%s'\n",
                __get_relative_path(root, hardlink->path));
            return -1;
        }
    }
    return 0;
}

static void __destroy_extraction_state(
    struct progress_context* progress)
{
    struct list_item* it;

    for (it = progress->extracted_objects.head; it != NULL;) {
        struct extracted_object* object = (struct extracted_object*)it;
        it = it->next;
        free(object->path);
        free(object);
    }
    list_init(&progress->extracted_objects);

    for (it = progress->pending_hardlinks.head; it != NULL;) {
        struct pending_hardlink* hardlink = (struct pending_hardlink*)it;
        it = it->next;
        free(hardlink->path);
        free(hardlink);
    }
    list_init(&progress->pending_hardlinks);
}

static int __extract_file(
    struct VaFsFileHandle* fileHandle,
    const char*            path)
{
    struct VaFsMetadata metadata;
    FILE*  file;
    size_t fileSize;
    void*  fileBuffer;

    if ((file = fopen(path, "wb+")) == NULL) {
        fprintf(stderr, "unmkvafs: unable to open file %s\n", path);
        return -1;
    }

    fileSize = vafs_file_length(fileHandle);
    if (fileSize) {
        fileBuffer = malloc(fileSize);
        if (fileBuffer == NULL) {
            fprintf(stderr, "unmkvafs: unable to allocate memory for file %s\n", path);
            return -1;
        }

        vafs_file_read(fileHandle, fileBuffer, fileSize);
        fwrite(fileBuffer, 1, fileSize, file);
        
        free(fileBuffer);
    }
    fclose(file);

    if (vafs_file_stat(fileHandle, &metadata) != 0) {
        return -1;
    }

    // update permissions on file
    return platform_fs_chmod(path, metadata.Mode & 07777u);
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

static int __extract_directory(
    struct VaFs*                vafsHandle,
    struct progress_context*    progress,
    struct VaFsDirectoryHandle* directoryHandle,
    const char*                 root,
    const char*                 path,
    const char*                 imagePath)
{
    struct VaFsMetadata metadata;
    struct VaFsEntry dp;
    int              status;
    char*            filepathBuffer;

    // ensure the directory exists
    if (strlen(path)) {
        status = platform_fs_directory_exists(path);
        if (status == -1) {
            fprintf(stderr, "unmkvafs: stat failed for '%s'\n", path);
            return status;
        }

        if (vafs_directory_stat(directoryHandle, &metadata) != 0) {
            fprintf(stderr, "unmkvafs: failed to read directory metadata for '%s'\n", path);
            return -1;
        }

        if (!status && platform_fs_create_directory(path, metadata.Mode & 07777u)) {
            fprintf(stderr, "unmkvafs: unable to create directory %s\n", path);
            return -1;
        }

        // Restore directory xattrs as soon as the host node exists so the
        // extracted tree exposes parent metadata before children are visited.
        status = __try_export_xattrs(vafsHandle, imagePath, root, path, 1);
        if (status != 0) {
            return status;
        }
    }

    do {
        status = vafs_directory_read(directoryHandle, &dp);
        if (status) {
            if (errno != ENOENT) {
                fprintf(stderr, "unmkvafs: failed to read directory '%s' - %i\n",
                    __get_relative_path(root, path), status);
                return -1;
            }
            break;
        }

        filepathBuffer = malloc(strlen(path) + strlen(dp.Name) + 2);
        if (filepathBuffer == NULL) {
            errno = ENOMEM;
            return -1;
        }

        char* imagePathBuffer = __combine_image_path(imagePath, dp.Name);
        if (imagePathBuffer == NULL) {
            free(filepathBuffer);
            return -1;
        }

        sprintf(filepathBuffer, "%s/%s", path, dp.Name);

        __write_progress(dp.Name, progress);
        if (dp.Type == VaFsEntryType_Directory) {
            struct VaFsDirectoryHandle* subdirectoryHandle;
            status = vafs_directory_open_directory(directoryHandle, dp.Name, &subdirectoryHandle);
            if (status) {
                fprintf(stderr, "unmkvafs: failed to open directory '%s'\n", __get_relative_path(root, filepathBuffer));
                free(imagePathBuffer);
                free(filepathBuffer);
                return -1;
            }

            status = __extract_directory(vafsHandle, progress, subdirectoryHandle, root, filepathBuffer, imagePathBuffer);
            if (status) {
                fprintf(stderr, "unmkvafs: unable to extract directory '%s'\n", __get_relative_path(root, path));
                free(imagePathBuffer);
                free(filepathBuffer);
                return -1;
            }

            status = vafs_directory_close(subdirectoryHandle);
            if (status) {
                fprintf(stderr, "unmkvafs: failed to close directory '%s'\n", __get_relative_path(root, filepathBuffer));
                free(imagePathBuffer);
                free(filepathBuffer);
                return -1;
            }
            progress->directories++;
        } else if (dp.Type == VaFsEntryType_Hardlink) {
            // Queue aliases instead of creating them inline because the first
            // concrete file path for this object may still be later in the walk.
            if (dp.ObjectId == 0) {
                fprintf(stderr, "unmkvafs: hardlink '%s' is missing an object id\n",
                    __get_relative_path(root, filepathBuffer));
                free(imagePathBuffer);
                free(filepathBuffer);
                return -1;
            }

            status = __queue_pending_hardlink(progress, dp.ObjectId, filepathBuffer);
            if (status != 0) {
                free(imagePathBuffer);
                free(filepathBuffer);
                return -1;
            }
        } else if (dp.Type == VaFsEntryType_Symlink) {
            const char* symlinkTarget;
            
            status = vafs_directory_read_symlink(directoryHandle, dp.Name, &symlinkTarget);
            if (status) {
                fprintf(stderr, "unmkvafs: failed to read symlink '%s' - %i\n",
                    __get_relative_path(root, filepathBuffer), status);
                free(imagePathBuffer);
                free(filepathBuffer);
                return -1;
            }

            status = symlink_utils_create(symlinkTarget, filepathBuffer);
            if (status) {
                fprintf(stderr, "unmkvafs: failed to create symlink '%s' - %i\n",
                    __get_relative_path(root, filepathBuffer), status);
                free(imagePathBuffer);
                free(filepathBuffer);
                return -1;
            }

            // Symlink xattrs belong on the link object, so extraction must not
            // follow the newly created host link to its target.
            status = __try_export_xattrs(vafsHandle, imagePathBuffer, root, filepathBuffer, 0);
            if (status != 0) {
                free(imagePathBuffer);
                free(filepathBuffer);
                return status;
            }
            progress->symlinks++;
        } else if (dp.Type == VaFsEntryType_CharacterDevice ||
            dp.Type == VaFsEntryType_BlockDevice ||
            dp.Type == VaFsEntryType_Fifo) {
            status = vafs_path_stat(vafsHandle, imagePathBuffer, 0, &metadata);
            if (status) {
                fprintf(stderr, "unmkvafs: failed to stat special entry '%s'\n",
                    __get_relative_path(root, filepathBuffer));
                free(imagePathBuffer);
                free(filepathBuffer);
                return -1;
            }

            status = platform_fs_create_special(filepathBuffer, &metadata);
            if (status) {
                int createError = errno;

                if (__is_nonfatal_special_create_error(createError)) {
                    fprintf(stderr,
                        "unmkvafs: warning: skipping special entry '%s' (%s)\n",
                        __get_relative_path(root, filepathBuffer),
                        strerror(createError));
                } else {
                    fprintf(stderr,
                        "unmkvafs: failed to create special entry '%s' - %i\n",
                        __get_relative_path(root, filepathBuffer), status);
                    free(imagePathBuffer);
                    free(filepathBuffer);
                    return -1;
                }
            } else {
                // Only restore xattrs after the host node exists; the warning
                // path above intentionally skips this because there is no host
                // object left to attach metadata to.
                status = __try_export_xattrs(vafsHandle, imagePathBuffer, root, filepathBuffer, 1);
                if (status != 0) {
                    free(imagePathBuffer);
                    free(filepathBuffer);
                    return status;
                }
            }
        } else {
            struct VaFsFileHandle* fileHandle;
            status = vafs_directory_open_file(directoryHandle, dp.Name, &fileHandle);
            if (status) {
                fprintf(stderr, "unmkvafs: failed to open file '%s' - %i\n",
                    __get_relative_path(root, filepathBuffer), status);
                free(imagePathBuffer);
                free(filepathBuffer);
                return -1;
            }

            status = __extract_file(fileHandle, filepathBuffer);
            if (status) {
                fprintf(stderr, "unmkvafs: unable to extract file '%s'\n", __get_relative_path(root, path));
                free(imagePathBuffer);
                free(filepathBuffer);
                return -1;
            }

            status = vafs_file_close(fileHandle);
            if (status) {
                fprintf(stderr, "unmkvafs: failed to close file '%s'\n", __get_relative_path(root, filepathBuffer));
                free(imagePathBuffer);
                free(filepathBuffer);
                return -1;
            }

            // Restore xattrs before registering the path as the concrete source
            // for later hardlink aliases so the first extracted inode carries
            // the full shared metadata set.
            status = __try_export_xattrs(vafsHandle, imagePathBuffer, root, filepathBuffer, 1);
            if (status != 0) {
                free(imagePathBuffer);
                free(filepathBuffer);
                return status;
            }

            status = __register_extracted_file(vafsHandle, progress, imagePathBuffer, filepathBuffer);
            if (status != 0) {
                free(imagePathBuffer);
                free(filepathBuffer);
                return -1;
            }
            progress->files++;
        }
        __write_progress(dp.Name, progress);
        free(imagePathBuffer);
        free(filepathBuffer);
    } while(1);

    // todo change permissions on directory

    return 0;
}

static int __handle_overview(struct VaFs* vafsHandle, struct progress_context* progress)
{
    struct VaFsFeatureOverview* overview;
    int                         status;

    status = vafs_feature_query(vafsHandle, &g_overviewGuid, (struct VaFsFeatureHeader**)&overview);
    if (status) {
        fprintf(stderr, "unmkvafs: failed to query feature overview - %i\n", errno);
        return -1;
    }

    progress->files_total       = overview->Counts.Files;
    progress->directories_total = overview->Counts.Directories;
    progress->symlinks_total    = overview->Counts.Symlinks;
    return 0;
}

static int __parse_options(struct __options* opts, int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && (i + 1) < argc) {
            opts->out_path = argv[++i];
        } else if (!strcmp(argv[i], "--v")) {
            opts->level = VaFsLogLevel_Info;
        } else if (!strcmp(argv[i], "--vv")) {
            opts->level = VaFsLogLevel_Debug;
        } else if (!strcmp(argv[i], "--no-progress")) {
            opts->no_progress = 1;
        } else if (!strncmp(argv[i], "-", 1)) {
            fprintf(stderr, "mkvfs: unrecognized parameter %s\n", argv[i]);
            return -1;
        } else {
            opts->image_path = argv[i];
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    struct VaFsDirectoryHandle* directoryHandle = NULL;
    struct VaFs*                vafsHandle;
    int                         status;
    struct progress_context     progressContext = { 0 };
    int                         exitCode = 0;

    struct __options opts = { 
        .image_path = NULL,
        .out_path = "vafs-root",
        .level = VaFsLogLevel_Warning
    };
    
    if (__parse_options(&opts, argc, argv)) {
        __show_help();
        return -1;
    }

    if (opts.image_path == NULL) {
        __show_help();
        return -1;
    }

    if (opts.no_progress) {
        progressContext.disabled = 1;
    }

    list_init(&progressContext.extracted_objects);
    list_init(&progressContext.pending_hardlinks);

    status = vafs_open_file(opts.image_path, &vafsHandle);
    if (status) {
        fprintf(stderr, "unmkvafs: cannot open vafs image: %s\n", opts.image_path);
        return -1;
    }

    status = __handle_overview(vafsHandle, &progressContext);
    if (status) {
        fprintf(stderr, "unmkvafs: failed to handle image overview\n");
        goto error;
    }

    status = __handle_filter(vafsHandle);
    if (status) {
        fprintf(stderr, "unmkvafs: failed to handle image filter\n");
        goto error;
    }

    status = vafs_directory_open(vafsHandle, "/", &directoryHandle);
    if (status) {
        fprintf(stderr, "unmkvafs: cannot open root directory: /\n");
        goto error;
    }

    status = __extract_directory(vafsHandle, &progressContext, directoryHandle, opts.out_path, opts.out_path, "/");
    if (status != 0) {
        fprintf(stderr, "unmkvafs: unable to extract to directory %s\n", opts.out_path);
        goto error;
    }

    status = __create_pending_hardlinks(&progressContext, opts.out_path);
    if (status != 0) {
        goto error;
    }

    if (!progressContext.disabled) {
        printf("\n");
    }

    goto exit;

error:
    exitCode = -1;

exit:
    if (directoryHandle) {
        status = vafs_directory_close(directoryHandle);
        if (status) {
            fprintf(stderr, "unmkvafs: failed to close root directory handle\n");
        }
    }
    __destroy_extraction_state(&progressContext);
    status = vafs_close(vafsHandle);
    if (status) {
        fprintf(stderr, "unmkvafs: failed to close image handle\n");
    }
    return exitCode;
}
