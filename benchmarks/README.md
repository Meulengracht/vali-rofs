# VaFS Benchmark Suite

This directory contains a comprehensive benchmark suite for measuring VaFS (Vali Filesystem) performance across various rootfs-oriented workloads.

## Overview

The benchmark suite measures seven core workload categories:

1. **Mount Latency** - Time to open and initialize a VaFS image
2. **Metadata Traversal** - Speed of directory listing and metadata operations
3. **Small File Read** - Performance reading small files (4KB)
4. **Large File Sequential Read** - Throughput for reading large files sequentially
5. **Repeated Path Lookup** - Path resolution performance with repeated lookups
6. **Deep Path Stat** - Repeated `stat` on a long/deep path
7. **Wide Directory Stat** - Repeated `stat` across many siblings in a wide directory

## Building

The benchmarks are built automatically with the project if `VAFS_BUILD_BENCHMARKS` is enabled (default: ON).

```bash
mkdir build && cd build
cmake ..
make
```

This produces:
- `build/bin/vafs-bench` - Main benchmark executable
- `build/lib/libvafs-benchmark.a` - Benchmark framework library

To disable benchmark building:
```bash
cmake -DVAFS_BUILD_BENCHMARKS=OFF ..
```

## Generating Test Data

Before running benchmarks, generate test data and a VaFS image:

```bash
cd benchmarks
./generate_test_data.sh
```

This creates:
- `/tmp/vafs-benchmark-data/source/` - Test files and directories
- `/tmp/vafs-benchmark-data/benchmark.vafs` - VaFS image for benchmarking

The test data includes:
- 100 small files (1-10KB each)
- 10 large files (1-10MB each)
- A directory with 500 files (for traversal testing)
- Deep directory structures (for path lookup testing)
- Various file types and sizes

You can customize the output location:
```bash
TEST_DATA_DIR=/path/to/data OUTPUT_IMAGE=/path/to/image.vafs ./generate_test_data.sh
```

## Running Benchmarks

### Basic Usage

```bash
./build/bin/vafs-bench /tmp/vafs-benchmark-data/benchmark.vafs
```

### Output Formats

The benchmark suite supports three output formats:

#### Human-Readable (default)

```bash
./build/bin/vafs-bench --format=human benchmark.vafs
```

Example output:
```
VaFS Benchmark Suite
====================
Image: benchmark.vafs

=== Mount Latency ===
Iterations:    100
Total time:    245.123 ms
Average time:  2.451 ms
Min time:      2.123 ms
Max time:      5.678 ms

=== Metadata Traversal ===
Iterations:    50
Total time:    123.456 ms
Average time:  2.469 ms
Min time:      2.123 ms
Max time:      3.456 ms
...
```

#### JSON Format

```bash
./build/bin/vafs-bench --format=json benchmark.vafs
```

Example output:
```json
{
  "image": "benchmark.vafs",
  "benchmarks": [
    {
      "name": "Mount Latency",
      "iterations": 100,
      "total_time_ms": 245.123,
      "avg_time_ms": 2.451,
      "min_time_ms": 2.123,
      "max_time_ms": 5.678
    },
    {
      "name": "Small File Read (4KB)",
      "iterations": 1000,
      "total_time_ms": 456.789,
      "avg_time_ms": 0.457,
      "min_time_ms": 0.234,
      "max_time_ms": 1.234,
      "bytes_processed": 4096000,
      "throughput_mbps": 8.54
    }
  ]
}
```

#### CSV Format

```bash
./build/bin/vafs-bench --format=csv benchmark.vafs > results.csv
```

Example output:
```csv
name,iterations,total_time_ms,avg_time_ms,min_time_ms,max_time_ms,bytes_processed,throughput_mbps
Mount Latency,100,245.123,2.451,2.123,5.678,0,0.00
Metadata Traversal,50,123.456,2.469,2.123,3.456,0,0.00
Small File Read (4KB),1000,456.789,0.457,0.234,1.234,4096000,8.54
...
```

### Custom Test Files

You can specify custom paths within the VaFS image for testing:

```bash
./build/bin/vafs-bench \
  --small-file=/small.txt \
  --large-file=/large.bin \
  --directory=/wide_dir \
  --lookup-path=/lookup_test/subdir1/subdir2/subdir3/target.txt \
  --wide-directory=/wide_dir \
  --deep-path=/lookup_test/subdir1/subdir2/subdir3/target.txt \
  benchmark.vafs
```

