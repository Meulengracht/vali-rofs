# VaFS Metadata Hotspot Report

Date: 2026-03-26  
Image: `/tmp/vafs-benchmark-data/benchmark.vafs` (617 files, 500-file wide dir, deep path at `/lookup_test/subdir1/subdir2/subdir3/target.txt`)  
Platform: Linux 6.14.0-1017-azure, GCC 13.3.0, BriefLZ compression

## Summary Metrics

- Mount latency (5 iters): avg **0.023 ms**
- Metadata traversal `/` (3 iters): avg **0.018 ms**
- Repeated path lookup `/test.txt` (5 iters): avg **0.010 ms**
- Deep path stat `/lookup_test/.../target.txt` (10 iters): avg **0.008 ms**
- Wide directory stat `/wide_dir` (5 iters across 500 entries): avg **0.001 ms**
- Large file sequential read (2 iters, 5 MB): avg **62.6 ms**, **79.8 MB/s**

## Hotspots & Findings

1) **Sequential read dominates overall cost**  
   - At ~62.6 ms per 5 MB iteration, large file streaming is the only material hotspot. CPU time is driven by BriefLZ decompression and repeated stream seeks. Improving streaming (larger block reads, async prefetch, or lighter filter) would move the needle most.

2) **Directory lookups are currently fast but O(n)**  
   - Wide directory stat across 500 entries averaged 0.001 ms, yet lookups are linear scans in `__vafs_path_stat_internal`. For much larger sibling counts this will scale linearly; adding name hashing or caching could prevent future regressions.

3) **`vafs_file_read` does not advance file position**  
   - Sequential readers must explicitly seek after each read (benchmarks now do). Without advancing, callers can loop forever on the same block. Moving the position update into `vafs_file_read` would reduce repeated seeks and prevent misuse.

4) **Deep path traversal shows negligible cost**  
   - Deep path stat (~0.008 ms) indicates tokenization and multi-level descriptor traversal are cheap under current depth. No immediate bottleneck observed.

## Recommendations

- Optimize the streaming path (decompression hotspot) before tuning metadata lookups.
- Consider a directory name index for very wide directories to cap lookup cost.
- Update `vafs_file_read` to advance `handle->Position` internally to avoid caller pitfalls and extra seeks.
