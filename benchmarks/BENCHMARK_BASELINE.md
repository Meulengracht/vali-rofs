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
Iterations:    5
Total time:    0.116 ms
Average time:  0.023 ms
Min time:      0.011 ms
Max time:      0.060 ms
```

**What this measures**: Header parsing, stream device initialization, block cache setup.

### Metadata Traversal
Measures directory listing and metadata operation performance.

```
Iterations:    3
Total time:    0.053 ms
Average time:  0.018 ms
Min time:      0.000 ms
Max time:      0.052 ms
```

**What this measures**: Directory entry parsing, linked list traversal, descriptor stream reads.

### Small File Read (4KB)
Measures random access performance for small files.

```
Iterations:    10
Total time:    2.063 ms
Average time:  0.206 ms
Min time:      0.005 ms
Max time:      1.064 ms
Bytes:         40960
Throughput:    18.93 MB/s
```

**What this measures**: File open/close overhead, small read performance, block cache effectiveness.

### Large File Sequential Read
Measures sequential I/O throughput for large files.

```
Iterations:    2
Total time:    125.288 ms
Average time:  62.644 ms
Min time:      37.121 ms
Max time:      88.168 ms
Bytes:         10485760
Throughput:    79.82 MB/s
```

**What this measures**: Sequential read performance, block cache streaming, data stream efficiency.

### Repeated Path Lookup
Measures path resolution and file lookup performance.

```
Iterations:    5
Total time:    0.049 ms
Average time:  0.010 ms
Min time:      0.000 ms
Max time:      0.048 ms
```

**What this measures**: Path tokenization, directory traversal, descriptor lookups, open/close overhead.

### Deep Path Stat
Measures repeated stat on a long nested path.

```
Iterations:    10
Total time:    0.083 ms
Average time:  0.008 ms
Min time:      0.000 ms
Max time:      0.080 ms
```

**What this measures**: Path tokenization on long paths, multi-level descriptor traversal, symlink resolution overhead (if present).

### Wide Directory Stat
Measures metadata lookup behavior in wide directories.

```
Iterations:    5
Total time:    0.004 ms
Average time:  0.001 ms
Min time:      0.000 ms
Max time:      0.003 ms
```

**What this measures**: Directory scanning cost when many siblings exist, descriptor lookup locality.

## Analysis

(To be filled after capturing actual results)

### Performance Characteristics

- **Mount latency**: ~0.023 ms avg
- **Metadata operations**: ~0.018 ms avg traversal of root
- **Small file throughput**: ~18.9 MB/s
- **Large file throughput**: ~79.8 MB/s (5 MB file, BriefLZ)
- **Path lookup**: ~0.010 ms avg repeated open/close
- **Deep path stat**: ~0.008 ms avg
- **Wide directory stat**: ~0.001 ms avg across 500 siblings

### Observations

- Metadata costs are negligible compared to I/O; even deep paths and wide directories resolve in microseconds.
- Large file sequential reads dominate overall time because of BriefLZ decompression; improving streaming would yield the biggest win.
- `vafs_file_read` does not advance file position; sequential readers (including the benchmark) must seek manually between reads.

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
