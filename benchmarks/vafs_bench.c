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
 * VaFS Benchmark Suite
 */

/* Suppress MSVC deprecation warnings for POSIX functions */
#if defined(_MSC_VER)
#pragma warning(disable:4996)
#endif

#include "filter.h"
#include "benchmark.h"
#include <vafs/vafs.h>
#include <vafs/reader.h>
#include <vafs/builder.h>
#include <vafs/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#if !defined(_WIN32) && !defined(_WIN64)
#include <unistd.h>
#else
#include <io.h>
#define access _access
#define R_OK 4
#endif

// ============================
// Benchmark Configuration
// ============================

#define MOUNT_LATENCY_ITERATIONS       5
#define METADATA_TRAVERSAL_ITERATIONS  3
#define SMALL_FILE_READ_ITERATIONS     10
#define LARGE_FILE_READ_ITERATIONS     2
#define PATH_LOOKUP_ITERATIONS         5
#define WIDE_LOOKUP_ITERATIONS         5
#define DEEP_STAT_ITERATIONS           10
#define XATTR_GET_ITERATIONS           20
#define XATTR_LIST_ITERATIONS          20

#if !defined(PATH_MAX)
#define PATH_MAX 4096
#endif

static int open_benchmark_image(
    const char*    imagePath,
    struct VaFs**  vafsOut)
{
    struct VaFsReaderConfiguration readerConfiguration;

    __configure_reader_filters(&readerConfiguration);
    return vafs_reader_open_file(imagePath, &readerConfiguration, vafsOut);
}

// ============================
// Benchmark Context Structures
// ============================

typedef struct {
    const char* image_path;
    struct VaFs* vafs;
} MountBenchmarkContext;

typedef struct {
    const char* image_path;
    const char* directory_path;
    struct VaFs* vafs;
    int entry_count;
} TraversalBenchmarkContext;

typedef struct {
    const char* image_path;
    const char* file_path;
    struct VaFs* vafs;
    struct VaFsObjectReader* handle;
    char* buffer;
    size_t buffer_size;
    size_t bytes_read;
} FileReadBenchmarkContext;

typedef struct {
    const char* image_path;
    const char* path;
    struct VaFs* vafs;
} PathLookupBenchmarkContext;

typedef struct {
    const char* image_path;
    const char* path;
    struct VaFs* vafs;
} PathStatBenchmarkContext;

typedef struct {
    const char* image_path;
    const char* directory_path;
    struct VaFs* vafs;
    char** entries;
    size_t entry_count;
    size_t current_index;
} WideLookupBenchmarkContext;

typedef struct {
    char         generated_image_path[PATH_MAX];
    const char*  path;
    const char*  name;
    struct VaFs* vafs;
    char*        buffer;
    size_t       buffer_size;
    size_t       bytes_processed;
} XattrBenchmarkContext;

static int benchmark_path_stat(
    struct VaFs*         vafs,
    const char*          path,
    struct VaFsMetadata* metadataOut)
{
    struct VaFsObjectReader* object;
    int                      status;

    status = vafs_object_reader_open(vafs, path, VaFsLookup_None, &object);
    if (status != 0) {
        return status;
    }

    status = vafs_object_reader_stat(object, metadataOut);
    vafs_object_reader_close(object);
    return status;
}

static int benchmark_path_getxattr(
    struct VaFs* vafs,
    const char*  path,
    const char*  name,
    void*        value,
    size_t       valueSize,
    size_t*      bytesWrittenOut)
{
    struct VaFsObjectReader* object;
    int                      status;

    status = vafs_object_reader_open(vafs, path, VaFsLookup_None, &object);
    if (status != 0) {
        return status;
    }

    status = vafs_object_reader_getxattr(object, name, value, valueSize, bytesWrittenOut);
    vafs_object_reader_close(object);
    return status;
}

static int benchmark_path_listxattr(
    struct VaFs* vafs,
    const char*  path,
    char*        buffer,
    size_t       bufferSize,
    size_t*      bytesWrittenOut)
{
    struct VaFsObjectReader* object;
    int                      status;

    status = vafs_object_reader_open(vafs, path, VaFsLookup_None, &object);
    if (status != 0) {
        return status;
    }

    status = vafs_object_reader_listxattr(object, buffer, bufferSize, bytesWrittenOut);
    vafs_object_reader_close(object);
    return status;
}

static int benchmark_write_payload(
    struct VaFsFileBuilder* file,
    const void*             payload,
    size_t                  size)
{
    size_t bytesWritten;

    if (vafs_file_builder_write(file, payload, size, &bytesWritten) != 0) {
        return -1;
    }
    return bytesWritten == size ? 0 : -1;
}

typedef struct {
    uint64_t iteration_override;
    uint64_t warmup_iterations;
} BenchmarkCliOptions;

static BenchmarkRunConfig make_benchmark_run_config(
    uint64_t                  default_iterations,
    const BenchmarkCliOptions* cli_options)
{
    BenchmarkRunConfig config = {
        .iterations = default_iterations,
        .warmup_iterations = 0
    };

    if (cli_options != NULL) {
        if (cli_options->iteration_override != 0) {
            config.iterations = cli_options->iteration_override;
        }
        config.warmup_iterations = cli_options->warmup_iterations;
    }
    return config;
}

