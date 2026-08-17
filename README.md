# VaFS

VaFS is a read-only filesystem format and C library originally built for Vali/MollenOS initrd images. The project packages directory trees into compact `.vafs` images, exposes a small C API for reading and creating them, and ships command-line tools for building, extracting, mounting, and benchmarking images.

## Overview

| Area | What VaFS provides |
| --- | --- |
| Format | A read-only image layout with separate descriptor and data streams |
| Library | A static C library for image creation, traversal, metadata queries, and file reads |
| Storage backends | Open images from files, memory buffers, or custom `VaFsOperations` backends |
| Metadata | Files, directories, symbolic links, stored permissions, and POSIX-like `stat` queries |
| Compression | Stream filters with BriefLZ enabled by default and aplib available optionally |
| Integrity | CRC32 validation on block data |
| Extensibility | GUID-based feature records plus runtime-only filter callback installation |
| Tooling | `mkvafs`, `unmkvafs`, optional `vafs-util` FUSE mounting, and `vafs-bench` |

## Highlights

- Separate descriptor and data streams let metadata stay small and random-access friendly while file payloads use their own block sizing policy.
- The feature system cleanly splits portable on-disk metadata from host-specific runtime behavior, so images stay stable while filter callbacks remain pluggable.
- The same read path can operate on `FILE*`, in-memory images, and custom backends, which makes the library usable in boot flows, tools, and embedded integrations.
- Read-heavy workloads benefit from a block cache, a bounded lookup cache for path components, and stored-block bypass for incompressible data.

## Project Structure

| Component | Purpose |
| --- | --- |
| `libvafs` | Core library for creating and reading VaFS images |
| `mkvafs` | Packs a directory tree into a `.vafs` image |
| `unmkvafs` | Extracts an image back to a host directory |
| `vafs-util` | Optional FUSE mount tool built when FUSE is available on non-Windows hosts |
| `vafs-bench` | Benchmark suite for mount, metadata, lookup, and file-read workloads |

## Performance

The table below summarizes the stable Windows rerun recorded in `benchmarks/BENCHMARK_BASELINE.md`.

- Host: Windows 11 on Snapdragon X Elite
- Filter: BriefLZ
- Corpus: 617 files, 60.75 MiB source data compressed to 6.73 MiB
- Method: median of 3 full-suite runs with `--warmup=50 --iterations=1000`

| Benchmark | Median average | Median throughput |
| --- | ---: | ---: |
| Mount Latency | 0.076 ms | - |
| Metadata Traversal | 0.000 ms | - |
| Small File Read (4KB) | 0.006 ms | 879.09 MB/s |
| Large File Sequential Read | 10.946 ms | 460.73 MB/s |
| Repeated Path Lookup | 0.000 ms | - |
| Deep Path Stat | 0.001 ms | - |
| Wide Directory Stat | 0.001 ms | - |

Several metadata-heavy paths hit the current formatter precision floor in the benchmark output, so `0.000 ms` and `0.001 ms` here should be read as "below current reporting precision," not as literal zero-cost operations.

For the full baseline, methodology notes, and reproduction commands, see [benchmarks/BENCHMARK_BASELINE.md](benchmarks/BENCHMARK_BASELINE.md) and [benchmarks/README.md](benchmarks/README.md).

## Build

VaFS uses CMake and builds the core library, tools, benchmarks, and tests by default.

```bash
cmake -S . -B build
cmake --build build --config Release
```

Useful options:

| Option | Default | Description |
| --- | --- | --- |
| `VAFS_BUILD_TOOLS` | `ON` | Build `mkvafs`, `unmkvafs`, and `vafs-util` when FUSE is available |
| `VAFS_BUILD_BENCHMARKS` | `ON` | Build the benchmark suite |
| `VAFS_BUILD_TESTS` | `ON` | Build unit and fuzz-style test targets |
| `VAFS_BUILD_FILTER_BRIEFLZ` | `ON` | Enable the BriefLZ filter |
| `VAFS_BUILD_FILTER_APLIB` | `OFF` | Enable aplib filter support |

On non-Windows hosts, the FUSE utility is only built when the FUSE development package is found.

## Minimal Example

```c
#include <stdio.h>
#include <vafs/vafs.h>
#include 

int main(void) {
	struct VaFs* fs = NULL;
	struct VaFsFileHandle* file = NULL;
	char buffer[128];
	size_t bytesRead;

	if (vafs_open_file("rootfs.vafs", &fs) != 0) {
		return 1;
	}

	if (vafs_file_open(fs, "/etc/version.txt", &file) != 0) {
		vafs_close(fs);
		return 1;
	}

	bytesRead = vafs_file_read(file, buffer, sizeof(buffer) - 1);
	buffer[bytesRead] = '\0';
	puts(buffer);

	vafs_file_close(file);
	vafs_close(fs);
	return 0;
}
```

For more detail on the API surface and internals, see:

- [docs/LIBRARY_ARCHITECTURE.md](docs/LIBRARY_ARCHITECTURE.md)
- [docs/VAFS_FORMAT_SPEC.md](docs/VAFS_FORMAT_SPEC.md)
- [docs/VAFS_ROOTFS_METADATA_REQUIREMENTS.md](docs/VAFS_ROOTFS_METADATA_REQUIREMENTS.md)
- [benchmarks/README.md](benchmarks/README.md)

## License

VaFS is distributed under the terms described in [LICENSE](LICENSE).
