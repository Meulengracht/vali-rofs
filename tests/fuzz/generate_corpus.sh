#!/bin/bash
# Script to generate corpus seed files for fuzzing
# These are intentionally malformed VaFS images to seed the fuzzer

set -e

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORPUS_DIR="${SCRIPT_DIR}/corpus"

echo "Generating fuzzing corpus seeds in ${CORPUS_DIR}"

# Create corpus directory if it doesn't exist
mkdir -p "${CORPUS_DIR}"

# Helper function to write a hex byte to a file
write_hex() {
    local file="$1"
    local hex="$2"
    printf "\\x${hex}" >> "$file"
}

# Helper function to write a 32-bit little-endian integer
write_u32() {
    local file="$1"
    local value="$2"
    printf "$(printf '\\x%02x\\x%02x\\x%02x\\x%02x' $((value & 0xFF)) $(((value >> 8) & 0xFF)) $(((value >> 16) & 0xFF)) $(((value >> 24) & 0xFF)))" >> "$file"
}

# Helper function to write a 16-bit little-endian integer
write_u16() {
    local file="$1"
    local value="$2"
    printf "$(printf '\\x%02x\\x%02x' $((value & 0xFF)) $(((value >> 8) & 0xFF)))" >> "$file"
}

# Seed 1: Minimal valid VaFS image (empty root directory)
echo "Creating seed_minimal.vafs"
{
    # VaFS Header (48 bytes)
    write_u32 "${CORPUS_DIR}/seed_minimal.vafs" 0x3144524D  # Magic
    write_u32 "${CORPUS_DIR}/seed_minimal.vafs" 0x00010000  # Version
    write_u32 "${CORPUS_DIR}/seed_minimal.vafs" 0x00000000  # Architecture
    write_u16 "${CORPUS_DIR}/seed_minimal.vafs" 0x0000      # FeatureCount
    write_u16 "${CORPUS_DIR}/seed_minimal.vafs" 0x0000      # Reserved
    write_u32 "${CORPUS_DIR}/seed_minimal.vafs" 0x00000000  # Attributes
    write_u32 "${CORPUS_DIR}/seed_minimal.vafs" 48          # DescriptorBlockOffset
    write_u32 "${CORPUS_DIR}/seed_minimal.vafs" 8240        # DataBlockOffset (48 + 16 + 8192)
    write_u32 "${CORPUS_DIR}/seed_minimal.vafs" 0x00000000  # RootDescriptor.Index
    write_u32 "${CORPUS_DIR}/seed_minimal.vafs" 0x00000000  # RootDescriptor.Offset

    # Stream Header (16 bytes)
    write_u32 "${CORPUS_DIR}/seed_minimal.vafs" 0x314D5356  # Stream Magic
    write_u32 "${CORPUS_DIR}/seed_minimal.vafs" 8192        # BlockSize
    write_u32 "${CORPUS_DIR}/seed_minimal.vafs" 8208        # BlockHeadersOffset
    write_u32 "${CORPUS_DIR}/seed_minimal.vafs" 0           # BlockHeadersCount

    # Directory Header (4 bytes)
    write_u32 "${CORPUS_DIR}/seed_minimal.vafs" 0           # Count = 0 (empty directory)

    # Pad to descriptor block size (8192 - 16 - 4 = 8172 bytes of padding)
    dd if=/dev/zero bs=1 count=8172 >> "${CORPUS_DIR}/seed_minimal.vafs" 2>/dev/null
}

# Seed 2: Invalid magic number
echo "Creating seed_bad_magic.vafs"
{
    write_u32 "${CORPUS_DIR}/seed_bad_magic.vafs" 0xDEADBEEF  # Bad Magic
    write_u32 "${CORPUS_DIR}/seed_bad_magic.vafs" 0x00010000
    write_u32 "${CORPUS_DIR}/seed_bad_magic.vafs" 0x00000000
    write_u16 "${CORPUS_DIR}/seed_bad_magic.vafs" 0x0000
    write_u16 "${CORPUS_DIR}/seed_bad_magic.vafs" 0x0000
    write_u32 "${CORPUS_DIR}/seed_bad_magic.vafs" 0x00000000
    write_u32 "${CORPUS_DIR}/seed_bad_magic.vafs" 48
    write_u32 "${CORPUS_DIR}/seed_bad_magic.vafs" 8240
    write_u32 "${CORPUS_DIR}/seed_bad_magic.vafs" 0x00000000
    write_u32 "${CORPUS_DIR}/seed_bad_magic.vafs" 0x00000000
}

# Seed 3: Invalid version
echo "Creating seed_bad_version.vafs"
{
    write_u32 "${CORPUS_DIR}/seed_bad_version.vafs" 0x3144524D
    write_u32 "${CORPUS_DIR}/seed_bad_version.vafs" 0x00020000  # Bad Version
    write_u32 "${CORPUS_DIR}/seed_bad_version.vafs" 0x00000000
    write_u16 "${CORPUS_DIR}/seed_bad_version.vafs" 0x0000
    write_u16 "${CORPUS_DIR}/seed_bad_version.vafs" 0x0000
    write_u32 "${CORPUS_DIR}/seed_bad_version.vafs" 0x00000000
    write_u32 "${CORPUS_DIR}/seed_bad_version.vafs" 48
    write_u32 "${CORPUS_DIR}/seed_bad_version.vafs" 8240
    write_u32 "${CORPUS_DIR}/seed_bad_version.vafs" 0x00000000
    write_u32 "${CORPUS_DIR}/seed_bad_version.vafs" 0x00000000
}

