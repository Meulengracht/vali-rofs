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

### 2026-05-17 Stable Windows Rerun

This rerun was executed on the current Windows workspace using the native `generate_test_data.ps1` generator and the existing `build/bin/Debug` tools.

- **Platform**: Windows 11 Home 10.0.28000
- **CPU**: Snapdragon(R) X2 Elite Extreme - X2E94100 - Qualcomm Oryon(TM) CPU
- **Compression**: BriefLZ
- **Test Image**: `C:\Users\the_m\AppData\Local\Temp\vafs-benchmark-data\benchmark.vafs`
- **Test Image Size**: 6.73 MiB (7,061,477 bytes)
- **Source Corpus Size**: 60.75 MiB across 617 files
- **Methodology**: 3 full-suite runs, each with `--warmup=50 --iterations=1000`, summarized as the median of per-suite averages
- **Corpus**: deterministic structured text plus record-varying large payloads for reproducible compression behavior
- **Windows note**: symlink creation was skipped on this host because symbolic-link creation requires elevation or Developer Mode

| Benchmark | Median avg | Avg range across 3 runs | Median throughput | Throughput range |
| --- | ---: | ---: | ---: | ---: |
| Mount Latency | 0.076 ms | 0.071-0.076 ms | - | - |
| Metadata Traversal | 0.000 ms | 0.000-0.000 ms | - | - |
| Small File Read (4KB) | 0.006 ms | 0.004-0.006 ms | 879.09 MB/s | 636.02-879.09 MB/s |
| Large File Sequential Read | 10.946 ms | 10.852-10.946 ms | 460.73 MB/s | 456.78-460.73 MB/s |
| Repeated Path Lookup | 0.000 ms | 0.000-0.000 ms | - | - |
| Deep Path Stat | 0.001 ms | 0.001-0.001 ms | - | - |
| Wide Directory Stat | 0.001 ms | 0.001-0.001 ms | - | - |

Additional wide-directory spot checks were rerun with the same stable settings to probe the 512-entry name-index threshold used by the library:

- **256 entries**: 0.001 ms avg with `--warmup=50 --iterations=1000`
- **5000 entries**: 0.001 ms avg with `--warmup=50 --iterations=1000`

### Updated Observations

- The new warmup and higher iteration counts materially reduced noise on the expensive path. Large sequential read throughput stayed in a tight `456.78-460.73 MB/s` band across the three stable reruns.
- The deterministic corpus changed the shape of the benchmark in an expected way: the compressed image dropped to `6.73 MiB`, so large sequential reads are much faster than they were on the earlier mostly-random corpus. Cross-run comparisons only make sense when the corpus profile is held constant.
- Small-file and metadata-heavy paths are now so cheap that several of them hit the formatter's `0.001 ms` precision floor. They are still non-zero operations, but the current output precision no longer distinguishes the smallest steady-state differences.
- Directory scaling still does not look like the limiting factor. The 256-entry and 5000-entry wide-directory spot checks both stayed at `0.001 ms` average with the stable settings, which suggests the current sorted-index/hash-index and bounded lookup-cache strategy is already doing its job.
- Large sequential reads remain the dominant absolute cost even on the more compressible deterministic corpus, so the highest-value runtime optimization target is still the read/decompression pipeline rather than directory lookup.

### What Could Be Improved

- **Output precision**: sub-microsecond metadata benchmarks round to `0.000 ms` or `0.001 ms` in the current formatter. For the fast-path benchmarks, the next useful improvement is reporting in microseconds or adding a higher-precision machine-readable field.
- **Per-benchmark tuning**: the new global `--warmup` and `--iterations` flags are enough for stable whole-suite reruns, but the fastest metadata paths and the large sequential read benchmark still want different ideal iteration counts. Per-benchmark overrides or named profiles would make the tool easier to tune.
- **Corpus profiles**: the deterministic corpus is reproducible and much more representative than pure random data, but benchmark outcomes still depend strongly on compressibility. It would be useful to ship named corpus profiles such as `mixed`, `compressible`, and `worst-case` so regressions can be checked under multiple workloads.
- **Large-read path**: sequential reads are still where most absolute time is spent. Improvements here are more likely to matter than additional directory-lookup tuning.

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

For a steadier local baseline, rerun with warmup and higher iteration counts:

```bash
../build/bin/vafs-bench --warmup=50 --iterations=1000 /tmp/vafs-benchmark-data/benchmark.vafs
```

On Windows, use the native PowerShell generator and the Debug binaries directly:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\generate_test_data.ps1
..\build\bin\Debug\vafs-bench.exe --warmup=50 --iterations=1000 $env:TEMP\vafs-benchmark-data\benchmark.vafs
```

## Notes

- Results may vary based on system load, CPU frequency scaling, and I/O caching
- For consistent results, disable CPU frequency scaling and clear caches between runs
- The benchmarks use compressed data (BriefLZ) which adds decompression overhead
- Mount latency includes filter installation time
- The source defaults in `vafs_bench.c` are still tuned for quick smoke runs; use `--warmup` and `--iterations` for stable local baselines
