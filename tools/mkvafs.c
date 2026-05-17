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
 * VaFs Builder
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

struct progress_context {
    struct list file_list;
    int         disabled;

    int files;
    int symlinks;

    int files_total;
    int symlinks_total;
};

extern int __install_filters(struct VaFs* vafs, const char* descriptorFilterName, const char* dataFilterName);

static struct VaFsMetadata __metadata_for_mode(
    enum VaFsEntryType type,
    uint32_t           mode)
{
    struct VaFsMetadata metadata;

    // The builder still discovers host metadata incrementally. Packaging the
    // common type-plus-mode conversion here keeps the call sites readable while
    // making the new metadata contract explicit at every creation site.
    vafs_metadata_initialize(&metadata);
    vafs_metadata_set_mode(&metadata, type, mode);
    return metadata;
}

// Prints usage format of this program
static void __show_help(void)
{
    // Keep the legacy shared compression shorthand visible while surfacing the
    // per-stream overrides for descriptor and data policy tuning.
    printf("Usage: mkvafs [options] dir/files ...\n\n"
           "Options\n"
           "    --arch              {i386,amd64,arm,arm64,rv32,rv64,all}\n"
           "    --compression       {aplib,brieflz,none} (applies to both streams)\n"
           "    --descriptor-compression {aplib,brieflz,none}\n"
           "    --data-compression  {aplib,brieflz,none}\n"
           "    --descriptor-block-size <bytes>\n"
           "    --data-block-size   <bytes>\n"
           "    --out               A path to where the disk image should be written to\n"
           "    --git-ignore        Enable discovery of ignore files and apply to file discovery\n"
           "    --v,vv              Enables extra tracing output for debugging\n");
}

static int __parse_block_size_arg(
    const char* value,
    uint32_t*   blockSizeOut)
{
    char*         end;
    unsigned long parsed;

    // Parse explicit byte counts so descriptor and data streams can be tuned
    // independently from the command line.

    if (value == NULL || blockSizeOut == NULL) {
        return -1;
    }

    parsed = strtoul(value, &end, 0);
    if (value[0] == '\0' || *end != '\0' || parsed > UINT32_MAX) {
        fprintf(stderr, "mkvafs: invalid block size '%s'\n", value);
        return -1;
    }

    *blockSizeOut = (uint32_t)parsed;
    return 0;
}


static enum VaFsArchitecture __get_vafs_arch(
    const char* arch)
{
    if (arch == NULL) {
        return VaFsArchitecture_ALL;
    }

    if (strcmp(arch, "x86") == 0 || strcmp(arch, "i386") == 0)
        return VaFsArchitecture_X86;
    else if (strcmp(arch, "x64") == 0 || strcmp(arch, "amd64") == 0)
        return VaFsArchitecture_X64;
    else if (strcmp(arch, "arm") == 0 || strcmp(arch, "armhf") == 0)
        return VaFsArchitecture_ARM;
    else if (strcmp(arch, "arm64") == 0)
        return VaFsArchitecture_ARM64;
    else if (strcmp(arch, "r32") == 0)
        return VaFsArchitecture_RISCV32;
    else if (strcmp(arch, "r64") == 0)
        return VaFsArchitecture_RISCV64;
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

    total   = context->files_total + context->symlinks_total;
    current = context->files + context->symlinks;
    progress = (current * 100) / total;

    printf("\33[2K\rcompressing [%d%%] %i/%i %-40.40s", progress, current, total, prefix);
    fflush(stdout);
}

