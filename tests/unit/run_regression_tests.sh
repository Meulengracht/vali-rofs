#!/bin/bash
#
# Unified regression test runner for VaFS malformed images
# This script runs all negative tests to ensure malformed images are rejected
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

# Determine paths based on OS and try multiple locations
# Script can be run from tests/unit/ or repository root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" || "$OSTYPE" == "cygwin" ]]; then
    BUILD_BIN="$REPO_ROOT/build/bin/Release"
    EXE_EXT=".exe"
else
    BUILD_BIN="$REPO_ROOT/build/bin"
    EXE_EXT=""
fi

TEST_REGRESSION="$BUILD_BIN/test-regression$EXE_EXT"
TEST_HEADER_VALIDATION="$BUILD_BIN/test-header-validation$EXE_EXT"
TEST_MALFORMED="$BUILD_BIN/test-malformed$EXE_EXT"
TEST_SYMLINKS="$BUILD_BIN/test-symlinks$EXE_EXT"
UNMKVAFS="$BUILD_BIN/unmkvafs$EXE_EXT"

# Check if binaries exist
MISSING=0

print_info "Repository root: $REPO_ROOT"
print_info "Build binary directory: $BUILD_BIN"
print_info "Looking for test binaries..."

if [ ! -f "$TEST_REGRESSION" ]; then
    print_error "test-regression binary not found at $TEST_REGRESSION"
    MISSING=1
fi

if [ ! -f "$TEST_HEADER_VALIDATION" ]; then
    print_error "test-header-validation binary not found at $TEST_HEADER_VALIDATION"
    MISSING=1
fi

if [ ! -f "$TEST_MALFORMED" ]; then
    print_error "test-malformed binary not found at $TEST_MALFORMED"
    MISSING=1
fi

if [ ! -f "$TEST_SYMLINKS" ]; then
    print_error "test-symlinks binary not found at $TEST_SYMLINKS"
    MISSING=1
fi

if [ ! -f "$UNMKVAFS" ]; then
    print_error "unmkvafs binary not found at $UNMKVAFS"
    MISSING=1
fi

if [ $MISSING -eq 1 ]; then
    print_error "Missing binaries. Build them with: cd build && cmake --build . --config Release"
    exit 1
fi

print_info "Using test-regression: $TEST_REGRESSION"
print_info "Using test-header-validation: $TEST_HEADER_VALIDATION"
print_info "Using test-malformed: $TEST_MALFORMED"
print_info "Using test-symlinks: $TEST_SYMLINKS"
print_info "Using unmkvafs: $UNMKVAFS"

# Setup test directories
TEST_ROOT="$(mktemp -d)"
TEST_IMAGES="$TEST_ROOT/images"

print_header "VaFS Malformed Image Regression Test Suite"

mkdir -p "$TEST_IMAGES"

print_success "Test environment created"

# Test 1: Run comprehensive regression suite
print_header "Running comprehensive regression suite"
print_test "Generating and testing 25 malformed image cases"
if "$TEST_REGRESSION" "$TEST_IMAGES" > "$TEST_ROOT/regression.log" 2>&1; then
    print_success "All 25 regression tests passed"
else
    print_error "Regression suite failed"
    cat "$TEST_ROOT/regression.log"
fi

# Test 2: Run header validation tests
print_header "Running header validation tests"
print_test "Testing malformed header cases"
if "$TEST_HEADER_VALIDATION" "$TEST_IMAGES" > "$TEST_ROOT/header.log" 2>&1; then
    print_success "All header validation tests passed"
else
    print_error "Header validation tests failed"
    cat "$TEST_ROOT/header.log"
fi

# Test 3: Run malformed descriptor tests
print_header "Running malformed descriptor tests"
print_test "Generating malformed descriptor test images"
if "$TEST_MALFORMED" "$TEST_IMAGES" > "$TEST_ROOT/malformed.log" 2>&1; then
    print_success "All malformed descriptor tests passed"
else
    # test-malformed doesn't have the test harness, so we need to run it differently
    # Just verify it creates the images
    if [ -f "$TEST_IMAGES/test_descriptor_too_short.vafs" ]; then
        print_success "Malformed descriptor images created"

        # Test each image manually
        test_should_fail() {
            local test_name="$1"
            local image_path="$2"

            if [ ! -f "$image_path" ]; then
                print_error "$test_name: Image not created"
                return 1
            fi

            if "$UNMKVAFS" -i "$image_path" -o "$TEST_ROOT/extract_$test_name" 2>/dev/null; then
                print_error "$test_name: Parser accepted malformed image (should have rejected)"
                return 1
            else
                print_success "$test_name: Parser correctly rejected malformed image"
                return 0
            fi
        }

        test_should_fail "descriptor_too_short" "$TEST_IMAGES/test_descriptor_too_short.vafs"
        test_should_fail "descriptor_too_long" "$TEST_IMAGES/test_descriptor_too_long.vafs"
        test_should_fail "directory_excessive_count" "$TEST_IMAGES/test_directory_excessive_count.vafs"
        test_should_fail "symlink_length_mismatch" "$TEST_IMAGES/test_symlink_length_mismatch.vafs"
        test_should_fail "symlink_zero_length" "$TEST_IMAGES/test_symlink_zero_length.vafs"
        test_should_fail "file_no_name" "$TEST_IMAGES/test_file_no_name.vafs"
        test_should_fail "symlink_excessive_length" "$TEST_IMAGES/test_symlink_excessive_length.vafs"
    else
        print_error "Malformed descriptor tests failed"
        cat "$TEST_ROOT/malformed.log"
    fi
fi

# Test 4: Run symlink security tests
print_header "Running symlink security tests"
print_test "Testing symlink loop detection and depth limits"
if "$TEST_SYMLINKS" > "$TEST_ROOT/symlinks.log" 2>&1; then
    print_success "All symlink security tests passed"
else
    print_error "Symlink security tests failed"
    cat "$TEST_ROOT/symlinks.log"
fi

# Cleanup
print_header "Cleaning up"
rm -rf "$TEST_ROOT"
print_success "Test environment cleaned up"

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
