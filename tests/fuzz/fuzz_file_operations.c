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
 * - vafs_object_reader_open()
 * - vafs_object_reader_read()
 * - File position validation and overflow checks
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <vafs/vafs.h>
#include <vafs/reader.h>
#include <vafs/builder.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 48) {
        return 0;
    }

    struct VaFs* vafs = NULL;
    int result = vafs_reader_open_memory(data, size, NULL, &vafs);

    if (result == 0 && vafs != NULL) {
        // First, enumerate files in the root directory
        struct VaFsDirectoryReader* dirHandle = NULL;
        result = vafs_directory_reader_open(vafs, "/", VaFsLookup_None, &dirHandle);

        if (result == 0 && dirHandle != NULL) {
            struct VaFsEntry entry;
            char pathBuffer[VAFS_PATH_MAX];

            while (vafs_directory_reader_next(dirHandle, &entry) == 0) {
                // Try to open files
                if (entry.Type == VaFsEntryType_File) {
                    snprintf(pathBuffer, sizeof(pathBuffer), "/%s", entry.Name);
                    struct VaFsObjectReader* fileHandle = NULL;

                    if (vafs_object_reader_open(vafs, pathBuffer, VaFsLookup_None, &fileHandle) == 0) {
                        // Try to read file data
                        uint8_t readBuffer[4096];

                        // Try reading at different positions
                        vafs_object_reader_read(fileHandle, readBuffer, sizeof(readBuffer));

                        // Try seeking and reading again
                        vafs_object_reader_seek(fileHandle, 1024, SEEK_SET);
                        vafs_object_reader_read(fileHandle, readBuffer, sizeof(readBuffer));

                        // Try reading at a large offset
                        vafs_object_reader_seek(fileHandle, 0xFFFFFF, SEEK_SET);
                        vafs_object_reader_read(fileHandle, readBuffer, sizeof(readBuffer));

                        vafs_object_reader_close(fileHandle);
                    }
                }
            }
            vafs_directory_reader_close(dirHandle);
        }

        vafs_reader_close(vafs);
    }

    return 0;
}
