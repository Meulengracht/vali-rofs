# VaFS Test Suite

This directory contains the test harness for the VaFS tools (mkvafs and unmkvafs).

## Test Harness

The `github-ci.sh` script provides comprehensive testing of the mkvafs and unmkvafs tools.

### What it tests

1. **Basic file operations**
   - Simple text files
   - Binary files
   - Zero-byte files
   - Large files

2. **Directory operations**
   - Nested directory structures
   - Empty directories
   - Files with special characters in names

3. **Round-trip verification**
   - Creates a VaFS image from test data
   - Extracts the image
   - Verifies all files match the originals byte-for-byte
   - Creates a second image from the extracted data
   - Extracts and verifies the second image

### Running the tests

From the repository root:

```bash
# Build the project first
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DVAFS_BUILD_TOOLS=ON
cmake --build . --config Release

# Run the tests
cd ../tests/libs
./github-ci.sh
```

### Test Output

The test harness creates a `test_output` directory containing:
- `test_data/` - The original test files
- `test.vafs` - The created VaFS image
- `extracted/` - The extracted files from the image
- `test2.vafs` - A second image created from the extracted data (round-trip test)
- `extracted2/` - The extracted files from the second image

### CI Integration

The test harness is automatically run by GitHub Actions on:
- Ubuntu (latest)
- Windows (latest)

See `.github/workflows/ci.yml` for the CI configuration.