### Options

- `--format=<format>` - Output format: `human`, `json`, or `csv` (default: human)
- `--small-file=<path>` - Path to small file in image (default: `/small.txt`)
- `--large-file=<path>` - Path to large file in image (default: `/large.bin`)
- `--directory=<path>` - Directory path for traversal benchmark (default: `/`)
- `--lookup-path=<path>` - Path for repeated lookup benchmark (default: `/test.txt`)
- `--wide-directory=<path>` - Directory with many entries for wide lookup benchmark (default: `/wide_dir`)
- `--deep-path=<path>` - Deep path for repeated `stat` benchmark (default: `/lookup_test/subdir1/subdir2/subdir3/target.txt`)
- `--only=<name>` - Run a single benchmark (`mount`, `traversal`, `small`, `large`, `lookup`, `deepstat`, `wide`)
- `--help` - Display help message

## Benchmark Details

### 1. Mount Latency

**Purpose**: Measure the time to open and initialize a VaFS image.

**Methodology**:
- Opens the VaFS image file
- Parses headers and initializes internal structures
- Closes the image
- Repeats 100 times

**Metrics**:
- Average mount time (ms)
- Min/max latency
- Total time for all iterations

**What it measures**:
- Header parsing overhead
- Stream device initialization
- Block cache setup

### 2. Metadata Traversal

**Purpose**: Measure directory listing and metadata operation performance.

**Methodology**:
- Opens a directory in the VaFS image
- Reads all directory entries sequentially
- Closes the directory handle
- Repeats 50 times

**Metrics**:
- Average traversal time (ms)
- Min/max latency
- Entries processed per iteration

**What it measures**:
- Directory entry parsing
- Linked list traversal
- Descriptor stream reads

### 3. Small File Read (4KB)

**Purpose**: Measure random access performance for small files.

**Methodology**:
- Opens a 4KB file
- Reads entire file into memory
- Closes file handle
- Repeats 10 times by default

**Metrics**:
- Average read time (ms)
- Throughput (MB/s)
- Total bytes processed

**What it measures**:
- File open/close overhead
- Small read performance
- Block cache effectiveness
- Path resolution for each open

### 4. Large File Sequential Read

**Purpose**: Measure sequential I/O throughput for large files.

**Methodology**:
- Opens a large file (5MB)
- Reads entire file in 128KB chunks
- Seeks back to beginning
- Repeats 2 times by default

**Metrics**:
- Average read time (ms)
- Throughput (MB/s)
- Total bytes processed

**What it measures**:
- Sequential read performance
- Block cache streaming behavior
- Data stream efficiency
- Large buffer handling

### 5. Repeated Path Lookup

**Purpose**: Measure path resolution and file lookup performance.

**Methodology**:
- Resolves path to a file
- Opens file handle
- Closes file handle immediately
- Repeats 5 times by default

**Metrics**:
- Average lookup time (ms)
- Min/max latency

**What it measures**:
- Path tokenization and parsing
- Directory traversal
- Descriptor lookups
- Open/close overhead

### 6. Deep Path Stat

**Purpose**: Measure repeated metadata lookups on a long nested path.

**Methodology**:
- Calls `vafs_path_stat` on a deep path
- Resolves symlinks when present
- Repeats 10 times by default

**Metrics**:
- Average stat time (ms)
- Min/max latency

**What it measures**:
- Path tokenization on long paths
- Descriptor traversal through multiple levels
- Symlink resolution overhead (if present)

### 7. Wide Directory Stat

**Purpose**: Measure metadata lookup performance across many siblings.

**Methodology**:
- Enumerates all names in a wide directory once
- Repeatedly `stat`s entries in a round-robin fashion
- Repeats 5 times by default

**Metrics**:
- Average stat time (ms)
- Min/max latency

**What it measures**:
- Directory entry scanning in wide directories
- Descriptor lookup behavior under high sibling counts

## Benchmark Configuration

The default iteration counts are defined in `vafs_bench.c`:

```c
#define MOUNT_LATENCY_ITERATIONS       5
#define METADATA_TRAVERSAL_ITERATIONS  3
#define SMALL_FILE_READ_ITERATIONS     10
#define LARGE_FILE_READ_ITERATIONS     2
#define PATH_LOOKUP_ITERATIONS         5
#define WIDE_LOOKUP_ITERATIONS         5
#define DEEP_STAT_ITERATIONS           10
```

