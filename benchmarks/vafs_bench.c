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

#include "filter.h"
#include "benchmark.h"
#include <vafs/vafs.h>
#include <vafs/file.h>
#include <vafs/directory.h>
#include <vafs/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

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
    struct VaFsFileHandle* handle;
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

// ============================
// Mount Latency Benchmark
// ============================

static int mount_benchmark_setup(void* user_data)
{
    // No setup needed
    return 0;
}

static int mount_benchmark_run(void* user_data)
{
    MountBenchmarkContext* ctx = (MountBenchmarkContext*)user_data;
    struct VaFs* vafs = NULL;
    int status;

    status = vafs_open_file(ctx->image_path, &vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to open VaFS image: %s\n", strerror(errno));
        return -1;
    }

    // Install decompression filters if needed
    status = __handle_filter(vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to install filters: %s\n", strerror(errno));
        vafs_close(vafs);
        return -1;
    }

    vafs_close(vafs);
    return 0;
}

static void mount_benchmark_teardown(void* user_data)
{
    // No teardown needed
}

static BenchmarkResult run_mount_latency_benchmark(const char* image_path)
{
    MountBenchmarkContext ctx = {
        .image_path = image_path,
        .vafs = NULL
    };

    return benchmark_run(
        "Mount Latency",
        MOUNT_LATENCY_ITERATIONS,
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

    status = vafs_open_file(ctx->image_path, &ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to open VaFS image: %s\n", strerror(errno));
        return -1;
    }

    // Install decompression filters if needed
    status = __handle_filter(ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to install filters: %s\n", strerror(errno));
        vafs_close(ctx->vafs);
        return -1;
    }

    return 0;
}

static int traversal_benchmark_run(void* user_data)
{
    TraversalBenchmarkContext* ctx = (TraversalBenchmarkContext*)user_data;
    struct VaFsDirectoryHandle* dir = NULL;
    struct VaFsEntry entry;
    int status;
    int count = 0;

    status = vafs_directory_open(ctx->vafs, ctx->directory_path, &dir);
    if (status != 0) {
        fprintf(stderr, "Failed to open directory: %s\n", strerror(errno));
        return -1;
    }

    while (vafs_directory_read(dir, &entry) == 0) {
        count++;
    }

    ctx->entry_count = count;
    vafs_directory_close(dir);
    return 0;
}

static void traversal_benchmark_teardown(void* user_data)
{
    TraversalBenchmarkContext* ctx = (TraversalBenchmarkContext*)user_data;
    if (ctx->vafs) {
        vafs_close(ctx->vafs);
    }
}

static BenchmarkResult run_metadata_traversal_benchmark(const char* image_path, const char* directory_path)
{
    TraversalBenchmarkContext ctx = {
        .image_path = image_path,
        .directory_path = directory_path,
        .vafs = NULL,
        .entry_count = 0
    };

    BenchmarkResult result = benchmark_run(
        "Metadata Traversal",
        METADATA_TRAVERSAL_ITERATIONS,
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

    status = vafs_open_file(ctx->image_path, &ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to open VaFS image: %s\n", strerror(errno));
        return -1;
    }

    // Install decompression filters if needed
    status = __handle_filter(ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to install filters: %s\n", strerror(errno));
        vafs_close(ctx->vafs);
        return -1;
    }

    ctx->buffer = (char*)malloc(ctx->buffer_size);
    if (!ctx->buffer) {
        fprintf(stderr, "Failed to allocate read buffer\n");
        vafs_close(ctx->vafs);
        return -1;
    }

    return 0;
}

static int small_file_read_run(void* user_data)
{
    FileReadBenchmarkContext* ctx = (FileReadBenchmarkContext*)user_data;
    struct VaFsFileHandle* handle = NULL;
    size_t bytes_read;
    int status;

    status = vafs_file_open(ctx->vafs, ctx->file_path, &handle);
    if (status != 0) {
        fprintf(stderr, "Failed to open file: %s\n", strerror(errno));
        return -1;
    }

    bytes_read = vafs_file_read(handle, ctx->buffer, ctx->buffer_size);
    ctx->bytes_read = bytes_read;

    vafs_file_close(handle);
    return 0;
}

static void small_file_read_teardown(void* user_data)
{
    FileReadBenchmarkContext* ctx = (FileReadBenchmarkContext*)user_data;
    if (ctx->buffer) {
        free(ctx->buffer);
    }
    if (ctx->vafs) {
        vafs_close(ctx->vafs);
    }
}

static BenchmarkResult run_small_file_read_benchmark(const char* image_path, const char* file_path)
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
        SMALL_FILE_READ_ITERATIONS,
        small_file_read_setup,
        small_file_read_run,
        small_file_read_teardown,
        &ctx
    );

    // Calculate throughput
    result.bytes_processed = ctx.bytes_read * SMALL_FILE_READ_ITERATIONS;
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

    status = vafs_open_file(ctx->image_path, &ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to open VaFS image: %s\n", strerror(errno));
        return -1;
    }

    // Install decompression filters if needed
    status = __handle_filter(ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to install filters: %s\n", strerror(errno));
        vafs_close(ctx->vafs);
        return -1;
    }

    status = vafs_file_open(ctx->vafs, ctx->file_path, &ctx->handle);
    if (status != 0) {
        fprintf(stderr, "Failed to open file: %s\n", strerror(errno));
        vafs_close(ctx->vafs);
        return -1;
    }

    ctx->buffer = (char*)malloc(ctx->buffer_size);
    if (!ctx->buffer) {
        fprintf(stderr, "Failed to allocate read buffer\n");
        vafs_file_close(ctx->handle);
        vafs_close(ctx->vafs);
        return -1;
    }

    return 0;
}

