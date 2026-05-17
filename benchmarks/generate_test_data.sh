#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/../build}"
TEST_DATA_DIR="${TEST_DATA_DIR:-/tmp/vafs-benchmark-data}"
OUTPUT_IMAGE="${OUTPUT_IMAGE:-${TEST_DATA_DIR}/benchmark.vafs}"

resolve_mkvafs() {
    local candidate

    for candidate in \
        "${BUILD_DIR}/bin/mkvafs" \
        "${BUILD_DIR}/bin/Debug/mkvafs.exe" \
        "${BUILD_DIR}/bin/Release/mkvafs.exe" \
        "${BUILD_DIR}/bin/RelWithDebInfo/mkvafs.exe" \
        "${BUILD_DIR}/bin/MinSizeRel/mkvafs.exe"
    do
        if [ -f "${candidate}" ]; then
            printf '%s' "${candidate}"
            return 0
        fi
    done

    return 1
}

config_block() {
    local label="$1"
    local index="$2"
    local service shard feature

    service=$(printf 'svc-%03d' $(( (index % 23) + 1 )))
    shard=$(( index % 8 ))
    feature="false"
    if [ $(( index % 3 )) -eq 0 ]; then
        feature="true"
    fi

    cat <<EOF
# VaFS benchmark payload
name=${label}
service=${service}
profile=readonly
arch=all
cache=shared
shard=${shard}
feature.alpha=true
feature.beta=${feature}
paths=/usr/bin/${service};/usr/lib/${service};/etc/${service}
notes=Deterministic benchmark content for reproducible compression and stable timing.
EOF
}

unit_block() {
    local label="$1"
    local index="$2"
    local service shard

    service=$(printf 'svc-%03d' $(( (index % 23) + 1 )))
    shard=$(( index % 8 ))

    cat <<EOF
[unit]
Description=${label} benchmark unit
After=network-online.target local-fs.target
[service]
Type=simple
ExecStart=/usr/bin/${service} --config /etc/${service}.conf --shard=${shard}
Restart=always
Environment=PROFILE=benchmark
Environment=MODE=readonly
Environment=TARGET=${label}
EOF
}

json_block() {
    local label="$1"
    local index="$2"
    local service shard

    service=$(printf 'svc-%03d' $(( (index % 23) + 1 )))
    shard=$(( index % 8 ))

    cat <<EOF
{
  "name": "${label}",
  "service": "${service}",
  "shard": ${shard},
  "files": ["manifest.txt", "settings.ini", "payload.bin"],
  "mount": "/opt/${service}",
  "compression": "brieflz",
  "deterministic": true
}
EOF
}

manifest_block() {
    local label="$1"
    local index="$2"

    cat <<EOF
manifest.version=1
manifest.name=${label}
manifest.index=${index}
layers=base,network,storage,services
paths=/etc/${label}.conf,/usr/share/${label}/manifest.txt,/var/lib/${label}/state.db
checksums=00112233,44556677,8899aabb,ccddeeff
EOF
}

