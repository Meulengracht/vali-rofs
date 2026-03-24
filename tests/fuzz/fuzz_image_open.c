/**
 * Fuzzing harness for VaFS image opening and header parsing
 *
 * This harness fuzzes vafs_open_memory() which parses the VaFS image header,
 * validates all header fields, loads features, and initializes the image.
 *
 * Target areas:
 * - Header validation (magic, version, offsets, etc.)
 * - Feature loading and parsing
 * - Stream initialization
 * - Root descriptor validation
 */

#include <stdint.h>
#include <stddef.h>
#include <vafs/vafs.h>
#include <vafs/directory.h>

// LibFuzzer entry point
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    // Minimum size for a VaFS header is 48 bytes
    if (size < 48) {
        return 0;
    }

    // Try to open the fuzzed data as a VaFS image
    struct VaFs* vafs = NULL;
    int result = vafs_open_memory(data, size, &vafs);

    // If opening succeeded, perform some basic operations to exercise the parser
    if (result == 0 && vafs != NULL) {
        // Try to open the root directory
        struct VaFsDirectoryHandle* dirHandle = NULL;
        result = vafs_directory_open(vafs, "/", &dirHandle);

        if (result == 0 && dirHandle != NULL) {
            // Try to read directory entries
            struct VaFsEntry entry;
            while (vafs_directory_read(dirHandle, &entry) == 0) {
                // Exercise directory traversal
            }
            vafs_directory_close(dirHandle);
        }

        // Clean up
        vafs_close(vafs);
    }

    return 0;
}