static int large_file_read_run(void* user_data)
{
    FileReadBenchmarkContext* ctx = (FileReadBenchmarkContext*)user_data;
    size_t total_read = 0;
    size_t bytes_read;

    // Reset file position to beginning
    vafs_file_seek(ctx->handle, 0, SEEK_SET);

    // Read entire file in chunks
    while ((bytes_read = vafs_file_read(ctx->handle, ctx->buffer, ctx->buffer_size)) > 0) {
        total_read += bytes_read;
        vafs_file_seek(ctx->handle, (long)bytes_read, SEEK_CUR);
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
        vafs_file_close(ctx->handle);
    }
    if (ctx->vafs) {
        vafs_close(ctx->vafs);
    }
}

static BenchmarkResult run_large_file_read_benchmark(const char* image_path, const char* file_path)
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
        LARGE_FILE_READ_ITERATIONS,
        large_file_read_setup,
        large_file_read_run,
        large_file_read_teardown,
        &ctx
    );

    // Calculate throughput
    result.bytes_processed = ctx.bytes_read * LARGE_FILE_READ_ITERATIONS;
    result.throughput_mbps = benchmark_calculate_throughput(result.bytes_processed, result.total_time_ms);

    return result;
}

// ============================
// Path Lookup Benchmark
// ============================

static int path_lookup_setup(void* user_data)
{
    PathLookupBenchmarkContext* ctx = (PathLookupBenchmarkContext*)user_data;
    int status;

    status = vafs_open_file(ctx->image_path, &ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to open VaFS image: %s\n", strerror(errno));
        return -1;
    }

    // Install decompression filters if needed
    status = __handle_filter(ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to install filters: %s\n", strerror(errno));
        vafs_close(ctx->vafs);
        return -1;
    }

    return 0;
}

static int path_lookup_run(void* user_data)
{
    PathLookupBenchmarkContext* ctx = (PathLookupBenchmarkContext*)user_data;
    struct VaFsFileHandle* handle = NULL;
    int status;

    status = vafs_file_open(ctx->vafs, ctx->path, &handle);
    if (status != 0) {
        return -1;
    }

    vafs_file_close(handle);
    return 0;
}