static int __write_file(
    struct VaFsDirectoryHandle* directoryHandle,
    const char*                 path,
    const char*                 filename,
    uint32_t                    permissions)
{
    struct VaFsMetadata   metadata = __metadata_for_mode(VaFsEntryType_File, permissions);
    struct VaFsFileHandle* fileHandle;
    FILE*                  file;
    long                   fileSize;
    void*                  fileBuffer;
    int                    status;

    // create the VaFS file
    status = vafs_directory_create_file(directoryHandle, filename, &metadata, &fileHandle);
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
    const char*       descriptor_compression;
    const char*       data_compression;
    uint32_t          descriptor_block_size;
    uint32_t          data_block_size;
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
    
    (void)index;
    (void)userContext;

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
        p[0] = '\0';
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
            vafs_hashtable_set(ignoreMap, &ign);
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
    while (pitr != NULL && *pitr) {
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
        ignent = __find_filter(&ignoreMap, entry->path);
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
    for (int i = 0; i < count; i++) {
        int      status;
        uint32_t filemode;
        char*    abspath;

        // resolve the full path first of all
        abspath = symlink_utils_abspath(paths[i]);
        if (abspath == NULL) {
            fprintf(stderr, "mkvafs: failed to resolve %s\n", paths[i]);
            return -1;
        }

        status = symlink_utils_ministat(abspath, &filemode);
        if (status) {
            fprintf(stderr, "mkvafs: failed to stat %s\n", abspath);
            free(abspath);
            return status;
        }

        if (platform_fs_mode_is_directory(filemode)) {
            status = __discover_files_in_directory(progress, abspath, gitIgnore);
            if (status) {
                fprintf(stderr, "mkvafs: failed to discover files in %s\n", abspath);
                free(abspath);
                return status;
            }
        } else if (platform_fs_mode_is_symlink(filemode)) {
            status = __add_platform_file_entry(
                &progress->file_list, __get_filename(abspath),
                PLATFORM_FILETYPE_SYMLINK, NULL, abspath
            );
            if (status) {
                fprintf(stderr, "mkvafs: failed to allocate memory for %s\n", abspath);
                free(abspath);
                return status;
            }
            progress->symlinks_total++;
        } else if (platform_fs_mode_is_file(filemode)) {
            status = __add_platform_file_entry(
                &progress->file_list, __get_filename(abspath),
                PLATFORM_FILETYPE_FILE, NULL, abspath
            );
            if (status) {
                fprintf(stderr, "mkvafs: failed to allocate memory for %s\n", abspath);
                free(abspath);
                return status;
            }
            progress->files_total++;
        }
        free(abspath);
    }
    return 0;
}

static struct VaFsDirectoryHandle* __get_directory_handle(struct VaFs* vafs, const char* abs, const char* relative)
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
    strcpy(&full[0], abs);
    full[strlen(abs) - strlen(relative)] = '\0';

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
            struct VaFsMetadata metadata;

            status = symlink_utils_ministat(&full[0], &filemode);
            if (status) {
                fprintf(stderr, "mkvafs: failed to stat %s\n", &full[0]);
                return NULL;
            }

            metadata = __metadata_for_mode(VaFsEntryType_Directory, platform_fs_mode_permissions(filemode));
            status = vafs_directory_create_directory(handle, &temp[0], &metadata, &next);
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

    // Image creation discovers inputs first, then applies stream policy
    // overrides before any filesystem content is written.

    // disable progress if we have debug output
    if (opts->level > VaFsLogLevel_Warning) {
        // Verbose logging would fight with the live progress line, so turn the
        // progress renderer off when tracing is enabled.
        progressContext.disabled = 1;
    }

    status = __discover_files(&progressContext, &opts->paths[0], opts->paths_count, opts->git_ignore);
    if (status) {
        fprintf(stderr, "mkvafs: failed to discover files: %i\n", status);
        return status;
    }

    // ensure there will be content to actually write
    if (progressContext.files_total == 0 && progressContext.symlinks_total == 0) {
        // Treat an empty discovery result as a user error instead of silently
        // producing an empty image.
        fprintf(stderr, "mkvafs: skipping image creation due to no files being created\n");
        return -1;
    }

    vafs_config_initialize(&configuration);
    vafs_config_set_architecture(&configuration, __get_vafs_arch(opts->arch));
    if (opts->descriptor_block_size != 0) {
        // Only override the descriptor default when the caller asked for it.
        vafs_config_set_descriptor_block_size(&configuration, opts->descriptor_block_size);
    }
    if (opts->data_block_size != 0) {
        // Keep the data stream default unless the caller explicitly changes it.
        vafs_config_set_data_block_size(&configuration, opts->data_block_size);
    }

    status = vafs_create(opts->image_path, &configuration, &vafsHandle);
    if (status) {
        fprintf(stderr, "mkvafs: cannot create vafs output file: %s\n", opts->image_path);
        return status;
    }

    // Install per-stream filter policy before writing entries so every emitted
    // descriptor and data block uses the requested callbacks.
    if (opts->descriptor_compression != NULL || opts->data_compression != NULL) {
        status = __install_filters(vafsHandle, opts->descriptor_compression, opts->data_compression);
        if (status) {
            fprintf(stderr, "mkvafs: cannot set stream compression\n");
            vafs_close(vafsHandle);
            return status;
        }
    }

    list_foreach(&progressContext.file_list, it) {
        struct platform_file_entry* entry = (struct platform_file_entry*)it;
        struct VaFsDirectoryHandle* directoryHandle;
        __write_progress(entry->sub_path, &progressContext);

        directoryHandle = __get_directory_handle(vafsHandle, entry->path, entry->sub_path);
        if (directoryHandle == NULL) {
            fprintf(stderr, "mkvafs: failed to get internal directory handle for %s\n", entry->sub_path);
            break;
        }

        if (entry->type == PLATFORM_FILETYPE_SYMLINK) {
            char* linkpath = NULL;
            struct VaFsMetadata metadata = __metadata_for_mode(VaFsEntryType_Symlink, 0777);

            status = symlink_utils_read(entry->path, &linkpath);
            if (status != 0) {
                fprintf(stderr, "mkvafs: failed to read link %s\n", entry->path);
                break;
            }

            status = vafs_directory_create_symlink(directoryHandle, entry->path, linkpath, &metadata);
            free(linkpath);

            if (status != 0) {
                fprintf(stderr, "mkvafs: failed to create symlink %s\n", entry->path);
                break;
            }
            progressContext.symlinks++;
        } else if (entry->type == PLATFORM_FILETYPE_FILE) {
            uint32_t filemode;
            status = symlink_utils_ministat(entry->path, &filemode);
            if (status) {
                fprintf(stderr, "mkvafs: cannot stat file/directory: %s\n", entry->path);
                break;
            }

            status = __write_file(directoryHandle, entry->path, __get_filename(entry->path), platform_fs_mode_permissions(filemode));
            if (status != 0) {
                fprintf(stderr, "mkvafs: unable to write file %s\n", entry->path);
                break;
            }
            progressContext.files++;
        }
        __write_progress(entry->sub_path, &progressContext);
    }
    
    if (!progressContext.disabled) {
        printf("\n");
    }

    if (vafs_close(vafsHandle)) {
        fprintf(stderr, "mkvafs: failed to finalize image\n");
    }
    return status;
}

