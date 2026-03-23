#!/bin/bash
# VaFS Wide Directory Image Generator
# Creates a VaFS image with a single directory containing many files

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/../build}"
TEST_DATA_DIR="${TEST_DATA_DIR:-/tmp/vafs-test-wide-dir}"
OUTPUT_IMAGE="${OUTPUT_IMAGE:-${TEST_DATA_DIR}/wide-directory.vafs}"
FILE_COUNT="${FILE_COUNT:-1000}"

echo "VaFS Wide Directory Image Generator"
echo "===================================="
echo "Build directory: ${BUILD_DIR}"
echo "Test data directory: ${TEST_DATA_DIR}"
echo "Output image: ${OUTPUT_IMAGE}"
echo "Files per directory: ${FILE_COUNT}"
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

echo "Generating wide directory with ${FILE_COUNT} files..."

# Create a single wide directory
mkdir -p wide_dir

# Generate many files in the same directory
for i in $(seq 1 ${FILE_COUNT}); do
    # Use various file types and patterns
    case $((i % 4)) in
        0)
            # Numbered files with prefix
            echo "Content of file ${i}" > "wide_dir/file_${i}.txt"
            ;;
        1)
            # UUID-like names
            printf "Data-%04x-%04x\n" $((RANDOM)) $((RANDOM)) > "wide_dir/$(printf '%08x-%04x' ${i} $((RANDOM))).dat"
            ;;
        2)
            # Date-based names
            echo "Entry ${i}" > "wide_dir/entry_$(date +%Y%m%d)_${i}.log"
            ;;
        3)
            # Short binary files
            dd if=/dev/urandom of="wide_dir/data_${i}.bin" bs=512 count=1 2>/dev/null
            ;;
    esac

    # Progress indicator every 100 files
    if [ $((i % 100)) -eq 0 ]; then
        echo "  Created $i files..."
    fi
done

# Add some files with special characters in names (if supported)
echo "test" > "wide_dir/file with spaces.txt" 2>/dev/null || true
echo "test" > "wide_dir/file_with_underscores.txt"
echo "test" > "wide_dir/file-with-dashes.txt"
echo "test" > "wide_dir/file.multiple.dots.txt"

# Create a README in the root to explain the structure
cat > README.md << 'EOF'
# Wide Directory Test Image

This VaFS image contains a single directory with many files.
Purpose: Test directory traversal and listing performance with wide directories.

Structure:
- wide_dir/: Contains many files in a single directory
EOF

echo ""
echo "Test data generation complete!"
echo "Source directory size: $(du -sh . | cut -f1)"
echo "File count: $(find . -type f | wc -l)"
echo "Files in wide_dir: $(find wide_dir -type f | wc -l)"
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