static void path_lookup_teardown(void* user_data)
{
    PathLookupBenchmarkContext* ctx = (PathLookupBenchmarkContext*)user_data;
    if (ctx->vafs) {
        vafs_close(ctx->vafs);
    }
}

static BenchmarkResult run_path_lookup_benchmark(const char* image_path, const char* path)
{
    PathLookupBenchmarkContext ctx = {
        .image_path = image_path,
        .path = path,
        .vafs = NULL
    };

    return benchmark_run(
        "Repeated Path Lookup",
        PATH_LOOKUP_ITERATIONS,
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
    int status;

    status = vafs_open_file(ctx->image_path, &ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to open VaFS image: %s\n", strerror(errno));
        return -1;
    }

    status = __handle_filter(ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to install filters: %s\n", strerror(errno));
        vafs_close(ctx->vafs);
        return -1;
    }

    return 0;
}

static int path_stat_run(void* user_data)
{
    PathStatBenchmarkContext* ctx = (PathStatBenchmarkContext*)user_data;
    struct vafs_stat statbuf;
    int status;

    status = vafs_path_stat(ctx->vafs, ctx->path, 1, &statbuf);
    if (status != 0) {
        return -1;
    }

    return 0;
}

static void path_stat_teardown(void* user_data)
{
    PathStatBenchmarkContext* ctx = (PathStatBenchmarkContext*)user_data;
    if (ctx->vafs) {
        vafs_close(ctx->vafs);
    }
}

static BenchmarkResult run_deep_path_stat_benchmark(const char* image_path, const char* path)
{
    PathStatBenchmarkContext ctx = {
        .image_path = image_path,
        .path = path,
        .vafs = NULL
    };

    return benchmark_run(
        "Deep Path Stat",
        DEEP_STAT_ITERATIONS,
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
    struct VaFsDirectoryHandle* dir = NULL;
    struct VaFsEntry entry;
    int status;
    size_t capacity = 0;

    status = vafs_open_file(ctx->image_path, &ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to open VaFS image: %s\n", strerror(errno));
        return -1;
    }

    status = __handle_filter(ctx->vafs);
    if (status != 0) {
        fprintf(stderr, "Failed to install filters: %s\n", strerror(errno));
        vafs_close(ctx->vafs);
        return -1;
    }

    status = vafs_directory_open(ctx->vafs, ctx->directory_path, &dir);
    if (status != 0) {
        fprintf(stderr, "Failed to open directory: %s\n", strerror(errno));
        vafs_close(ctx->vafs);
        return -1;
    }

    while (vafs_directory_read(dir, &entry) == 0) {
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
                vafs_directory_close(dir);
                vafs_close(ctx->vafs);
                return -1;
            }
            ctx->entries = new_entries;
            capacity = new_capacity;
        }

        ctx->entries[ctx->entry_count] = strdup(entry.Name);
        if (!ctx->entries[ctx->entry_count]) {
            fprintf(stderr, "Failed to store entry name\n");
            vafs_directory_close(dir);
            vafs_close(ctx->vafs);
            return -1;
        }
        ctx->entry_count++;
    }

    vafs_directory_close(dir);
    if (ctx->entry_count == 0) {
        fprintf(stderr, "No entries found in directory %s\n", ctx->directory_path);
        vafs_close(ctx->vafs);
        return -1;
    }

    return 0;
}

static int wide_lookup_run(void* user_data)
{
    WideLookupBenchmarkContext* ctx = (WideLookupBenchmarkContext*)user_data;
    struct vafs_stat statbuf;
    size_t index;
    char path_buffer[1024];
    int status;

    index = ctx->current_index % ctx->entry_count;
    ctx->current_index++;

    if (snprintf(path_buffer, sizeof(path_buffer), "%s/%s", ctx->directory_path, ctx->entries[index]) >= (int)sizeof(path_buffer)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    status = vafs_path_stat(ctx->vafs, path_buffer, 1, &statbuf);
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
        vafs_close(ctx->vafs);
    }
}