write_pattern_file() {
    local path="$1"
    local size="$2"
    shift 2
    local -a blocks=("$@")
    local written=0
    local block_index=0

    : > "${path}"
    while [ "${written}" -lt "${size}" ]; do
        local block="${blocks[$((block_index % ${#blocks[@]}))]}"
        printf '%s' "${block}" >> "${path}"
        written=$((written + ${#block}))
        block_index=$((block_index + 1))
    done

    if [ "${written}" -gt "${size}" ]; then
        head -c "${size}" "${path}" > "${path}.tmp"
        mv "${path}.tmp" "${path}"
    fi
}

write_record_file() {
    local path="$1"
    local size="$2"
    local label="$3"
    local seed="$4"
    local written=0
    local record_index=0

    : > "${path}"
    while [ "${written}" -lt "${size}" ]; do
        local service mode checksum line_one line_two

        service=$(printf 'svc-%03d' $(( ((record_index + seed) % 23) + 1 )))
        mode="shared"
        if [ $(( (record_index + seed) % 2 )) -ne 0 ]; then
            mode="private"
        fi
        checksum=$(printf '%08x' $(( ((record_index * 1103515245) + (seed * 12345)) & 0xffffffff )))

        printf -v line_one 'record=%08d label=%s service=%s path=/usr/lib/%s/module/%03d.so mode=%s checksum=%s shard=%d flags=ro,cache,deterministic\n' \
            "${record_index}" "${label}" "${service}" "${service}" "$(( (record_index + seed) % 257 ))" "${mode}" "${checksum}" "$(( (record_index + seed) % 8 ))"
        printf -v line_two '{"record":%d,"label":"%s","service":"%s","owner":"root","retry":%d,"slice":"%02d","compression":"brieflz"}\n' \
            "${record_index}" "${label}" "${service}" "$(( (record_index + seed) % 5 ))" "$(( (record_index + seed) % 64 ))"

        printf '%s%s' "${line_one}" "${line_two}" >> "${path}"
        written=$((written + ${#line_one} + ${#line_two}))
        record_index=$((record_index + 1))
    done

    if [ "${written}" -gt "${size}" ]; then
        head -c "${size}" "${path}" > "${path}.tmp"
        mv "${path}.tmp" "${path}"
    fi
}

echo "VaFS Benchmark Test Data Generator"
echo "==================================="
echo "Build directory: ${BUILD_DIR}"
echo "Test data directory: ${TEST_DATA_DIR}"
echo "Output image: ${OUTPUT_IMAGE}"
echo ""

if ! MKVAFS="$(resolve_mkvafs)"; then
    echo "Error: mkvafs not found under ${BUILD_DIR}"
    echo "Please build the project first: cd build && cmake .. && make"
    exit 1
fi

rm -rf "${TEST_DATA_DIR}"
mkdir -p "${TEST_DATA_DIR}/source"
cd "${TEST_DATA_DIR}/source"

echo "Generating deterministic benchmark data..."

echo "Creating small files..."
mkdir -p small_files
for i in {1..100}; do
    size=$(( (1 + ((i - 1) % 10)) * 1024 ))
    write_pattern_file "small_files/file_${i}.txt" "${size}" \
        "$(config_block "small-file-${i}" "${i}")" \
        "$(unit_block "small-file-${i}" "${i}")"
done

write_pattern_file "small.txt" $((4 * 1024)) \
    "$(config_block "small-target" 401)" \
    "$(json_block "small-target" 401)"

echo "Creating large files..."
mkdir -p large_files
for i in {1..10}; do
    size=$(( i * 1024 * 1024 ))
    write_record_file "large_files/file_${i}.bin" "${size}" "large-file-${i}" "$((1000 + i))"
done

write_record_file "large.bin" $((5 * 1024 * 1024)) "large-target" 2048

echo "Creating directory structure..."
mkdir -p deep/path/to/test/nested/directories/for/traversal/benchmark
mkdir -p wide_dir
for i in {1..500}; do
    printf 'File %d\ncategory=wide\nslot=%03d\n' "${i}" "$((i % 32))" > "wide_dir/file_${i}.txt"
done

mkdir -p lookup_test/subdir1/subdir2/subdir3
printf 'Target file for lookup benchmark\nmode=stable\n' > "lookup_test/subdir1/subdir2/subdir3/target.txt"
printf 'Test file for lookup\nmode=stable\n' > "test.txt"

echo "Creating mixed content..."
mkdir -p mixed
printf 'Hello, VaFS!\nprofile=deterministic\n' > "mixed/config.txt"
dd if=/dev/zero of="mixed/zeros.bin" bs=1024 count=100 2>/dev/null
write_record_file "mixed/random.bin" $((100 * 1024)) "mixed-random" 77

if ln -s "test.txt" "symlink_test.txt" 2>/dev/null; then
    echo "Created symlinks"
fi

echo ""
echo "Test data generation complete!"
echo "Source directory size: $(du -sh . | cut -f1)"
echo "File count: $(find . -type f | wc -l)"
echo ""

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
