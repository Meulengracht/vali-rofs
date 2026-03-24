# VaFS Malformed Image Regression Test Suite

This directory contains a comprehensive regression test suite for malformed VaFS images. The purpose of this suite is to prevent future safety regressions by ensuring that all known malformed image cases are detected and rejected safely.

## Overview

The regression test suite includes:

- **Truncated image cases** (5 tests): Tests for files that are cut off at various points
- **Invalid offset cases** (5 tests): Tests for images with offsets that are out of bounds or cause overflows
- **Invalid descriptor length cases** (4 tests): Tests for descriptors with incorrect length fields
- **Invalid feature record cases** (3 tests): Tests for headers with invalid feature counts
- **Symlink loop cases** (3 tests): Tests for symlinks that would cause infinite loops
- **Directory entry cases** (2 tests): Tests for directories with excessive entry counts
- **Invalid header cases** (3 tests): Tests for headers with invalid magic, version, or root descriptor

**Total: 25 comprehensive regression tests**

## Test Programs

### test_regression.c

The main comprehensive regression test suite that generates and validates all 25 malformed image test cases. This program:

1. Generates each malformed image in the specified output directory
2. Attempts to open each image with `vafs_open_file()`
3. Verifies that the malformed image is correctly rejected
4. Reports success or failure for each test case

**Usage:**
```bash
./test-regression <output_directory>
```

**Example:**
```bash
mkdir -p /tmp/test_images
./test-regression /tmp/test_images
```

### test_header_validation.c

Tests specifically for header validation edge cases, including:
- Invalid magic numbers
- Invalid versions
- Feature count limits
- Reserved field validation
- Offset validation
- Root descriptor validation
- Truncated headers

### test_malformed.c

Tests for malformed descriptors, including:
- Descriptors with invalid lengths
- Directory entry count limits
- Symlink validation
- File descriptors without names

### test_symlinks.c

Tests for symlink security, including:
- Cyclic symlink detection
- Deep symlink chain limits
- Malformed symlink targets
- Empty and overly long symlinks

### test_common.h

Common definitions and helper functions shared across all test programs, including:
- VaFS format structure definitions
- Helper functions for writing valid headers and streams
- Constants for block sizes and limits

## Running the Tests

### Run All Regression Tests

Use the unified test runner script:

```bash
cd tests/unit
bash run_regression_tests.sh
```

This script will:
1. Run the comprehensive regression suite (25 tests)
2. Run header validation tests (11 tests)
3. Run malformed descriptor tests (7 tests)
4. Run symlink security tests (6 tests)

### Run Individual Test Programs

```bash
# Run comprehensive regression suite
./test-regression /tmp/test_images

# Run header validation tests
./test-header-validation /tmp/test_images

# Run malformed descriptor tests
./test-malformed /tmp/test_images

# Run symlink security tests
./test-symlinks
```

## Building the Tests

The tests are automatically built when `VAFS_BUILD_TESTS` is enabled (default):

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

The test executables will be in `build/bin/`:
- `test-regression`
- `test-header-validation`
- `test-malformed`
- `test-symlinks`

## Continuous Integration

The regression tests are automatically run in CI for both Ubuntu and Windows:

1. The build system compiles all test programs
2. The unified test runner executes all test suites
3. Test failures cause the CI build to fail

See `.github/workflows/ci.yml` for CI configuration.

## Adding New Test Cases

The regression test suite is designed to be easy to extend. To add a new test case:

### 1. Create a Test Generator Function

Add a new function to `test_regression.c`:

```c
static int test_your_new_case(const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) return -1;

    // Create your malformed image
    test_write_vafs_header(fp);
    // ... add malformed data ...

    fclose(fp);
    return 0;
}
```

### 2. Register the Test Case

Add your test to the `g_test_cases` array:

```c
static TestCase g_test_cases[] = {
    // ... existing tests ...
    {"your_test_name", "Description of what it tests", test_your_new_case},
};
```

### 3. Rebuild and Test

```bash
cd build
cmake --build . --target test-regression --config Release
./bin/test-regression /tmp/test_images
```

The new test will automatically be included in the total count and validation.

## Test Case Categories

### Truncated Cases
These tests ensure the parser handles incomplete files gracefully:
- Truncated mid-header
- Truncated after header
- Truncated in descriptor stream
- Truncated mid-descriptor

### Invalid Offset Cases
These tests ensure offset validation prevents out-of-bounds access:
- Offset before header end
- Data offset before descriptor offset
- Offsets causing arithmetic overflow
- Root descriptor offset beyond bounds

### Invalid Length Cases
These tests ensure descriptor length validation works correctly:
- Length smaller than base descriptor size
- Zero length
- Length exceeding block size
- Mismatched symlink length fields

### Feature Record Cases
These tests ensure feature validation is correct:
- Feature count exceeding maximum
- Maximum uint16_t feature count
- Non-zero reserved fields

### Symlink Cases
These tests ensure symlink validation prevents loops and abuse:
- Self-referencing symlinks
- Zero-length names/targets
- Excessively long names/targets

### Directory Cases
These tests ensure directory limits are enforced:
- Excessive entry counts (uint32_t max)
- Entry counts exceeding reasonable limits

### Header Cases
These tests ensure basic header validation:
- Invalid magic numbers
- Unsupported versions
- Invalid root descriptor indices

## Best Practices

1. **Always test rejection**: Each test should verify that the malformed image is correctly rejected, not just that it's created
2. **Use descriptive names**: Test case names should clearly describe what they're testing
3. **Add comments**: Explain what makes the image malformed
4. **Test boundaries**: Include both slightly invalid and extremely invalid cases
5. **Consider arithmetic**: Test for overflow conditions and off-by-one errors

## Expected Behavior

All malformed images should be rejected by the parser with appropriate error messages. The tests verify that:

1. `vafs_open_file()` returns a non-zero error code
2. No crashes or undefined behavior occurs
3. Error handling is safe and predictable
4. Resources are properly cleaned up

## Troubleshooting

### Test Failures

If a test fails (image is accepted when it should be rejected):
1. Check the implementation in `libvafs/` to see if validation is missing
2. Add appropriate validation to reject the malformed case
3. Re-run the test to verify the fix

### Build Errors

If tests fail to build:
1. Ensure you're using a recent C compiler
2. Check that `vafs` library is built first
3. Verify include paths in `CMakeLists.txt`

### Runtime Errors

If tests crash or hang:
1. Run with a debugger to identify the issue
2. Check for buffer overflows or null pointer dereferences
3. Ensure the test generator creates valid malformed images

## Related Documentation

- [VaFS Format Specification](../../VAFS_FORMAT_SPEC.md)
- [Library Architecture](../../LIBRARY_ARCHITECTURE.md)
- [Main Test Harness](../lib/github-ci.sh)
- [Fuzzing Suite](../fuzz/README.md)