static int __parse_options(struct __options* opts, int argc, char *argv[])
{
    // Parse the shared compression shorthand first, then let later per-stream
    // flags override either side independently on the same command line.
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--arch") && (i + 1) < argc) {
            opts->arch = argv[++i];
        } else if (!strcmp(argv[i], "--compression") && (i + 1) < argc) {
            opts->descriptor_compression = argv[++i];
            opts->data_compression = opts->descriptor_compression;
        } else if (!strcmp(argv[i], "--descriptor-compression") && (i + 1) < argc) {
            opts->descriptor_compression = argv[++i];
        } else if (!strcmp(argv[i], "--data-compression") && (i + 1) < argc) {
            opts->data_compression = argv[++i];
        } else if (!strcmp(argv[i], "--descriptor-block-size") && (i + 1) < argc) {
            // Fail fast on malformed numeric overrides instead of deferring the
            // error until image creation starts.
            if (__parse_block_size_arg(argv[++i], &opts->descriptor_block_size) != 0) {
                return -1;
            }
        } else if (!strcmp(argv[i], "--data-block-size") && (i + 1) < argc) {
            if (__parse_block_size_arg(argv[++i], &opts->data_block_size) != 0) {
                return -1;
            }
        } else if (!strcmp(argv[i], "--out") && (i + 1) < argc) {
            opts->image_path = argv[++i];
        } else if (!strcmp(argv[i], "--v")) {
            opts->level = VaFsLogLevel_Info;
        } else if (!strcmp(argv[i], "--vv")) {
            opts->level = VaFsLogLevel_Debug;
        } else if (!strcmp(argv[i], "--git-ignore")) {
            opts->git_ignore = 1;
        } else if (!strncmp(argv[i], "-", 1)) {
            fprintf(stderr, "unmkvfs: unrecognized parameter %s\n", argv[i]);
            return -1;
        } else {
            opts->paths[opts->paths_count++] = argv[i];
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int status;

    struct __options opts = { 
        .paths = { NULL },
        .paths_count = 0,
        .image_path = "image.vafs",
        .arch = NULL,
#ifdef __VAFS_FILTER_APLIB
        .descriptor_compression = "aplib",
        .data_compression = "aplib",
#elif __VAFS_FILTER_BRIEFLZ
        .descriptor_compression = "brieflz",
        .data_compression = "brieflz",
#endif
        .descriptor_block_size = 0,
        .data_block_size = 0,
        .git_ignore = 0,
        .level = VaFsLogLevel_Warning
    };
    
    if (__parse_options(&opts, argc, argv)) {
        __show_help();
        return -1;
    }

    // validate parameters
    if (!opts.paths_count) {
        __show_help();
        return -1;
    }
    vafs_log_initalize(opts.level);

#if defined(_WIN32) || defined(_WIN64)
    status = symlink_utils_init();
    if (status) {
        fprintf(stderr, "mkvafs: cannot load ntdll functions required on windows\n");
        return -1;
    }
#endif

    status = __create_image(&opts);

#if defined(_WIN32) || defined(_WIN64)
    symlink_utils_cleanup();
#endif
    return status;
}
