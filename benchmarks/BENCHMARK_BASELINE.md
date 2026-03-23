# VaFS Benchmark Baseline Results

This document contains baseline benchmark results for the VaFS read-only filesystem.

## Test Environment

- **Date**: 2026-03-23
- **Platform**: Linux
- **Kernel**: 6.14.0-1017-azure
- **CPU**: (auto-detected)
- **Compression**: BriefLZ
- **Test Image**: /tmp/vafs-benchmark-data/benchmark.vafs
- **Test Image Size**: ~16MB compressed
- **Test Data**:
  - 617 files total
  - 100 small files (1-10KB each)
  - 10 large files (1-10MB each)
  - 500 files in wide_dir for traversal
  - Deep directory structures for path lookup

## Benchmark Results

### Mount Latency
Measures the time to open and initialize a VaFS image.

```
Iterations:    100
Total time:    TBD ms
Average time:  TBD ms
Min time:      TBD ms
Max time:      TBD ms
```

**What this measures**: Header parsing, stream device initialization, block cache setup.

### Metadata Traversal
Measures directory listing and metadata operation performance.

```
Iterations:    50
Total time:    TBD ms
Average time:  TBD ms
Min time:      TBD ms
Max time:      TBD ms
```

**What this measures**: Directory entry parsing, linked list traversal, descriptor stream reads.

### Small File Read (4KB)
Measures random access performance for small files.

```
Iterations:    1000
Total time:    TBD ms
Average time:  TBD ms
Min time:      TBD ms
Max time:      TBD ms
Bytes:         TBD
Throughput:    TBD MB/s
```

**What this measures**: File open/close overhead, small read performance, block cache effectiveness.

### Large File Sequential Read
Measures sequential I/O throughput for large files.

```
Iterations:    50
Total time:    TBD ms
Average time:  TBD ms
Min time:      TBD ms
Max time:      TBD ms
Bytes:         TBD
Throughput:    TBD MB/s
```

**What this measures**: Sequential read performance, block cache streaming, data stream efficiency.

### Repeated Path Lookup
Measures path resolution and file lookup performance.

```
Iterations:    1000
Total time:    TBD ms
Average time:  TBD ms
Min time:      TBD ms
Max time:      TBD ms
```

**What this measures**: Path tokenization, directory traversal, descriptor lookups, open/close overhead.

## Analysis

(To be filled after capturing actual results)

### Performance Characteristics

- **Mount latency**: TBD
- **Metadata operations**: TBD
- **Small file throughput**: TBD
- **Large file throughput**: TBD
- **Path lookup**: TBD

### Observations

(To be filled after capturing actual results)

## Reproducing Results

To reproduce these benchmarks:

```bash
# 1. Build the project
mkdir build && cd build
cmake .. -DVAFS_BUILD_BENCHMARKS=ON
make

# 2. Generate test data
cd ../benchmarks
./generate_test_data.sh

# 3. Run benchmarks
../build/bin/vafs-bench /tmp/vafs-benchmark-data/benchmark.vafs

# For JSON output:
../build/bin/vafs-bench --format=json /tmp/vafs-benchmark-data/benchmark.vafs

# For CSV output:
../build/bin/vafs-bench --format=csv /tmp/vafs-benchmark-data/benchmark.vafs > results.csv
```

## Notes

- Results may vary based on system load, CPU frequency scaling, and I/O caching
- For consistent results, disable CPU frequency scaling and clear caches between runs
- The benchmarks use compressed data (BriefLZ) which adds decompression overhead
- Mount latency includes filter installation time
