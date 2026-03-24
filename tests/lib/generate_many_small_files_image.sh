#!/bin/bash
# VaFS Many Small Files Image Generator
# Creates a VaFS image with many small files to stress test file operations

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/../build}"
TEST_DATA_DIR="${TEST_DATA_DIR:-/tmp/vafs-test-many-small}"
OUTPUT_IMAGE="${OUTPUT_IMAGE:-${TEST_DATA_DIR}/many-small-files.vafs}"
FILE_COUNT="${FILE_COUNT:-2000}"

echo "VaFS Many Small Files Image Generator"
echo "======================================"
echo "Build directory: ${BUILD_DIR}"
echo "Test data directory: ${TEST_DATA_DIR}"
echo "Output image: ${OUTPUT_IMAGE}"
echo "File count: ${FILE_COUNT}"
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

echo "Generating ${FILE_COUNT} small files..."

# Create files in multiple subdirectories for better organization
for subdir in {1..10}; do
    mkdir -p "files${subdir}"
done

# Create many small text files (1-5KB each)
for i in $(seq 1 ${FILE_COUNT}); do
    subdir=$((1 + (i % 10)))

    # Vary content type
    case $((i % 5)) in
        0)
            # Plain text
            echo "File number ${i}" > "files${subdir}/file_${i}.txt"
            echo "This is a test file for stress testing" >> "files${subdir}/file_${i}.txt"
            echo "Line 3" >> "files${subdir}/file_${i}.txt"
            ;;
        1)
            # JSON-like
            cat > "files${subdir}/data_${i}.json" << EOF
{
  "id": ${i},
  "name": "file_${i}",
  "type": "test",
  "value": $((RANDOM % 1000))
}
EOF
            ;;
        2)
            # Config-like
            cat > "files${subdir}/config_${i}.conf" << EOF
# Configuration ${i}
key1=value${i}
key2=value$((i * 2))
enabled=true
EOF
            ;;
        3)
            # Small binary data
            dd if=/dev/urandom of="files${subdir}/binary_${i}.bin" bs=1024 count=1 2>/dev/null
            ;;
        4)
            # CSV-like
            cat > "files${subdir}/data_${i}.csv" << EOF
id,value,timestamp
${i},$((RANDOM % 1000)),$(date +%s)
EOF
            ;;
    esac

    # Progress indicator every 200 files
    if [ $((i % 200)) -eq 0 ]; then
        echo "  Created $i files..."
    fi
done

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
else
    echo "Error: Failed to create VaFS image"
    exit 1
fi
