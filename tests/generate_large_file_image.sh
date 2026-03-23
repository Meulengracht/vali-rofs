#!/bin/bash
# VaFS Large File Image Generator
# Creates a VaFS image with one or more large files for performance testing

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/../build}"
TEST_DATA_DIR="${TEST_DATA_DIR:-/tmp/vafs-test-large-file}"
OUTPUT_IMAGE="${OUTPUT_IMAGE:-${TEST_DATA_DIR}/large-file.vafs}"
LARGE_FILE_SIZE="${LARGE_FILE_SIZE:-100}"  # Size in MB

echo "VaFS Large File Image Generator"
echo "================================"
echo "Build directory: ${BUILD_DIR}"
echo "Test data directory: ${TEST_DATA_DIR}"
echo "Output image: ${OUTPUT_IMAGE}"
echo "Large file size: ${LARGE_FILE_SIZE}MB"
echo ""

# Check if mkvafs tool exists
MKVAFS="${BUILD_DIR}/bin/mkvafs"
if [ ! -f "${MKVAFS}" ]; then
    echo "Error: mkvafs not found at ${MKVAFS}"
    echo "Please build the project first: cd build && cmake .. && make"
    exit 1
fi

# Create test data directory
rm -rf "${TEST_DATA_DIR}"
mkdir -p "${TEST_DATA_DIR}/source"
cd "${TEST_DATA_DIR}/source"

echo "Generating large file test data..."

# Create a very large file with random data
echo "Creating large random file (${LARGE_FILE_SIZE}MB)..."
dd if=/dev/urandom of="large_random.bin" bs=1M count=${LARGE_FILE_SIZE} 2>/dev/null

# Create a large file with zeros (tests compression)
echo "Creating large zero-filled file (${LARGE_FILE_SIZE}MB)..."
dd if=/dev/zero of="large_zeros.bin" bs=1M count=${LARGE_FILE_SIZE} 2>/dev/null

# Create a large file with pattern (semi-compressible)
echo "Creating large pattern file ($((LARGE_FILE_SIZE / 2))MB)..."
{
    for i in $(seq 1 $((LARGE_FILE_SIZE * 256))); do
        printf "PATTERN_%08d_ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789_" ${i}
    done
} > "large_pattern.bin"

# Create several medium-large files (10-20MB each)
echo "Creating medium-large files..."
mkdir -p medium_files
for i in {1..5}; do
    dd if=/dev/urandom of="medium_files/file${i}.bin" bs=1M count=$((10 + i * 2)) 2>/dev/null
done

# Create a large text file (log-like)
echo "Creating large text file..."
{
    for i in $(seq 1 1000000); do
        echo "$(date '+%Y-%m-%d %H:%M:%S') [INFO] Log entry number ${i} - Lorem ipsum dolor sit amet"
    done
} > "large_log.txt"

# Create a large CSV file
echo "Creating large CSV file..."
{
    echo "id,timestamp,value,data,status"
    for i in $(seq 1 500000); do
        echo "${i},$(date +%s),$((RANDOM % 10000)),data_${i},active"
    done
} > "large_data.csv"

# Create a large JSON file
echo "Creating large JSON file..."
{
    echo "["
    for i in $(seq 1 100000); do
        if [ ${i} -eq 100000 ]; then
            echo "  {\"id\": ${i}, \"value\": $((RANDOM % 1000)), \"name\": \"entry_${i}\"}"
        else
            echo "  {\"id\": ${i}, \"value\": $((RANDOM % 1000)), \"name\": \"entry_${i}\"},"
        fi
    done
    echo "]"
} > "large_data.json"

# Create some small files for contrast
echo "Creating small reference files..."
echo "This is a small file for comparison" > small_file.txt
echo "Another small file" > small_file2.txt

# Create a README
cat > README.md << EOF
# Large File Test Image

This VaFS image contains large files for performance testing.

Files included:
- large_random.bin: ${LARGE_FILE_SIZE}MB of random data
- large_zeros.bin: ${LARGE_FILE_SIZE}MB of zeros (highly compressible)
- large_pattern.bin: Pattern data (semi-compressible)
- large_log.txt: Large text log file
- large_data.csv: Large CSV dataset
- large_data.json: Large JSON dataset
- medium_files/: Several 10-20MB files
- Small files for comparison

Purpose: Test sequential read performance, memory efficiency, and compression.
EOF

echo ""
echo "Test data generation complete!"
echo "Source directory size: $(du -sh . | cut -f1)"
echo "File count: $(find . -type f | wc -l)"
echo ""

# Create VaFS image
echo "Creating VaFS image (this may take a while)..."
"${MKVAFS}" --out "${OUTPUT_IMAGE}" .

if [ -f "${OUTPUT_IMAGE}" ]; then
    echo ""
    echo "VaFS image created successfully!"
    echo "Image size: $(du -sh "${OUTPUT_IMAGE}" | cut -f1)"
    echo "Image path: ${OUTPUT_IMAGE}"
    echo ""
    echo "Note: The image size may be smaller than source due to compression."
else
    echo "Error: Failed to create VaFS image"
    exit 1
fi
