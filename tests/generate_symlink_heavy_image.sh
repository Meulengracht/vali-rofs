#!/bin/bash
# VaFS Symlink-Heavy Image Generator
# Creates a VaFS image with many symbolic links for symlink resolution testing

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/../build}"
TEST_DATA_DIR="${TEST_DATA_DIR:-/tmp/vafs-test-symlinks}"
OUTPUT_IMAGE="${OUTPUT_IMAGE:-${TEST_DATA_DIR}/symlink-heavy.vafs}"

echo "VaFS Symlink-Heavy Image Generator"
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
rm -rf "${TEST_DATA_DIR}"
mkdir -p "${TEST_DATA_DIR}/source"
cd "${TEST_DATA_DIR}/source"

echo "Generating symlink-heavy test data..."

# Create directory structure
mkdir -p files/dir1 files/dir2 files/dir3
mkdir -p links/set1 links/set2 links/set3 links/chains

# Create real files to link to
echo "Creating real files..."
for i in {1..30}; do
    echo "File content ${i}" > "files/dir1/file${i}.txt"
done

for i in {1..30}; do
    dd if=/dev/urandom of="files/dir2/binary${i}.bin" bs=512 count=$((1 + i % 5)) 2>/dev/null
done

for i in {1..20}; do
    cat > "files/dir3/config${i}.conf" << EOF
# Config file ${i}
setting${i}=value${i}
EOF
done

# Create nested structure
mkdir -p files/dir1/deep/nested/path
echo "Deep content" > files/dir1/deep/nested/path/deep.txt

# Create symlinks set 1 - simple relative links
echo "Creating symlink set 1..."
for i in {1..30}; do
    ln -s "../../files/dir1/file${i}.txt" "links/set1/link${i}.txt"
done

# Create symlinks set 2 - linking to binaries
echo "Creating symlink set 2..."
for i in {1..30}; do
    ln -s "../../files/dir2/binary${i}.bin" "links/set2/bin${i}.bin"
done

# Create symlinks set 3 - linking to config files
echo "Creating symlink set 3..."
for i in {1..20}; do
    ln -s "../../files/dir3/config${i}.conf" "links/set3/cfg${i}.conf"
done

# Create symlink chains (symlink -> symlink -> file)
echo "Creating symlink chains..."
echo "Target file" > files/target.txt
ln -s "../files/target.txt" "links/chains/level1.txt"
ln -s "level1.txt" "links/chains/level2.txt"
ln -s "level2.txt" "links/chains/level3.txt"
ln -s "level3.txt" "links/chains/level4.txt"

# Create additional mixed symlinks
for i in {1..10}; do
    ln -s "../../files/dir1/deep/nested/path/deep.txt" "links/chains/deep_link${i}.txt"
done

# Create a README
cat > README.md << 'EOF'
# Symlink-Heavy Test Image

This VaFS image contains many symbolic links for testing symlink resolution.

Structure:
- files/dir1/: Text files
- files/dir2/: Binary files
- files/dir3/: Config files
- links/set1/: Symlinks to text files (30 links)
- links/set2/: Symlinks to binary files (30 links)
- links/set3/: Symlinks to config files (20 links)
- links/chains/: Chained symlinks and deep path links

Total: 80+ real files, 90+ symlinks

Note: VaFS does not support broken or circular symlinks.
EOF

echo ""
echo "Test data generation complete!"
echo "Source directory size: $(du -sh . | cut -f1)"
echo "File count: $(find . -type f | wc -l)"
echo "Symlink count: $(find . -type l | wc -l)"
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
