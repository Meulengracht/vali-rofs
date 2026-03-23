#!/bin/bash
#
# Test harness for malformed VaFS descriptor validation
# This script creates malformed images and verifies the parser rejects them
#

set -e

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

TESTS_PASSED=0
TESTS_FAILED=0

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
    UNMKVAFS="../build/bin/Release/unmkvafs.exe"
    TEST_MALFORMED="../build/bin/Release/test-malformed.exe"
else
    UNMKVAFS="../build/bin/unmkvafs"
    TEST_MALFORMED="../build/bin/test-malformed"
fi

# Check if binaries exist
if [ ! -f "$UNMKVAFS" ]; then
    print_error "unmkvafs binary not found at $UNMKVAFS"
    exit 1
fi

if [ ! -f "$TEST_MALFORMED" ]; then
    print_error "test-malformed binary not found at $TEST_MALFORMED"
    print_info "Build it with: cd build && cmake --build . --target test-malformed"
    exit 1
fi

print_info "Using unmkvafs: $UNMKVAFS"
print_info "Using test-malformed: $TEST_MALFORMED"

# Setup test directories
TEST_ROOT="$(pwd)/test_malformed_output"
TEST_IMAGES="$TEST_ROOT/images"
TEST_EXTRACT="$TEST_ROOT/extracted"

print_header "Setting up test environment"

# Clean up previous test run
if [ -d "$TEST_ROOT" ]; then
    print_info "Cleaning up previous test run"
    rm -rf "$TEST_ROOT"
fi

mkdir -p "$TEST_IMAGES"
mkdir -p "$TEST_EXTRACT"

print_success "Test environment created"

# Create malformed test images
print_header "Creating malformed test images"
"$TEST_MALFORMED" "$TEST_IMAGES"
print_success "Malformed test images created"

# Test each malformed image - they should all fail to parse
print_header "Testing malformed descriptor rejection"

test_should_fail() {
    local test_name="$1"
    local image_path="$2"

    print_test "$test_name"

    # Try to extract - should fail
    if "$UNMKVAFS" -i "$image_path" -o "$TEST_EXTRACT/test_$test_name" 2>/dev/null; then
        print_error "$test_name: Parser accepted malformed descriptor (should have rejected)"
        return 1
    else
        print_success "$test_name: Parser correctly rejected malformed descriptor"
        return 0
    fi
}

# Run all malformed descriptor tests
test_should_fail "descriptor_too_short" "$TEST_IMAGES/test_descriptor_too_short.vafs"
test_should_fail "descriptor_too_long" "$TEST_IMAGES/test_descriptor_too_long.vafs"
test_should_fail "directory_excessive_count" "$TEST_IMAGES/test_directory_excessive_count.vafs"
test_should_fail "symlink_length_mismatch" "$TEST_IMAGES/test_symlink_length_mismatch.vafs"
test_should_fail "symlink_zero_length" "$TEST_IMAGES/test_symlink_zero_length.vafs"
test_should_fail "file_no_name" "$TEST_IMAGES/test_file_no_name.vafs"
test_should_fail "symlink_excessive_length" "$TEST_IMAGES/test_symlink_excessive_length.vafs"

# Print summary
print_header "Test Summary"
echo -e "Total tests: $((TESTS_PASSED + TESTS_FAILED))"
echo -e "${GREEN}Passed: $TESTS_PASSED${NC}"
echo -e "${RED}Failed: $TESTS_FAILED${NC}"

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed!${NC}"
    exit 1
fi
