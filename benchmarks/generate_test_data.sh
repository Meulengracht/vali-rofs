#!/bin/bash
# VaFS Benchmark Test Data Generator
# This script creates test data and VaFS images for benchmarking

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/../build}"
TEST_DATA_DIR="${TEST_DATA_DIR:-/tmp/vafs-benchmark-data}"
OUTPUT_IMAGE="${OUTPUT_IMAGE:-${TEST_DATA_DIR}/benchmark.vafs}"

echo "VaFS Benchmark Test Data Generator"
echo "==================================="
echo "Build directory: ${BUILD_DIR}"
echo "Test data directory: ${TEST_DATA_DIR}"
echo "Output image: ${OUTPUT_IMAGE}"
echo ""

# Check if mkvafs tool exists
MKVAFS="${BUILD_DIR}/bin/mkvafs"
if [ ! -f "${MKVAFS}" ]; then
    echo "Error: mkvafs not found at ${MKVAFS}"
    echo "Please build the project first: cd build && cmake .. && make"
    exit 1
fi

# Create test data directory
mkdir -p "${TEST_DATA_DIR}/source"
cd "${TEST_DATA_DIR}/source"

echo "Generating test data..."

# Create small files for small file read benchmark (1KB - 10KB)
echo "Creating small files..."
mkdir -p small_files
for i in {1..100}; do
    dd if=/dev/urandom of="small_files/file_${i}.txt" bs=1024 count=$((1 + RANDOM % 10)) 2>/dev/null
done

# Create a specific small file for benchmarking
dd if=/dev/urandom of="small.txt" bs=1024 count=4 2>/dev/null

# Create large files for sequential read benchmark (1MB - 10MB)
echo "Creating large files..."
mkdir -p large_files
for i in {1..10}; do
    dd if=/dev/urandom of="large_files/file_${i}.bin" bs=1M count=$i 2>/dev/null
done

# Create a specific large file for benchmarking (5MB)
dd if=/dev/urandom of="large.bin" bs=1M count=5 2>/dev/null

# Create directory structure for metadata traversal benchmark
echo "Creating directory structure..."
mkdir -p deep/path/to/test/nested/directories/for/traversal/benchmark
mkdir -p wide_dir
for i in {1..500}; do
    echo "File $i" > "wide_dir/file_${i}.txt"
done

# Create files for path lookup benchmark
mkdir -p lookup_test/subdir1/subdir2/subdir3
echo "Target file for lookup benchmark" > "lookup_test/subdir1/subdir2/subdir3/target.txt"
echo "Test file for lookup" > "test.txt"

# Create various file types and sizes
echo "Creating mixed content..."
mkdir -p mixed
echo "Hello, VaFS!" > "mixed/config.txt"
dd if=/dev/zero of="mixed/zeros.bin" bs=1K count=100 2>/dev/null
dd if=/dev/urandom of="mixed/random.bin" bs=1K count=100 2>/dev/null

# Create symlinks if supported
if ln -s "test.txt" "symlink_test.txt" 2>/dev/null; then
    echo "Created symlinks"
fi

echo ""
echo "Test data generation complete!"
echo "Source directory size: $(du -sh . | cut -f1)"
echo "File count: $(find . -type f | wc -l)"
echo ""

# Create VaFS image
echo "Creating VaFS image..."
"${MKVAFS}" --out "${OUTPUT_IMAGE}" .

if [ -f "${OUTPUT_IMAGE}" ]; then
    echo ""
    echo "VaFS image created successfully!"
    echo "Image size: $(du -sh "${OUTPUT_IMAGE}" | cut -f1)"
    echo "Image path: ${OUTPUT_IMAGE}"
    echo ""
    echo "You can now run benchmarks with:"
    echo "  ${BUILD_DIR}/bin/vafs-bench ${OUTPUT_IMAGE}"
else
    echo "Error: Failed to create VaFS image"
    exit 1
fi