static int parse_uint64_option(
    const char* option_name,
    const char* value,
    int         allow_zero,
    uint64_t*   out_value)
{
    char*              end;
    unsigned long long parsed;

    if (option_name == NULL || value == NULL || out_value == NULL) {
        errno = EINVAL;
        return -1;
    }

    errno = 0;
    parsed = strtoull(value, &end, 10);
    if (errno != 0 || value[0] == '\0' || *end != '\0' || (!allow_zero && parsed == 0) || parsed > UINT64_MAX) {
        fprintf(stderr, "Invalid value for %s: %s\n", option_name, value);
        return -1;
    }

    *out_value = (uint64_t)parsed;
    return 0;
}

static struct VaFsMetadata make_metadata(
    enum VaFsEntryType type,
    uint32_t           mode)
{
    struct VaFsMetadata metadata;

    vafs_metadata_initialize(&metadata);
    vafs_metadata_set_mode(&metadata, type, mode);
    return metadata;
}

static int make_temp_image_path(
    char*  buffer,
    size_t buffer_size)
{
#if defined(_WIN32) || defined(_WIN64)
    char  temp_path[MAX_PATH];
    char  temp_file[MAX_PATH];
    DWORD path_length;

    if (buffer == NULL || buffer_size == 0) {
        errno = EINVAL;
        return -1;
    }

    path_length = GetTempPathA((DWORD)sizeof(temp_path), temp_path);
    if (path_length == 0 || path_length >= sizeof(temp_path)) {
        errno = EIO;
        return -1;
    }

    if (GetTempFileNameA(temp_path, "vbx", 0, temp_file) == 0) {
        errno = EIO;
        return -1;
    }

    DeleteFileA(temp_file);
    if (snprintf(buffer, buffer_size, "%s.vafs", temp_file) >= (int)buffer_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
#else
    char template_path[] = "/tmp/vafs-bench-xattr-XXXXXX";
    int  fd;

    if (buffer == NULL || buffer_size == 0) {
        errno = EINVAL;
        return -1;
    }

    fd = mkstemp(template_path);
    if (fd < 0) {
        return -1;
    }

    close(fd);
    remove(template_path);
    if (snprintf(buffer, buffer_size, "%s.vafs", template_path) >= (int)buffer_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
#endif
}

static int create_xattr_benchmark_image(
    const char* image_path)
{
    static const char payload[] = "Synthetic xattr benchmark payload\n";
    struct VaFs*                vafs = NULL;
    struct VaFsBuilderConfiguration    config;
    struct VaFsDirectoryBuilder* root = NULL;
    struct VaFsFileBuilder*      file_handle = NULL;
    struct VaFsObjectBuilder*    file_object = NULL;
    struct VaFsMetadata         file_metadata = make_metadata(VaFsEntryType_File, 0644);
    int                         status = -1;

    if (image_path == NULL) {
        errno = EINVAL;
        return -1;
    }

    remove(image_path);
    vafs_builder_config_initialize(&config);
    if (vafs_builder_new(image_path, &config, &vafs, &root) != 0) {
        goto cleanup;
    }

    if (vafs_directory_builder_create_file(root, "xattr_target", &file_metadata, &file_handle, &file_object) != 0) {
        goto cleanup;
    }

    if (benchmark_write_payload(file_handle, payload, strlen(payload)) != 0) {
        goto cleanup;
    }

    if (vafs_file_builder_close(file_handle) != 0) {
        file_handle = NULL;
        goto cleanup;
    }
    file_handle = NULL;

    // Create the xattr-bearing image in-process so the benchmark stays
    // reproducible even on hosts whose filesystem APIs cannot store xattrs.
    if (vafs_object_builder_setxattr(file_object, "user.mime", "text/plain", strlen("text/plain")) != 0 ||
        vafs_object_builder_setxattr(file_object, "user.owner", "root", strlen("root")) != 0 ||
        vafs_object_builder_setxattr(file_object, "user.checksum", "0123456789abcdef", strlen("0123456789abcdef")) != 0 ||
        vafs_object_builder_setxattr(file_object, "user.empty", NULL, 0) != 0) {
        goto cleanup;
    }

    status = 0;

cleanup:
    if (file_handle != NULL) {
        vafs_file_builder_close(file_handle);
    }
    if (root != NULL) {
        vafs_directory_builder_close(root);
    }
    if (vafs != NULL) {
        vafs_builder_close(vafs);
    }
    if (status != 0) {
        remove(image_path);
    }
    return status;
}

static int xattr_benchmark_open(
    XattrBenchmarkContext* ctx)
{
    int status;

    if (ctx == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (make_temp_image_path(ctx->generated_image_path, sizeof(ctx->generated_image_path)) != 0) {
        return -1;
    }

    if (create_xattr_benchmark_image(ctx->generated_image_path) != 0) {
        return -1;
    }

    status = open_benchmark_image(ctx->generated_image_path, &ctx->vafs);
    if (status != 0) {
        remove(ctx->generated_image_path);
        ctx->generated_image_path[0] = '\0';
        return -1;
    }

    status = __handle_filter(ctx->vafs);
    if (status != 0) {
        vafs_reader_close(ctx->vafs);
        ctx->vafs = NULL;
        remove(ctx->generated_image_path);
        ctx->generated_image_path[0] = '\0';
        return -1;
    }
    return 0;
}

static void xattr_benchmark_teardown_common(
    XattrBenchmarkContext* ctx)
{
    if (ctx == NULL) {
        return;
    }

    free(ctx->buffer);
    ctx->buffer = NULL;

    if (ctx->vafs != NULL) {
        vafs_reader_close(ctx->vafs);
        ctx->vafs = NULL;
    }

    if (ctx->generated_image_path[0] != '\0') {
        remove(ctx->generated_image_path);
        ctx->generated_image_path[0] = '\0';
    }
}

// ============================
// Mount Latency Benchmark
// ============================

static int mount_benchmark_setup(void* user_data)
{
    (void)user_data;
    // No setup needed
    return 0;
}

static int mount_benchmark_run(void* user_data)
{
    MountBenchmarkContext* ctx = (MountBenchmarkContext*)user_data;
    struct VaFs* vafs = NULL;
    int status;

    status = open_benchmark_image(ctx->image_path, &vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to open VaFS image: %s\n", strerror(errno));
        return -1;
    }

    // Install decompression filters if needed
    status = __handle_filter(vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to install filters: %s\n", strerror(errno));
        vafs_reader_close(vafs);
        return -1;
    }

    vafs_reader_close(vafs);
    return 0;
}

static void mount_benchmark_teardown(void* user_data)
{
    (void)user_data;
    // No teardown needed
}

static BenchmarkResult run_mount_latency_benchmark(const char* image_path, const BenchmarkRunConfig* run_config)
{
    MountBenchmarkContext ctx = {
        .image_path = image_path,
        .vafs = NULL
    };

    return benchmark_run(
        "Mount Latency",
        run_config,
        mount_benchmark_setup,
        mount_benchmark_run,
        mount_benchmark_teardown,
        &ctx
    );
}

// ============================
// Metadata Traversal Benchmark
// ============================

static int traversal_benchmark_setup(void* user_data)
{
    TraversalBenchmarkContext* ctx = (TraversalBenchmarkContext*)user_data;
    int status;

    status = open_benchmark_image(ctx->image_path, &ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to open VaFS image: %s\n", strerror(errno));
        return -1;
    }

    // Install decompression filters if needed
    status = __handle_filter(ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to install filters: %s\n", strerror(errno));
        vafs_reader_close(ctx->vafs);
        return -1;
    }

    return 0;
}

static int traversal_benchmark_run(void* user_data)
{
    TraversalBenchmarkContext* ctx = (TraversalBenchmarkContext*)user_data;
    struct VaFsDirectoryReader* dir = NULL;
    struct VaFsEntry entry;
    int status;
    int count = 0;

    status = vafs_directory_reader_open(ctx->vafs, ctx->directory_path, VaFsLookup_None, &dir);
    if (status != 0) {
        fprintf(stderr, "Failed to open directory: %s\n", strerror(errno));
        return -1;
    }

    while (vafs_directory_reader_next(dir, &entry) == 0) {
        count++;
    }

    ctx->entry_count = count;
    vafs_directory_reader_close(dir);
    return 0;
}

static void traversal_benchmark_teardown(void* user_data)
{
    TraversalBenchmarkContext* ctx = (TraversalBenchmarkContext*)user_data;
    if (ctx->vafs) {
        vafs_reader_close(ctx->vafs);
    }
}

static BenchmarkResult run_metadata_traversal_benchmark(const char* image_path, const char* directory_path, const BenchmarkRunConfig* run_config)
{
    TraversalBenchmarkContext ctx = {
        .image_path = image_path,
        .directory_path = directory_path,
        .vafs = NULL,
        .entry_count = 0
    };

    BenchmarkResult result = benchmark_run(
        "Metadata Traversal",
        run_config,
        traversal_benchmark_setup,
        traversal_benchmark_run,
        traversal_benchmark_teardown,
        &ctx
    );

    return result;
}

// ============================
// Small File Read Benchmark
// ============================

static int small_file_read_setup(void* user_data)
{
    FileReadBenchmarkContext* ctx = (FileReadBenchmarkContext*)user_data;
    int status;

    status = open_benchmark_image(ctx->image_path, &ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to open VaFS image: %s\n", strerror(errno));
        return -1;
    }

    // Install decompression filters if needed
    status = __handle_filter(ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to install filters: %s\n", strerror(errno));
        vafs_reader_close(ctx->vafs);
        return -1;
    }

    ctx->buffer = (char*)malloc(ctx->buffer_size);
    if (!ctx->buffer) {
        fprintf(stderr, "Failed to allocate read buffer\n");
        vafs_reader_close(ctx->vafs);
        return -1;
    }

    return 0;
}

static int small_file_read_run(void* user_data)
{
    FileReadBenchmarkContext* ctx = (FileReadBenchmarkContext*)user_data;
    struct VaFsObjectReader* handle = NULL;
    uint64_t bytes_read;
    int status;

    status = vafs_object_reader_open(ctx->vafs, ctx->file_path, VaFsLookup_None, &handle);
    if (status != 0) {
        fprintf(stderr, "Failed to open file: %s\n", strerror(errno));
        return -1;
    }

    bytes_read = vafs_object_reader_read(handle, ctx->buffer, ctx->buffer_size);
    ctx->bytes_read = (size_t)bytes_read;

    vafs_object_reader_close(handle);
    return 0;
}

static void small_file_read_teardown(void* user_data)
{
    FileReadBenchmarkContext* ctx = (FileReadBenchmarkContext*)user_data;
    if (ctx->buffer) {
        free(ctx->buffer);
    }
    if (ctx->vafs) {
        vafs_reader_close(ctx->vafs);
    }
}

static BenchmarkResult run_small_file_read_benchmark(const char* image_path, const char* file_path, const BenchmarkRunConfig* run_config)
{
    FileReadBenchmarkContext ctx = {
        .image_path = image_path,
        .file_path = file_path,
        .vafs = NULL,
        .handle = NULL,
        .buffer = NULL,
        .buffer_size = 4096,  // 4KB read
        .bytes_read = 0
    };

    BenchmarkResult result = benchmark_run(
        "Small File Read (4KB)",
        run_config,
        small_file_read_setup,
        small_file_read_run,
        small_file_read_teardown,
        &ctx
    );

    // Calculate throughput
    result.bytes_processed = ctx.bytes_read * result.iterations;
    result.throughput_mbps = benchmark_calculate_throughput(result.bytes_processed, result.total_time_ms);

    return result;
}

// ============================
// Large File Sequential Read Benchmark
// ============================

static int large_file_read_setup(void* user_data)
{
    FileReadBenchmarkContext* ctx = (FileReadBenchmarkContext*)user_data;
    int status;

    status = open_benchmark_image(ctx->image_path, &ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to open VaFS image: %s\n", strerror(errno));
        return -1;
    }

    // Install decompression filters if needed
    status = __handle_filter(ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to install filters: %s\n", strerror(errno));
        vafs_reader_close(ctx->vafs);
        return -1;
    }

    status = vafs_object_reader_open(ctx->vafs, ctx->file_path, VaFsLookup_None, &ctx->handle);
    if (status != 0) {
        fprintf(stderr, "Failed to open file: %s\n", strerror(errno));
        vafs_reader_close(ctx->vafs);
        return -1;
    }

    ctx->buffer = (char*)malloc(ctx->buffer_size);
    if (!ctx->buffer) {
        fprintf(stderr, "Failed to allocate read buffer\n");
        vafs_object_reader_close(ctx->handle);
        vafs_reader_close(ctx->vafs);
        return -1;
    }

    return 0;
}

static int large_file_read_run(void* user_data)
{
    FileReadBenchmarkContext* ctx = (FileReadBenchmarkContext*)user_data;
    size_t total_read = 0;
    uint64_t bytes_read;

    // Reset file position to beginning
    vafs_object_reader_seek(ctx->handle, 0, SEEK_SET);

    // Read entire file in chunks
    while ((bytes_read = vafs_object_reader_read(ctx->handle, ctx->buffer, ctx->buffer_size)) > 0) {
        total_read += (size_t)bytes_read;
    }

    ctx->bytes_read = total_read;
    return 0;
}

static void large_file_read_teardown(void* user_data)
{
    FileReadBenchmarkContext* ctx = (FileReadBenchmarkContext*)user_data;
    if (ctx->buffer) {
        free(ctx->buffer);
    }
    if (ctx->handle) {
        vafs_object_reader_close(ctx->handle);
    }
    if (ctx->vafs) {
        vafs_reader_close(ctx->vafs);
    }
}

static BenchmarkResult run_large_file_read_benchmark(const char* image_path, const char* file_path, const BenchmarkRunConfig* run_config)
{
    FileReadBenchmarkContext ctx = {
        .image_path = image_path,
        .file_path = file_path,
        .vafs = NULL,
        .handle = NULL,
        .buffer = NULL,
        .buffer_size = 128 * 1024,  // 128KB chunks
        .bytes_read = 0
    };

    BenchmarkResult result = benchmark_run(
        "Large File Sequential Read",
        run_config,
        large_file_read_setup,
        large_file_read_run,
        large_file_read_teardown,
        &ctx
    );

    // Calculate throughput
    result.bytes_processed = ctx.bytes_read * result.iterations;
    result.throughput_mbps = benchmark_calculate_throughput(result.bytes_processed, result.total_time_ms);

    return result;
}

// ============================
// Path Lookup Benchmark
// ============================

static int path_lookup_setup(void* user_data)
{
    PathLookupBenchmarkContext* ctx = (PathLookupBenchmarkContext*)user_data;
    struct VaFsObjectReader*    handle = NULL;
    int status;

    status = open_benchmark_image(ctx->image_path, &ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to open VaFS image: %s\n", strerror(errno));
        return -1;
    }

    // Install decompression filters if needed
    status = __handle_filter(ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to install filters: %s\n", strerror(errno));
        vafs_reader_close(ctx->vafs);
        return -1;
    }

    // Warm the path once so the measured iterations reflect repeated lookup cost
    // rather than one-time directory loading and cache priming.
    status = vafs_object_reader_open(ctx->vafs, ctx->path, VaFsLookup_None, &handle);
    if (status != 0) {
        fprintf(stderr, "Failed to warm lookup path: %s\n", strerror(errno));
        vafs_reader_close(ctx->vafs);
        return -1;
    }
    vafs_object_reader_close(handle);

    return 0;
}

static int path_lookup_run(void* user_data)
{
    PathLookupBenchmarkContext* ctx = (PathLookupBenchmarkContext*)user_data;
    struct VaFsObjectReader* handle = NULL;
    int status;

    status = vafs_object_reader_open(ctx->vafs, ctx->path, VaFsLookup_None, &handle);
    if (status != 0) {
        return -1;
    }

    vafs_object_reader_close(handle);
    return 0;
}

static void path_lookup_teardown(void* user_data)
{
    PathLookupBenchmarkContext* ctx = (PathLookupBenchmarkContext*)user_data;
    if (ctx->vafs) {
        vafs_reader_close(ctx->vafs);
    }
}

static BenchmarkResult run_path_lookup_benchmark(const char* image_path, const char* path, const BenchmarkRunConfig* run_config)
{
    PathLookupBenchmarkContext ctx = {
        .image_path = image_path,
        .path = path,
        .vafs = NULL
    };

    return benchmark_run(
        "Repeated Path Lookup",
        run_config,
        path_lookup_setup,
        path_lookup_run,
        path_lookup_teardown,
        &ctx
    );
}

// ============================
// Deep Path Stat Benchmark
// ============================

static int path_stat_setup(void* user_data)
{
    PathStatBenchmarkContext* ctx = (PathStatBenchmarkContext*)user_data;
    struct VaFsMetadata       statbuf;
    int status;

    status = open_benchmark_image(ctx->image_path, &ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to open VaFS image: %s\n", strerror(errno));
        return -1;
    }

    status = __handle_filter(ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to install filters: %s\n", strerror(errno));
        vafs_reader_close(ctx->vafs);
        return -1;
    }

    // Warm the path once so repeated-stat measurements are not dominated by the
    // first directory load or initial lookup cache population.
    status = benchmark_path_stat(ctx->vafs, ctx->path, &statbuf);
    if (status != 0) {
        fprintf(stderr, "Failed to warm stat path: %s\n", strerror(errno));
        vafs_reader_close(ctx->vafs);
        return -1;
    }

    return 0;
}

static int path_stat_run(void* user_data)
{
    PathStatBenchmarkContext* ctx = (PathStatBenchmarkContext*)user_data;
    struct VaFsMetadata statbuf;
    int status;

    status = benchmark_path_stat(ctx->vafs, ctx->path, &statbuf);
    if (status != 0) {
        return -1;
    }

    return 0;
}

static void path_stat_teardown(void* user_data)
{
    PathStatBenchmarkContext* ctx = (PathStatBenchmarkContext*)user_data;
    if (ctx->vafs) {
        vafs_reader_close(ctx->vafs);
    }
}

static BenchmarkResult run_deep_path_stat_benchmark(const char* image_path, const char* path, const BenchmarkRunConfig* run_config)
{
    PathStatBenchmarkContext ctx = {
        .image_path = image_path,
        .path = path,
        .vafs = NULL
    };

    return benchmark_run(
        "Deep Path Stat",
        run_config,
        path_stat_setup,
        path_stat_run,
        path_stat_teardown,
        &ctx
    );
}

// ============================
// Wide Directory Lookup Benchmark
// ============================

static int wide_lookup_setup(void* user_data)
{
    WideLookupBenchmarkContext* ctx = (WideLookupBenchmarkContext*)user_data;
    struct VaFsDirectoryReader* dir = NULL;
    struct VaFsEntry entry;
    int status;
    size_t capacity = 0;

    status = open_benchmark_image(ctx->image_path, &ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to open VaFS image: %s\n", strerror(errno));
        return -1;
    }

    status = __handle_filter(ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to install filters: %s\n", strerror(errno));
        vafs_reader_close(ctx->vafs);
        return -1;
    }

    status = vafs_directory_reader_open(ctx->vafs, ctx->directory_path, VaFsLookup_None, &dir);
    if (status != 0) {
        fprintf(stderr, "Failed to open directory: %s\n", strerror(errno));
        vafs_reader_close(ctx->vafs);
        return -1;
    }

    while (vafs_directory_reader_next(dir, &entry) == 0) {
        size_t name_length;

        if (entry.Name == NULL) {
            continue;
        }

        name_length = strlen(entry.Name);
        if (name_length == 0) {
            continue;
        }

        if (ctx->entry_count == capacity) {
            size_t new_capacity = capacity == 0 ? 16 : capacity * 2;
            char** new_entries = realloc(ctx->entries, new_capacity * sizeof(char*));
            if (!new_entries) {
                fprintf(stderr, "Failed to grow entry list\n");
                vafs_directory_reader_close(dir);
                vafs_reader_close(ctx->vafs);
                return -1;
            }
            ctx->entries = new_entries;
            capacity = new_capacity;
        }

        ctx->entries[ctx->entry_count] = strdup(entry.Name);
        if (!ctx->entries[ctx->entry_count]) {
            fprintf(stderr, "Failed to store entry name\n");
            vafs_directory_reader_close(dir);
            vafs_reader_close(ctx->vafs);
            return -1;
        }
        ctx->entry_count++;
    }

    vafs_directory_reader_close(dir);
    if (ctx->entry_count == 0) {
        fprintf(stderr, "No entries found in directory %s\n", ctx->directory_path);
        vafs_reader_close(ctx->vafs);
        return -1;
    }

    return 0;
}

static int wide_lookup_run(void* user_data)
{
    WideLookupBenchmarkContext* ctx = (WideLookupBenchmarkContext*)user_data;
    struct VaFsMetadata statbuf;
    size_t index;
    char path_buffer[1024];
    int status;

    index = ctx->current_index % ctx->entry_count;
    ctx->current_index++;

    if (snprintf(path_buffer, sizeof(path_buffer), "%s/%s", ctx->directory_path, ctx->entries[index]) >= (int)sizeof(path_buffer)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    status = benchmark_path_stat(ctx->vafs, path_buffer, &statbuf);
    if (status != 0) {
        return -1;
    }

    return 0;
}

static void wide_lookup_teardown(void* user_data)
{
    WideLookupBenchmarkContext* ctx = (WideLookupBenchmarkContext*)user_data;
    size_t i;

    if (ctx->entries) {
        for (i = 0; i < ctx->entry_count; i++) {
            free(ctx->entries[i]);
        }
        free(ctx->entries);
    }

    if (ctx->vafs) {
        vafs_reader_close(ctx->vafs);
    }
}

static BenchmarkResult run_wide_lookup_benchmark(const char* image_path, const char* directory_path, const BenchmarkRunConfig* run_config)
{
    WideLookupBenchmarkContext ctx = {
        .image_path = image_path,
        .directory_path = directory_path,
        .vafs = NULL,
        .entries = NULL,
        .entry_count = 0,
        .current_index = 0
    };

    return benchmark_run(
        "Wide Directory Stat",
        run_config,
        wide_lookup_setup,
        wide_lookup_run,
        wide_lookup_teardown,
        &ctx
    );
}

// ============================
// Xattr Benchmarks
// ============================

static int xattr_get_setup(void* user_data)
{
    XattrBenchmarkContext* ctx = (XattrBenchmarkContext*)user_data;
    int                    status;

    status = xattr_benchmark_open(ctx);
    if (status != 0) {
        return -1;
    }

    status = benchmark_path_getxattr(ctx->vafs, ctx->path, ctx->name, NULL, 0, &ctx->buffer_size);
    if (status != 0) {
        xattr_benchmark_teardown_common(ctx);
        return -1;
    }

    ctx->buffer = (char*)malloc(ctx->buffer_size == 0 ? 1 : ctx->buffer_size);
    if (ctx->buffer == NULL) {
        xattr_benchmark_teardown_common(ctx);
        errno = ENOMEM;
        return -1;
    }

    // Warm the first xattr read once so timed iterations measure steady-state
    // lookup cost instead of first-use xattr-set materialization.
    status = benchmark_path_getxattr(ctx->vafs, ctx->path, ctx->name, ctx->buffer, ctx->buffer_size, &ctx->bytes_processed);
    if (status != 0) {
        xattr_benchmark_teardown_common(ctx);
        return -1;
    }
    return 0;
}

static int xattr_get_run(void* user_data)
{
    XattrBenchmarkContext* ctx = (XattrBenchmarkContext*)user_data;
    int                    status;

    status = benchmark_path_getxattr(ctx->vafs, ctx->path, ctx->name, ctx->buffer, ctx->buffer_size, &ctx->bytes_processed);
    if (status != 0) {
        return -1;
    }
    return 0;
}

static void xattr_get_teardown(void* user_data)
{
    xattr_benchmark_teardown_common((XattrBenchmarkContext*)user_data);
}

static BenchmarkResult run_xattr_get_benchmark(const BenchmarkRunConfig* run_config)
{
    XattrBenchmarkContext ctx = {
        .generated_image_path = { 0 },
        .path = "/xattr_target",
        .name = "user.checksum",
        .vafs = NULL,
        .buffer = NULL,
        .buffer_size = 0,
        .bytes_processed = 0
    };
    BenchmarkResult result = benchmark_run(
        "Repeated Xattr Get",
        run_config,
        xattr_get_setup,
        xattr_get_run,
        xattr_get_teardown,
        &ctx
    );

    result.bytes_processed = ctx.bytes_processed * result.iterations;
    result.throughput_mbps = benchmark_calculate_throughput(result.bytes_processed, result.total_time_ms);
    return result;
}

static int xattr_list_setup(void* user_data)
{
    XattrBenchmarkContext* ctx = (XattrBenchmarkContext*)user_data;
    int                    status;

    status = xattr_benchmark_open(ctx);
    if (status != 0) {
        return -1;
    }

    status = benchmark_path_listxattr(ctx->vafs, ctx->path, NULL, 0, &ctx->buffer_size);
    if (status != 0) {
        xattr_benchmark_teardown_common(ctx);
        return -1;
    }

    ctx->buffer = (char*)malloc(ctx->buffer_size == 0 ? 1 : ctx->buffer_size);
    if (ctx->buffer == NULL) {
        xattr_benchmark_teardown_common(ctx);
        errno = ENOMEM;
        return -1;
    }

    status = benchmark_path_listxattr(ctx->vafs, ctx->path, ctx->buffer, ctx->buffer_size, &ctx->bytes_processed);
    if (status != 0) {
        xattr_benchmark_teardown_common(ctx);
        return -1;
    }
    return 0;
}

static int xattr_list_run(void* user_data)
{
    XattrBenchmarkContext* ctx = (XattrBenchmarkContext*)user_data;
    int                    status;

    status = benchmark_path_listxattr(ctx->vafs, ctx->path, ctx->buffer, ctx->buffer_size, &ctx->bytes_processed);
    if (status != 0) {
        return -1;
    }
    return 0;
}

static void xattr_list_teardown(void* user_data)
{
    xattr_benchmark_teardown_common((XattrBenchmarkContext*)user_data);
}

static BenchmarkResult run_xattr_list_benchmark(const BenchmarkRunConfig* run_config)
{
    XattrBenchmarkContext ctx = {
        .generated_image_path = { 0 },
        .path = "/xattr_target",
        .name = NULL,
        .vafs = NULL,
        .buffer = NULL,
        .buffer_size = 0,
        .bytes_processed = 0
    };
    BenchmarkResult result = benchmark_run(
        "Repeated Xattr List",
        run_config,
        xattr_list_setup,
        xattr_list_run,
        xattr_list_teardown,
        &ctx
    );

    result.bytes_processed = ctx.bytes_processed * result.iterations;
    result.throughput_mbps = benchmark_calculate_throughput(result.bytes_processed, result.total_time_ms);
    return result;
}

// ============================
// Main Benchmark Runner
// ============================

static void print_usage(const char* program_name)
{
    printf("Usage: %s [OPTIONS] <image_path>\n", program_name);
    printf("\nOptions:\n");
    printf("  --format=<format>    Output format: human (default), json, csv\n");
    printf("  --iterations=<count> Override measured iterations for every benchmark\n");
    printf("  --warmup=<count>     Run untimed warmup iterations before each benchmark\n");
    printf("  --small-file=<path>  Path to small file in image for small file read benchmark\n");
    printf("  --large-file=<path>  Path to large file in image for large file read benchmark\n");
    printf("  --directory=<path>   Path to directory in image for traversal benchmark\n");
    printf("  --lookup-path=<path> Path for repeated lookup benchmark\n");
    printf("  --wide-directory=<path> Directory with many entries for wide lookup stat benchmark\n");
    printf("  --deep-path=<path>   Deep path for repeated stat benchmark\n");
    printf("  --only=<name>        Run a single benchmark (mount, traversal, small, large, lookup, deepstat, wide, xattrget, xattrlist, xattrs)\n");
    printf("  --help               Display this help message\n");
    printf("\nExamples:\n");
    printf("  %s test.vafs\n", program_name);
    printf("  %s --format=json --small-file=/config.txt test.vafs\n", program_name);
    printf("  %s --warmup=10 --iterations=100 test.vafs\n", program_name);
    printf("  %s --only=xattrs --warmup=50 --iterations=1000\n", program_name);
}

static int should_run_benchmark(const char* only_benchmark, const char* name)
{
    return (only_benchmark == NULL) || (strcmp(only_benchmark, name) == 0);
}

static int is_synthetic_xattr_only_selection(const char* only_benchmark)
{
    return only_benchmark != NULL &&
        (strcmp(only_benchmark, "xattrget") == 0 ||
         strcmp(only_benchmark, "xattrlist") == 0 ||
         strcmp(only_benchmark, "xattrs") == 0);
}

int main(int argc, char** argv)
{
    const char* image_path = NULL;
    const char* display_image = NULL;
    const char* output_format = "human";
    const char* small_file_path = "/small.txt";
    const char* large_file_path = "/large.bin";
    const char* directory_path = "/";
    const char* lookup_path = "/test.txt";
    const char* wide_directory_path = "/wide_dir";
    const char* deep_stat_path = "/lookup_test/subdir1/subdir2/subdir3/target.txt";
    const char* only_benchmark = NULL;
    BenchmarkCliOptions cli_options = { 0 };
    BenchmarkResult results[9];
    int result_count = 0;
    int i;

    // Parse command line arguments
    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--format=", 9) == 0) {
            output_format = argv[i] + 9;
        } else if (strncmp(argv[i], "--iterations=", 13) == 0) {
            if (parse_uint64_option("--iterations", argv[i] + 13, 0, &cli_options.iteration_override) != 0) {
                return 1;
            }
        } else if (strncmp(argv[i], "--warmup=", 9) == 0) {
            if (parse_uint64_option("--warmup", argv[i] + 9, 1, &cli_options.warmup_iterations) != 0) {
                return 1;
            }
        } else if (strncmp(argv[i], "--small-file=", 13) == 0) {
            small_file_path = argv[i] + 13;
        } else if (strncmp(argv[i], "--large-file=", 13) == 0) {
            large_file_path = argv[i] + 13;
        } else if (strncmp(argv[i], "--directory=", 12) == 0) {
            directory_path = argv[i] + 12;
        } else if (strncmp(argv[i], "--lookup-path=", 14) == 0) {
            lookup_path = argv[i] + 14;
        } else if (strncmp(argv[i], "--wide-directory=", 17) == 0) {
            wide_directory_path = argv[i] + 17;
        } else if (strncmp(argv[i], "--deep-path=", 12) == 0) {
            deep_stat_path = argv[i] + 12;
        } else if (strncmp(argv[i], "--only=", 7) == 0) {
            only_benchmark = argv[i] + 7;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            image_path = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!image_path && !is_synthetic_xattr_only_selection(only_benchmark)) {
        fprintf(stderr, "Error: No image path specified\n\n");
        print_usage(argv[0]);
        return 1;
    }

    if (image_path != NULL && access(image_path, R_OK) != 0) {
        fprintf(stderr, "Error: Cannot access image file: %s\n", image_path);
        return 1;
    }

    display_image = is_synthetic_xattr_only_selection(only_benchmark) ?
        "(synthetic xattr benchmark image)" :
        image_path;

    // Print header
    if (strcmp(output_format, "json") == 0) {
        printf("{\n");
        printf("  \"image\": ");
        benchmark_print_json_string(display_image);
        printf(",\n");
        printf("  \"benchmarks\": [\n");
    } else if (strcmp(output_format, "human") == 0) {
        printf("VaFS Benchmark Suite\n");
        printf("====================\n");
        printf("Image: %s\n", display_image);
    }

    // Run benchmarks
    if (should_run_benchmark(only_benchmark, "mount")) {
        BenchmarkRunConfig run_config = make_benchmark_run_config(MOUNT_LATENCY_ITERATIONS, &cli_options);
        results[result_count++] = run_mount_latency_benchmark(image_path, &run_config);
    }
    if (should_run_benchmark(only_benchmark, "traversal")) {
        BenchmarkRunConfig run_config = make_benchmark_run_config(METADATA_TRAVERSAL_ITERATIONS, &cli_options);
        results[result_count++] = run_metadata_traversal_benchmark(image_path, directory_path, &run_config);
    }
    if (should_run_benchmark(only_benchmark, "small")) {
        BenchmarkRunConfig run_config = make_benchmark_run_config(SMALL_FILE_READ_ITERATIONS, &cli_options);
        results[result_count++] = run_small_file_read_benchmark(image_path, small_file_path, &run_config);
    }
    if (should_run_benchmark(only_benchmark, "large")) {
        BenchmarkRunConfig run_config = make_benchmark_run_config(LARGE_FILE_READ_ITERATIONS, &cli_options);
        results[result_count++] = run_large_file_read_benchmark(image_path, large_file_path, &run_config);
    }
    if (should_run_benchmark(only_benchmark, "lookup")) {
        BenchmarkRunConfig run_config = make_benchmark_run_config(PATH_LOOKUP_ITERATIONS, &cli_options);
        results[result_count++] = run_path_lookup_benchmark(image_path, lookup_path, &run_config);
    }
    if (should_run_benchmark(only_benchmark, "deepstat")) {
        BenchmarkRunConfig run_config = make_benchmark_run_config(DEEP_STAT_ITERATIONS, &cli_options);
        results[result_count++] = run_deep_path_stat_benchmark(image_path, deep_stat_path, &run_config);
    }
    if (should_run_benchmark(only_benchmark, "wide")) {
        BenchmarkRunConfig run_config = make_benchmark_run_config(WIDE_LOOKUP_ITERATIONS, &cli_options);
        results[result_count++] = run_wide_lookup_benchmark(image_path, wide_directory_path, &run_config);
    }
    if (should_run_benchmark(only_benchmark, "xattrget") || should_run_benchmark(only_benchmark, "xattrs")) {
        BenchmarkRunConfig run_config = make_benchmark_run_config(XATTR_GET_ITERATIONS, &cli_options);
        results[result_count++] = run_xattr_get_benchmark(&run_config);
    }
    if (should_run_benchmark(only_benchmark, "xattrlist") || should_run_benchmark(only_benchmark, "xattrs")) {
        BenchmarkRunConfig run_config = make_benchmark_run_config(XATTR_LIST_ITERATIONS, &cli_options);
        results[result_count++] = run_xattr_list_benchmark(&run_config);
    }

    // Print results
    if (strcmp(output_format, "json") == 0) {
        for (i = 0; i < result_count; i++) {
            benchmark_print_result_json(&results[i], i == result_count - 1);
        }
        printf("  ]\n");
        printf("}\n");
    } else if (strcmp(output_format, "csv") == 0) {
        for (i = 0; i < result_count; i++) {
            benchmark_print_result_csv(&results[i], i == 0);
        }
    } else {
        for (i = 0; i < result_count; i++) {
            benchmark_print_result(&results[i]);
        }
    }

    return 0;
}
