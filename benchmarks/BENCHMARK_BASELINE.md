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

### 2026-03-23 Linux Baseline Snapshot

- **Mount latency**: ~0.023 ms avg
- **Metadata operations**: ~0.018 ms avg traversal of root
- **Small file throughput**: ~18.9 MB/s
- **Large file throughput**: ~79.8 MB/s (5 MB file, BriefLZ)
- **Path lookup**: ~0.010 ms avg repeated open/close
- **Deep path stat**: ~0.008 ms avg
- **Wide directory stat**: ~0.001 ms avg across 500 siblings

### 2026-05-17 Windows Rerun

This rerun was executed on the current Windows workspace using a PowerShell recreation of `generate_test_data.sh` and the existing `build/bin/Debug` tools.

- **Platform**: Windows 11 Home 10.0.28000
- **CPU**: Snapdragon(R) X2 Elite Extreme - X2E94100 - Qualcomm Oryon(TM) CPU
- **Compression**: BriefLZ
- **Test Image**: `C:\Users\the_m\AppData\Local\Temp\vafs-benchmark-data\benchmark.vafs`
- **Test Image Size**: 60.67 MiB
- **Runs**: 5 full-suite runs, summarized as the median of per-suite averages
- **Windows note**: symlink creation was skipped on this host because symbolic-link creation requires elevation or Developer Mode

| Benchmark | Median avg | Avg range across 5 runs | Median throughput | Throughput range |
| --- | ---: | ---: | ---: | ---: |
| Mount Latency | 0.088 ms | 0.082-0.223 ms | - | - |
| Metadata Traversal | 0.022 ms | 0.021-0.068 ms | - | - |
| Small File Read (4KB) | 0.236 ms | 0.230-0.691 ms | 16.55 MB/s | 5.65-17.01 MB/s |
| Large File Sequential Read | 36.373 ms | 30.072-49.749 ms | 137.46 MB/s | 100.50-166.27 MB/s |
| Repeated Path Lookup | 0.030 ms | 0.015-0.035 ms | - | - |
| Deep Path Stat | 0.001 ms | 0.001-0.001 ms | - | - |
| Wide Directory Stat | 0.005 ms | 0.004-0.006 ms | - | - |

Additional wide-directory spot checks were also run to probe the 512-entry name-index threshold used by the library:

- **256 entries**: 0.002 ms avg for `--only=wide`
- **5000 entries**: 0.004 ms avg for `--only=wide`

### Updated Observations

- Metadata-heavy paths are still effectively free compared to file I/O. Even deep path stats and wide directory stats stayed in the low-microsecond range.
- Large sequential reads remain the dominant runtime cost in absolute terms, so the best runtime optimization target is still the read/decompression pipeline rather than directory lookup.
- Small file read throughput stayed close to the older Linux baseline, but the open/read/close path showed much higher run-to-run variance than the path-stat benchmarks. That suggests the next non-streaming optimization target is the small-file open path rather than directory traversal itself.
- Directory scaling does not look like the limiting factor right now. The 5000-entry wide-directory run only moved from 0.002 ms to 0.004 ms, which suggests the current sorted-index/hash-index and bounded lookup-cache strategy is already doing its job.

### What Could Be Improved

- **Benchmark stability**: the current iteration counts are too low for sub-millisecond workloads. Over five suite runs, mount latency varied from 0.082 ms to 0.223 ms, and small-file throughput ranged from 5.65 MB/s to 17.01 MB/s. The benchmark tool should support higher iteration counts, a warmup phase, and median or percentile reporting.
- **Benchmark corpus realism**: the reproduced image came out to 60.67 MiB, not the `~16MB compressed` noted above. The current generator mostly writes random payloads, which are effectively incompressible. If the goal is representative rootfs behavior, the corpus should be made deterministic and more realistic; otherwise the document should explicitly describe the current dataset as a worst-case compression workload.
- **Large-read path**: sequential reads are still where most absolute time is spent. Improvements here are more likely to matter than additional directory-lookup tuning.
- **Mount path**: mount latency is still small, but it is noticeably higher on this Windows rerun than in the Linux snapshot. If mount/open time becomes important, the first place to look is repeated stream/filter initialization rather than metadata lookup.

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

On Windows, `generate_test_data.sh` is Bash-only. Use WSL/Git Bash or mirror the same directory layout in PowerShell before invoking `build\bin\Debug\mkvafs.exe` and `build\bin\Debug\vafs-bench.exe`.

## Notes

- Results may vary based on system load, CPU frequency scaling, and I/O caching
- For consistent results, disable CPU frequency scaling and clear caches between runs
- The benchmarks use compressed data (BriefLZ) which adds decompression overhead
- Mount latency includes filter installation time
- The current benchmark iteration counts are low enough that repeated suite runs can show noticeable variance on Windows hosts