These can be adjusted by modifying the source and rebuilding, or you can focus on a single benchmark with `--only=<name>`.

## Output Format Specification

### Human Format

Plain text format designed for terminal display. Each benchmark shows:
- Name
- Number of iterations
- Total time in milliseconds
- Average, minimum, and maximum time per iteration
- For I/O benchmarks: bytes processed and throughput in MB/s

### JSON Format

Machine-readable JSON with the following structure:

```json
{
  "image": "path/to/image.vafs",
  "benchmarks": [
    {
      "name": "Benchmark Name",
      "iterations": 100,
      "total_time_ms": 123.456,
      "avg_time_ms": 1.234,
      "min_time_ms": 0.987,
      "max_time_ms": 2.345,
      "bytes_processed": 1234567,      // Optional, for I/O benchmarks
      "throughput_mbps": 12.34          // Optional, for I/O benchmarks
    }
  ]
}
```

All time values are in milliseconds with 3 decimal places. Throughput is in MB/s with 2 decimal places.

### CSV Format

Comma-separated values suitable for spreadsheet import and analysis:

```
name,iterations,total_time_ms,avg_time_ms,min_time_ms,max_time_ms,bytes_processed,throughput_mbps
```

For non-I/O benchmarks, `bytes_processed` is 0 and `throughput_mbps` is 0.00.

## Baseline Results

Baseline results are captured in `BENCHMARK_BASELINE.md` for the reference system.

## Performance Tips

### Reducing Variability

1. **Disable CPU frequency scaling**:
   ```bash
   sudo cpupower frequency-set --governor performance
   ```

2. **Clear caches** between runs:
   ```bash
   sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'
   ```

3. **Run on idle system** - Close unnecessary applications

4. **Use consistent test data** - Same VaFS image for all runs

### Interpreting Results

- **Mount Latency**: Lower is better. Should be <5ms for typical images.
- **Metadata Traversal**: Depends on entry count. Look for consistent times.
- **Small File Read**: High overhead expected due to open/close per iteration.
- **Large File Sequential Read**: Throughput should approach storage limits.
- **Path Lookup**: Lower is better. Cache effects may be visible.

## Extending the Benchmark Suite

To add new benchmarks:

1. Define context structure for benchmark state
2. Implement setup/benchmark/teardown functions
3. Create a run function that calls `benchmark_run()`
4. Add to main benchmark runner
5. Update documentation

Example:

```c
typedef struct {
    const char* image_path;
    struct VaFs* vafs;
    // ... benchmark-specific fields
} MyBenchmarkContext;

static int my_benchmark_setup(void* user_data) {
    // Initialize resources
    return 0;
}

static int my_benchmark_run(void* user_data) {
    // Perform one iteration of benchmark
    return 0;
}

static void my_benchmark_teardown(void* user_data) {
    // Clean up resources
}

static BenchmarkResult run_my_benchmark(const char* image_path) {
    MyBenchmarkContext ctx = { .image_path = image_path };
    return benchmark_run(
        "My Benchmark Name",
        ITERATIONS,
        my_benchmark_setup,
        my_benchmark_run,
        my_benchmark_teardown,
        &ctx
    );
}
```

## Framework API

The benchmark framework (`benchmark.h`) provides:

### Timer Functions

```c
void benchmark_timer_start(BenchmarkTimer* timer);
double benchmark_timer_stop(BenchmarkTimer* timer);
double benchmark_timespec_diff_ms(struct timespec* start, struct timespec* end);
```

### Result Output

```c
void benchmark_print_result(const BenchmarkResult* result);
void benchmark_print_result_json(const BenchmarkResult* result, int is_last);
void benchmark_print_result_csv(const BenchmarkResult* result, int print_header);
```

### Benchmark Runner

```c
BenchmarkResult benchmark_run(
    const char* name,
    uint64_t iterations,
    int (*setup_fn)(void* user_data),
    int (*benchmark_fn)(void* user_data),
    void (*teardown_fn)(void* user_data),
    void* user_data
);
```

### Utility Functions

```c
double benchmark_calculate_throughput(uint64_t bytes, double time_ms);
```

## License

Same as VaFS project (GPL-3.0).
