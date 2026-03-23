#!/bin/bash
# VaFS Medium RootFS-like Image Generator
# Creates a medium-sized VaFS image simulating a typical Linux root filesystem structure

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/../build}"
TEST_DATA_DIR="${TEST_DATA_DIR:-/tmp/vafs-test-rootfs}"
OUTPUT_IMAGE="${OUTPUT_IMAGE:-${TEST_DATA_DIR}/rootfs.vafs}"

echo "VaFS Medium RootFS-like Image Generator"
echo "========================================"
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

echo "Generating rootfs-like test data..."

# Create typical Linux directory structure
mkdir -p bin sbin lib lib64 usr/bin usr/sbin usr/lib usr/share etc/config etc/init.d
mkdir -p var/log var/tmp opt home/user dev proc sys tmp

# Simulate binaries in /bin (10-50KB each)
echo "Creating binaries..."
for bin in ls cat grep sed awk sh bash ps kill; do
    dd if=/dev/urandom of="bin/${bin}" bs=1024 count=$((10 + RANDOM % 40)) 2>/dev/null
    chmod +x "bin/${bin}"
done

# Simulate system binaries in /sbin (20-100KB each)
for sbin in init mount umount fsck reboot; do
    dd if=/dev/urandom of="sbin/${sbin}" bs=1024 count=$((20 + RANDOM % 80)) 2>/dev/null
    chmod +x "sbin/${sbin}"
done

# Create libraries (100-500KB each)
echo "Creating libraries..."
for i in {1..5}; do
    dd if=/dev/urandom of="lib/lib${i}.so.1.0" bs=1024 count=$((100 + RANDOM % 400)) 2>/dev/null
done

# Create configuration files
echo "Creating configuration files..."
cat > etc/config/system.conf << 'EOF'
# System Configuration
hostname=testhost
timezone=UTC
locale=en_US.UTF-8
EOF

cat > etc/config/network.conf << 'EOF'
# Network Configuration
interface=eth0
address=192.168.1.100
netmask=255.255.255.0
gateway=192.168.1.1
EOF

# Create init scripts
for script in network syslog cron; do
    cat > "etc/init.d/${script}" << 'EOF'
#!/bin/sh
# Init script
case "$1" in
    start) echo "Starting service" ;;
    stop)  echo "Stopping service" ;;
    *)     echo "Usage: $0 {start|stop}" ;;
esac
EOF
    chmod +x "etc/init.d/${script}"
done

# Create log files
echo "Creating log files..."
for log in syslog messages auth kern; do
    for i in {1..50}; do
        echo "$(date '+%Y-%m-%d %H:%M:%S') [INFO] Sample log entry $i" >> "var/log/${log}.log"
    done
done

# Create user files
mkdir -p home/user/.config home/user/documents home/user/downloads
echo "export PATH=/bin:/usr/bin" > home/user/.profile
echo "alias ll='ls -la'" > home/user/.bashrc
echo "User document content" > home/user/documents/readme.txt

# Create shared data
mkdir -p usr/share/doc usr/share/man/man1
for i in {1..10}; do
    echo "Documentation for application $i" > "usr/share/doc/app${i}.txt"
    echo ".TH APP${i} 1" > "usr/share/man/man1/app${i}.1"
done

# Create some empty marker files
touch etc/hostname etc/hosts etc/passwd

echo ""
echo "Test data generation complete!"
echo "Source directory size: $(du -sh . | cut -f1)"
echo "File count: $(find . -type f | wc -l)"
echo "Directory count: $(find . -type d | wc -l)"
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
