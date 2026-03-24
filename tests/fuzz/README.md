# VaFS Fuzzing Harnesses

This directory contains fuzzing harnesses for testing the security and robustness of the VaFS (Vali Filesystem) image parser and traversal operations.

## Overview

Fuzzing is a security testing technique that feeds malformed, unexpected, or random data to a program to discover bugs, crashes, memory corruption, and other security vulnerabilities. These harnesses use LibFuzzer, a coverage-guided fuzzing engine.

## Fuzzing Targets

### 1. `fuzz_image_open` - Image Parsing and Header Validation

**Purpose**: Fuzzes the image opening and header validation logic.

**Target Functions**:
- `vafs_open_memory()` - Opens a VaFS image from a memory buffer
- `__verify_header()` - Validates header fields (magic, version, offsets, etc.)
- `__load_features()` - Loads and parses feature headers
- `__initialize_imagestream()` - Initializes the stream device

**What It Tests**:
- Magic number validation
- Version checks
- Feature count bounds (max 16)
- Reserved field validation
- Descriptor/data block offset validation
- Root descriptor position validation
- Integer overflow in offset calculations
- Stream initialization

**Example Run**:
```bash
./fuzz_image_open corpus/ -max_total_time=60
```

### 2. `fuzz_directory_traversal` - Directory Parsing and Traversal

**Purpose**: Fuzzes directory entry parsing and traversal operations.

**Target Functions**:
- `vafs_directory_open()` - Opens a directory by path
- `vafs_directory_read()` - Reads directory entries
- `__load_directory()` - Loads directory from descriptor stream
- `__read_descriptor()` - Reads file/directory/symlink descriptors
- `__validate_file_descriptor()` - Validates file descriptors
- `__validate_directory_descriptor()` - Validates directory descriptors
- `__validate_symlink_descriptor()` - Validates symlink descriptors

**What It Tests**:
- Directory entry count limits (max 1,000,000)
- Descriptor length validation
- Name length bounds (max 255)
- Descriptor type validation
- Extended data allocation
- Nested directory traversal
- Descriptor consistency checks

**Example Run**:
```bash
./fuzz_directory_traversal corpus/ -max_total_time=60
```

### 3. `fuzz_file_operations` - File Open and Read Operations

**Purpose**: Fuzzes file opening and reading operations.

**Target Functions**:
- `vafs_file_open()` - Opens a file by path
- `vafs_file_read()` - Reads data from a file
- `vafs_file_seek()` - Seeks to a position in a file
- `__vafs_file_open_internal()` - Internal file opening with path resolution

**What It Tests**:
- Path resolution and traversal
- File descriptor validation
- Read bounds checking
- Seek position validation
- Integer overflow in offset calculations
- File position tracking
- Data block access

**Example Run**:
```bash
./fuzz_file_operations corpus/ -max_total_time=60
```

### 4. `fuzz_symlink_resolution` - Symlink Resolution and Path Canonicalization

**Purpose**: Fuzzes symlink resolution and path handling.

**Target Functions**:
- `vafs_symlink_open()` - Opens a symlink by path
- `vafs_symlink_target()` - Reads symlink target path
- `__vafs_resolve_symlink()` - Resolves and canonicalizes symlink paths
- Path resolution in `vafs_file_open()` and `vafs_directory_open()`

**What It Tests**:
- Circular symlink detection (max depth: 40)
- Relative path resolution (../ and ./)
- Absolute path handling
- Target path validation (max 4096 bytes)
- Name length validation (max 255 bytes)
- Buffer overflow in path concatenation
- Empty target handling
- Symlink descriptor validation

**Example Run**:
```bash
./fuzz_symlink_resolution corpus/ -max_total_time=60
```

## Building the Fuzzing Harnesses

### Requirements

- **Clang compiler** (required for LibFuzzer)
- **CMake** 3.14.3 or later
- **libvafs** and its dependencies

### Build Instructions

1. **Configure with fuzzing enabled**:
   ```bash
   mkdir build-fuzz
   cd build-fuzz
   CC=clang CXX=clang++ cmake -DVAFS_BUILD_FUZZ=ON ..
   ```

2. **Build the fuzzing targets**:
   ```bash
   cmake --build . --target fuzz_image_open
   cmake --build . --target fuzz_directory_traversal
   cmake --build . --target fuzz_file_operations
   cmake --build . --target fuzz_symlink_resolution
   ```

3. **Or build all fuzzing targets at once**:
   ```bash
   cmake --build .
   ```

The fuzzing binaries will be located in `build-fuzz/bin/` or `build-fuzz/tests/fuzz/`.

## Corpus Seeds

Corpus seeds are initial test inputs that help the fuzzer explore the input space more effectively. This directory includes a script to generate malformed VaFS images as corpus seeds.

### Generating Corpus Seeds

```bash
cd tests/fuzz
./generate_corpus.sh
```

This creates several malformed VaFS image files in `tests/fuzz/corpus/`:
- `seed_minimal.vafs` - Minimal valid empty VaFS image
- `seed_bad_magic.vafs` - Invalid magic number
- `seed_bad_version.vafs` - Invalid version
- `seed_excessive_features.vafs` - Feature count exceeds maximum
- `seed_invalid_root_index.vafs` - Invalid root descriptor index
- `seed_truncated.vafs` - Truncated header
- `seed_offset_collision.vafs` - Overlapping descriptor/data offsets
- `seed_excessive_dir_count.vafs` - Excessive directory entry count

These seeds cover edge cases from the existing unit tests (`test_header_validation.c` and `test_malformed.c`).

## Running Fuzzing Locally

### Basic Usage

Run a fuzzer for 60 seconds:
```bash
./fuzz_image_open corpus/ -max_total_time=60
```

### Common Options