# Seed 4: Excessive feature count
echo "Creating seed_excessive_features.vafs"
{
    write_u32 "${CORPUS_DIR}/seed_excessive_features.vafs" 0x3144524D
    write_u32 "${CORPUS_DIR}/seed_excessive_features.vafs" 0x00010000
    write_u32 "${CORPUS_DIR}/seed_excessive_features.vafs" 0x00000000
    write_u16 "${CORPUS_DIR}/seed_excessive_features.vafs" 100  # Way over max of 16
    write_u16 "${CORPUS_DIR}/seed_excessive_features.vafs" 0x0000
    write_u32 "${CORPUS_DIR}/seed_excessive_features.vafs" 0x00000000
    write_u32 "${CORPUS_DIR}/seed_excessive_features.vafs" 48
    write_u32 "${CORPUS_DIR}/seed_excessive_features.vafs" 8240
    write_u32 "${CORPUS_DIR}/seed_excessive_features.vafs" 0x00000000
    write_u32 "${CORPUS_DIR}/seed_excessive_features.vafs" 0x00000000
}

# Seed 5: Invalid root descriptor index
echo "Creating seed_invalid_root_index.vafs"
{
    write_u32 "${CORPUS_DIR}/seed_invalid_root_index.vafs" 0x3144524D
    write_u32 "${CORPUS_DIR}/seed_invalid_root_index.vafs" 0x00010000
    write_u32 "${CORPUS_DIR}/seed_invalid_root_index.vafs" 0x00000000
    write_u16 "${CORPUS_DIR}/seed_invalid_root_index.vafs" 0x0000
    write_u16 "${CORPUS_DIR}/seed_invalid_root_index.vafs" 0x0000
    write_u32 "${CORPUS_DIR}/seed_invalid_root_index.vafs" 0x00000000
    write_u32 "${CORPUS_DIR}/seed_invalid_root_index.vafs" 48
    write_u32 "${CORPUS_DIR}/seed_invalid_root_index.vafs" 8240
    write_u32 "${CORPUS_DIR}/seed_invalid_root_index.vafs" 0x0000FFFF  # Invalid block index
    write_u32 "${CORPUS_DIR}/seed_invalid_root_index.vafs" 0x00000000
}

# Seed 6: Truncated header
echo "Creating seed_truncated.vafs"
{
    write_u32 "${CORPUS_DIR}/seed_truncated.vafs" 0x3144524D
    write_u32 "${CORPUS_DIR}/seed_truncated.vafs" 0x00010000
    write_u32 "${CORPUS_DIR}/seed_truncated.vafs" 0x00000000
    # Only 12 bytes - truncated header
}

# Seed 7: Overlapping offsets
echo "Creating seed_offset_collision.vafs"
{
    write_u32 "${CORPUS_DIR}/seed_offset_collision.vafs" 0x3144524D
    write_u32 "${CORPUS_DIR}/seed_offset_collision.vafs" 0x00010000
    write_u32 "${CORPUS_DIR}/seed_offset_collision.vafs" 0x00000000
    write_u16 "${CORPUS_DIR}/seed_offset_collision.vafs" 0x0000
    write_u16 "${CORPUS_DIR}/seed_offset_collision.vafs" 0x0000
    write_u32 "${CORPUS_DIR}/seed_offset_collision.vafs" 0x00000000
    write_u32 "${CORPUS_DIR}/seed_offset_collision.vafs" 48
    write_u32 "${CORPUS_DIR}/seed_offset_collision.vafs" 48  # Same as descriptor offset!
    write_u32 "${CORPUS_DIR}/seed_offset_collision.vafs" 0x00000000
    write_u32 "${CORPUS_DIR}/seed_offset_collision.vafs" 0x00000000
}

# Seed 8: Excessive directory entry count
echo "Creating seed_excessive_dir_count.vafs"
{
    # Valid header
    write_u32 "${CORPUS_DIR}/seed_excessive_dir_count.vafs" 0x3144524D
    write_u32 "${CORPUS_DIR}/seed_excessive_dir_count.vafs" 0x00010000
    write_u32 "${CORPUS_DIR}/seed_excessive_dir_count.vafs" 0x00000000
    write_u16 "${CORPUS_DIR}/seed_excessive_dir_count.vafs" 0x0000
    write_u16 "${CORPUS_DIR}/seed_excessive_dir_count.vafs" 0x0000
    write_u32 "${CORPUS_DIR}/seed_excessive_dir_count.vafs" 0x00000000
    write_u32 "${CORPUS_DIR}/seed_excessive_dir_count.vafs" 48
    write_u32 "${CORPUS_DIR}/seed_excessive_dir_count.vafs" 8240
    write_u32 "${CORPUS_DIR}/seed_excessive_dir_count.vafs" 0x00000000
    write_u32 "${CORPUS_DIR}/seed_excessive_dir_count.vafs" 0x00000000

    # Stream Header
    write_u32 "${CORPUS_DIR}/seed_excessive_dir_count.vafs" 0x314D5356
    write_u32 "${CORPUS_DIR}/seed_excessive_dir_count.vafs" 8192
    write_u32 "${CORPUS_DIR}/seed_excessive_dir_count.vafs" 8208
    write_u32 "${CORPUS_DIR}/seed_excessive_dir_count.vafs" 0

    # Directory Header with excessive count
    write_u32 "${CORPUS_DIR}/seed_excessive_dir_count.vafs" 0xFFFFFFFF  # Way over max
}

echo ""
echo "Generated $(ls -1 ${CORPUS_DIR}/*.vafs 2>/dev/null | wc -l) corpus seed files"
echo "Corpus location: ${CORPUS_DIR}"