static BenchmarkResult run_wide_lookup_benchmark(const char* image_path, const char* directory_path)
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
        WIDE_LOOKUP_ITERATIONS,
        wide_lookup_setup,
        wide_lookup_run,
        wide_lookup_teardown,
        &ctx
    );
}

// ============================
// Main Benchmark Runner
// ============================

static void print_usage(const char* program_name)
{
    printf("Usage: %s [OPTIONS] <image_path>\n", program_name);
    printf("\nOptions:\n");
    printf("  --format=<format>    Output format: human (default), json, csv\n");
    printf("  --small-file=<path>  Path to small file in image for small file read benchmark\n");
    printf("  --large-file=<path>  Path to large file in image for large file read benchmark\n");
    printf("  --directory=<path>   Path to directory in image for traversal benchmark\n");
    printf("  --lookup-path=<path> Path for repeated lookup benchmark\n");
    printf("  --wide-directory=<path> Directory with many entries for wide lookup stat benchmark\n");
    printf("  --deep-path=<path>   Deep path for repeated stat benchmark\n");
    printf("  --only=<name>        Run a single benchmark (mount, traversal, small, large, lookup, deepstat, wide)\n");
    printf("  --help               Display this help message\n");
    printf("\nExamples:\n");
    printf("  %s test.vafs\n", program_name);
    printf("  %s --format=json --small-file=/config.txt test.vafs\n", program_name);
}

static int should_run_benchmark(const char* only_benchmark, const char* name)
{
    return (only_benchmark == NULL) || (strcmp(only_benchmark, name) == 0);
}

int main(int argc, char** argv)
{
    const char* image_path = NULL;
    const char* output_format = "human";
    const char* small_file_path = "/small.txt";
    const char* large_file_path = "/large.bin";
    const char* directory_path = "/";
    const char* lookup_path = "/test.txt";
    const char* wide_directory_path = "/wide_dir";
    const char* deep_stat_path = "/lookup_test/subdir1/subdir2/subdir3/target.txt";
    const char* only_benchmark = NULL;
    BenchmarkResult results[7];
    int result_count = 0;
    int i;

    // Parse command line arguments
    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--format=", 9) == 0) {
            output_format = argv[i] + 9;
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

    if (!image_path) {
        fprintf(stderr, "Error: No image path specified\n\n");
        print_usage(argv[0]);
        return 1;
    }

    // Check if image file exists
    if (access(image_path, R_OK) != 0) {
        fprintf(stderr, "Error: Cannot access image file: %s\n", image_path);
        return 1;
    }

    // Print header
    if (strcmp(output_format, "json") == 0) {
        printf("{\n");
        printf("  \"image\": \"%s\",\n", image_path);
        printf("  \"benchmarks\": [\n");
    } else if (strcmp(output_format, "human") == 0) {
        printf("VaFS Benchmark Suite\n");
        printf("====================\n");
        printf("Image: %s\n", image_path);
    }

    // Run benchmarks
    if (should_run_benchmark(only_benchmark, "mount")) {
        results[result_count++] = run_mount_latency_benchmark(image_path);
    }
    if (should_run_benchmark(only_benchmark, "traversal")) {
        results[result_count++] = run_metadata_traversal_benchmark(image_path, directory_path);
    }
    if (should_run_benchmark(only_benchmark, "small")) {
        results[result_count++] = run_small_file_read_benchmark(image_path, small_file_path);
    }
    if (should_run_benchmark(only_benchmark, "large")) {
        results[result_count++] = run_large_file_read_benchmark(image_path, large_file_path);
    }
    if (should_run_benchmark(only_benchmark, "lookup")) {
        results[result_count++] = run_path_lookup_benchmark(image_path, lookup_path);
    }
    if (should_run_benchmark(only_benchmark, "deepstat")) {
        results[result_count++] = run_deep_path_stat_benchmark(image_path, deep_stat_path);
    }
    if (should_run_benchmark(only_benchmark, "wide")) {
        results[result_count++] = run_wide_lookup_benchmark(image_path, wide_directory_path);
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
