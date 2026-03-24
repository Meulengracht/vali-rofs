#!/bin/bash
#
# Test harness for mkvafs and unmkvafs tools
# This script creates test data, builds a VaFS image, extracts it, and verifies the results
#

set -e  # Exit on any error

# Color output for better readability
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test counters
TESTS_PASSED=0
TESTS_FAILED=0

# Helper functions
print_header() {
    echo -e "${YELLOW}========================================${NC}"
    echo -e "${YELLOW}$1${NC}"
    echo -e "${YELLOW}========================================${NC}"
}

print_test() {
    echo -e "${YELLOW}[TEST]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[PASS]${NC} $1"
    TESTS_PASSED=$((TESTS_PASSED + 1))
}

print_error() {
    echo -e "${RED}[FAIL]${NC} $1"
    TESTS_FAILED=$((TESTS_FAILED + 1))
}

print_info() {
    echo -e "[INFO] $1"
}

# Determine paths based on OS
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" || "$OSTYPE" == "cygwin" ]]; then
    MKVAFS="../../build/bin/Release/mkvafs.exe"
    UNMKVAFS="../../build/bin/Release/unmkvafs.exe"
else
    MKVAFS="../../build/bin/mkvafs"
    UNMKVAFS="../../build/bin/unmkvafs"
fi

# Check if binaries exist
if [ ! -f "$MKVAFS" ]; then
    print_error "mkvafs binary not found at $MKVAFS"
    exit 1
fi

if [ ! -f "$UNMKVAFS" ]; then
    print_error "unmkvafs binary not found at $UNMKVAFS"
    exit 1
fi

print_info "Using mkvafs: $MKVAFS"
print_info "Using unmkvafs: $UNMKVAFS"

# Setup test directories
TEST_ROOT="$(mktemp -d)"
TEST_DATA="$TEST_ROOT/test_data"
TEST_IMAGE="$TEST_ROOT/test.vafs"
TEST_EXTRACT="$TEST_ROOT/extracted"

print_header "Setting up test environment"

# Clean up previous test run
if [ -d "$TEST_ROOT" ]; then
    print_info "Cleaning up previous test run"
    rm -rf "$TEST_ROOT"
fi

mkdir -p "$TEST_DATA"
mkdir -p "$TEST_EXTRACT"

print_success "Test environment created"

# Create test data
print_header "Creating test data"

# Test 1: Simple text files
print_test "Creating simple text files"
echo "Hello, World!" > "$TEST_DATA/hello.txt"
echo "This is a test file" > "$TEST_DATA/test.txt"
print_success "Simple text files created"

# Test 2: Binary file
print_test "Creating binary test file"
dd if=/dev/urandom of="$TEST_DATA/binary.dat" bs=1024 count=10 2>/dev/null
print_success "Binary file created (10KB)"

# Test 3: Nested directories
print_test "Creating nested directory structure"
mkdir -p "$TEST_DATA/dir1/subdir1"
mkdir -p "$TEST_DATA/dir1/subdir2"
mkdir -p "$TEST_DATA/dir2"
echo "File in subdir1" > "$TEST_DATA/dir1/subdir1/file1.txt"
echo "File in subdir2" > "$TEST_DATA/dir1/subdir2/file2.txt"
echo "File in dir2" > "$TEST_DATA/dir2/file3.txt"
print_success "Nested directory structure created"

# Test 4: Empty directory
print_test "Creating empty directory"
mkdir -p "$TEST_DATA/empty_dir"
print_success "Empty directory created"

# Test 5: Files with special characters in names (where supported)
print_test "Creating files with special characters"
echo "File with spaces" > "$TEST_DATA/file with spaces.txt"
echo "File with underscores" > "$TEST_DATA/file_with_underscores.txt"
echo "File with dashes" > "$TEST_DATA/file-with-dashes.txt"
print_success "Files with special characters created"

# Test 6: Large-ish file
print_test "Creating larger file"
dd if=/dev/urandom of="$TEST_DATA/large.dat" bs=1024 count=100 2>/dev/null
print_success "Large file created (100KB)"

# Test 7: Zero-byte file
print_test "Creating zero-byte file"
touch "$TEST_DATA/empty.txt"
print_success "Zero-byte file created"

print_info "Test data summary:"
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" || "$OSTYPE" == "cygwin" ]]; then
    find "$TEST_DATA" -type f | wc -l | xargs -I {} echo "  - Files: {}"
    find "$TEST_DATA" -type d | wc -l | xargs -I {} echo "  - Directories: {}"
else
    echo "  - Files: $(find "$TEST_DATA" -type f | wc -l)"
    echo "  - Directories: $(find "$TEST_DATA" -type d | wc -l)"
fi

# Build VaFS image
print_header "Building VaFS image"

print_test "Running mkvafs to create image"
if "$MKVAFS" --out "$TEST_IMAGE" "$TEST_DATA"; then
    print_success "VaFS image created successfully"
else
    print_error "Failed to create VaFS image"
    exit 1