- `-max_total_time=SECONDS` - Run for a maximum number of seconds
- `-max_len=BYTES` - Maximum length of test input (default: unlimited)
- `-timeout=SECONDS` - Timeout per test case (default: 1200)
- `-rss_limit_mb=MB` - Memory limit in megabytes (default: 2048)
- `-jobs=N` - Number of parallel fuzzing jobs
- `-workers=N` - Number of worker processes
- `-dict=FILE` - Use a dictionary file for mutation hints
- `-print_final_stats=1` - Print statistics when fuzzing ends
- `-detect_leaks=0` - Disable leak detection (if needed)

### Recommended Configuration

For comprehensive local testing:
```bash
# Fuzz image opening for 5 minutes
./fuzz_image_open corpus/ -max_total_time=300 -print_final_stats=1

# Fuzz directory traversal for 5 minutes
./fuzz_directory_traversal corpus/ -max_total_time=300 -print_final_stats=1

# Fuzz file operations for 5 minutes
./fuzz_file_operations corpus/ -max_total_time=300 -print_final_stats=1

# Fuzz symlink resolution for 5 minutes
./fuzz_symlink_resolution corpus/ -max_total_time=300 -print_final_stats=1
```

### Continuous Fuzzing

For continuous fuzzing (until manually stopped):
```bash
./fuzz_image_open corpus/ -workers=4 -jobs=4
```

Press `Ctrl+C` to stop.

## Understanding Fuzzing Output

### Normal Output

```
INFO: Seed: 1234567890
INFO: Loaded 1 modules (12345 inline 8-bit counters): 12345 [0x..., 0x...)
INFO: -max_len is not provided; libFuzzer will not generate inputs larger than 4096 bytes
INFO: A corpus is not provided, starting from an empty corpus
#2      INITED cov: 123 ft: 234 corp: 1/1b exec/s: 0 rss: 30Mb
#8      NEW    cov: 124 ft: 235 corp: 2/5b lim: 4 exec/s: 0 rss: 30Mb
```

- `cov` - Code coverage (edges covered)
- `ft` - Features (unique code paths)
- `corp` - Corpus size (number of interesting inputs)
- `exec/s` - Executions per second
- `rss` - Resident set size (memory usage)

### Crash Detection

If a crash is found, LibFuzzer will output:
```
==12345==ERROR: AddressSanitizer: heap-buffer-overflow
... stack trace ...
artifact_prefix='./'; Test unit written to ./crash-<hash>
```

The crash input is saved to a file for reproduction and analysis.

### Reproducing Crashes

To reproduce a crash:
```bash
./fuzz_image_open crash-<hash>
```

## Integration with CI/CD

### GitHub Actions Example

```yaml
- name: Run Fuzzing Tests
  run: |
    mkdir build-fuzz
    cd build-fuzz
    CC=clang CXX=clang++ cmake -DVAFS_BUILD_FUZZ=ON ..
    cmake --build .

    # Generate corpus
    cd ../tests/fuzz
    ./generate_corpus.sh
    cd ../../build-fuzz

    # Run each fuzzer for 60 seconds
    ./tests/fuzz/fuzz_image_open ../tests/fuzz/corpus/ -max_total_time=60
    ./tests/fuzz/fuzz_directory_traversal ../tests/fuzz/corpus/ -max_total_time=60
    ./tests/fuzz/fuzz_file_operations ../tests/fuzz/corpus/ -max_total_time=60
    ./tests/fuzz/fuzz_symlink_resolution ../tests/fuzz/corpus/ -max_total_time=60
```

## Security Boundaries Tested

The fuzzing harnesses specifically target these security-critical areas:

1. **Integer Overflow Protection**
   - Offset calculations in file reads
   - Path length computations
   - Descriptor size calculations

2. **Buffer Overflow Prevention**
   - Path concatenation in symlink resolution
   - Name copying from descriptors
   - Data reading operations

3. **Bounds Validation**
   - Feature count (max 16)
   - Directory entry count (max 1,000,000)
   - Name length (max 255)
   - Path length (max 4096)
   - Block offsets and indices

4. **Circular Reference Detection**
   - Symlink depth limit (max 40)
   - Prevention of infinite loops

5. **Consistency Checks**
   - Descriptor length vs. actual data
   - Symlink name/target length consistency
   - Block position validation

## Troubleshooting

### Issue: "Fuzzer not found"

**Solution**: Ensure you're using Clang and LibFuzzer is available:
```bash
clang --version  # Should show Clang version
```

### Issue: "Sanitizer errors during normal operation"

**Solution**: Some sanitizer findings may be false positives. Review the stack trace to determine if it's a real bug.

### Issue: "Out of memory"

**Solution**: Reduce the RSS limit or max input length:
```bash
./fuzz_image_open corpus/ -rss_limit_mb=512 -max_len=65536
```

### Issue: "No interesting inputs found"

**Solution**: Ensure corpus seeds are generated:
```bash
cd tests/fuzz
./generate_corpus.sh
ls corpus/  # Should show .vafs files
```

## References

- [LibFuzzer Documentation](https://llvm.org/docs/LibFuzzer.html)
- [AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html)
- [UndefinedBehaviorSanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)
- VaFS Format Specification: `VAFS_FORMAT_SPEC.md`
- VaFS Library Architecture: `LIBRARY_ARCHITECTURE.md`
- Unit Tests: `tests/unit/test_header_validation.c`, `tests/unit/test_malformed.c`

## Contributing

When adding new fuzzing harnesses:

1. Target a specific parsing or traversal function
2. Exercise all code paths in the target
3. Add relevant corpus seeds
4. Document the target areas and what is tested
5. Update this README with the new harness

## License

These fuzzing harnesses are part of the VaFS project and licensed under the same terms as the main project (GNU GPL v3).
