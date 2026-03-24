/**
 * Fuzzing harness for VaFS file operations
 *
 * This harness fuzzes file opening and reading operations including:
 * - Path resolution and traversal
 * - File descriptor validation
 * - File read bounds checking
 * - Integer overflow detection in offset calculations
 *
 * Target areas:
 * - vafs_file_open() and __vafs_file_open_internal()
 * - vafs_file_read()
 * - File position validation and overflow checks
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <vafs/vafs.h>
#include <vafs/directory.h>
#include <vafs/file.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 48) {
        return 0;
    }

    struct VaFs* vafs = NULL;
    int result = vafs_open_memory(data, size, &vafs);

    if (result == 0 && vafs != NULL) {
        // First, enumerate files in the root directory
        struct VaFsDirectoryHandle* dirHandle = NULL;
        result = vafs_directory_open(vafs, "/", &dirHandle);

        if (result == 0 && dirHandle != NULL) {
            struct VaFsEntry entry;
            char pathBuffer[VAFS_PATH_MAX];

            while (vafs_directory_read(dirHandle, &entry) == 0) {
                // Try to open files
                if (entry.Type == VaFsEntryType_File) {
                    snprintf(pathBuffer, sizeof(pathBuffer), "/%s", entry.Name);
                    struct VaFsFileHandle* fileHandle = NULL;

                    if (vafs_file_open(vafs, pathBuffer, &fileHandle) == 0) {
                        // Try to read file data
                        uint8_t readBuffer[4096];

                        // Try reading at different positions
                        vafs_file_read(fileHandle, readBuffer, sizeof(readBuffer));

                        // Try seeking and reading again
                        vafs_file_seek(fileHandle, 1024, SEEK_SET);
                        vafs_file_read(fileHandle, readBuffer, sizeof(readBuffer));

                        // Try reading at a large offset
                        vafs_file_seek(fileHandle, 0xFFFFFF, SEEK_SET);
                        vafs_file_read(fileHandle, readBuffer, sizeof(readBuffer));

                        vafs_file_close(fileHandle);
                    }
                }
            }
            vafs_directory_close(dirHandle);
        }

        vafs_close(vafs);
    }

    return 0;
}