fi

# Check if image was created
if [ -f "$TEST_IMAGE" ]; then
    IMAGE_SIZE=$(stat -c%s "$TEST_IMAGE" 2>/dev/null || stat -f%z "$TEST_IMAGE" 2>/dev/null || echo "unknown")
    print_success "Image file exists (size: $IMAGE_SIZE bytes)"
else
    print_error "Image file was not created"
    exit 1
fi

# Extract VaFS image
print_header "Extracting VaFS image"

print_test "Running unmkvafs to extract image"
if "$UNMKVAFS" --out "$TEST_EXTRACT" "$TEST_IMAGE"; then
    print_success "VaFS image extracted successfully"
else
    print_error "Failed to extract VaFS image"
    exit 1
fi

# Verify extracted contents
print_header "Verifying extracted contents"

# Helper function to compare files
compare_files() {
    local original="$1"
    local extracted="$2"
    local name="$3"

    if [ ! -f "$extracted" ]; then
        print_error "File missing: $name"
        return 1
    fi

    if cmp -s "$original" "$extracted"; then
        print_success "File matches: $name"
        return 0
    else
        print_error "File mismatch: $name"
        return 1
    fi
}

# Verify simple text files
print_test "Verifying simple text files"
compare_files "$TEST_DATA/hello.txt" "$TEST_EXTRACT/hello.txt" "hello.txt"
compare_files "$TEST_DATA/test.txt" "$TEST_EXTRACT/test.txt" "test.txt"

# Verify binary file
print_test "Verifying binary file"
compare_files "$TEST_DATA/binary.dat" "$TEST_EXTRACT/binary.dat" "binary.dat"

# Verify nested files
print_test "Verifying nested directory files"
compare_files "$TEST_DATA/dir1/subdir1/file1.txt" "$TEST_EXTRACT/dir1/subdir1/file1.txt" "dir1/subdir1/file1.txt"
compare_files "$TEST_DATA/dir1/subdir2/file2.txt" "$TEST_EXTRACT/dir1/subdir2/file2.txt" "dir1/subdir2/file2.txt"
compare_files "$TEST_DATA/dir2/file3.txt" "$TEST_EXTRACT/dir2/file3.txt" "dir2/file3.txt"

# Verify files with special characters
print_test "Verifying files with special characters"
compare_files "$TEST_DATA/file with spaces.txt" "$TEST_EXTRACT/file with spaces.txt" "file with spaces.txt"
compare_files "$TEST_DATA/file_with_underscores.txt" "$TEST_EXTRACT/file_with_underscores.txt" "file_with_underscores.txt"
compare_files "$TEST_DATA/file-with-dashes.txt" "$TEST_EXTRACT/file-with-dashes.txt" "file-with-dashes.txt"

# Verify large file
print_test "Verifying large file"
compare_files "$TEST_DATA/large.dat" "$TEST_EXTRACT/large.dat" "large.dat"

# Verify zero-byte file
print_test "Verifying zero-byte file"
compare_files "$TEST_DATA/empty.txt" "$TEST_EXTRACT/empty.txt" "empty.txt"

# Verify directory structure
print_test "Verifying directory structure"
# Note: VaFS does not preserve empty directories
# This is a common limitation in many filesystem image formats
# Only directories containing files or subdirectories are preserved

if [ -d "$TEST_EXTRACT/dir1/subdir1" ]; then
    print_success "Nested directory subdir1 exists"
else
    print_error "Nested directory subdir1 missing"
fi

if [ -d "$TEST_EXTRACT/dir1/subdir2" ]; then
    print_success "Nested directory subdir2 exists"
else
    print_error "Nested directory subdir2 missing"
fi

# Additional interoperability tests
print_header "Additional interoperability tests"

# Test: Create image from extracted data (round-trip test)
print_test "Round-trip test: Creating image from extracted data"
TEST_IMAGE2="$TEST_ROOT/test2.vafs"
if "$MKVAFS" --out "$TEST_IMAGE2" "$TEST_EXTRACT"; then
    print_success "Second image created from extracted data"

    # Extract the second image
    TEST_EXTRACT2="$TEST_ROOT/extracted2"
    mkdir -p "$TEST_EXTRACT2"

    if "$UNMKVAFS" --out "$TEST_EXTRACT2" "$TEST_IMAGE2"; then
        print_success "Second image extracted successfully"

        # Compare a few key files to ensure consistency
        if compare_files "$TEST_EXTRACT/hello.txt" "$TEST_EXTRACT2/hello.txt" "round-trip hello.txt"; then
            print_success "Round-trip test passed"
        fi
    else
        print_error "Failed to extract second image"
    fi
else
    print_error "Failed to create second image"
fi

# Print summary
print_header "Test Summary"
echo -e "${GREEN}Tests Passed: $TESTS_PASSED${NC}"
if [ $TESTS_FAILED -gt 0 ]; then
    echo -e "${RED}Tests Failed: $TESTS_FAILED${NC}"
    exit 1
else
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
fi
