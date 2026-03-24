/**
 * Fuzzing harness for VaFS directory traversal
 *
 * This harness fuzzes directory parsing and traversal operations including:
 * - Directory entry parsing
 * - Descriptor validation (files, directories, symlinks)
 * - Name length validation
 * - Directory entry count limits
 *
 * Target areas:
 * - __load_directory() and __read_descriptor()
 * - __validate_file_descriptor()
 * - __validate_directory_descriptor()
 * - __validate_symlink_descriptor()
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <vafs/vafs.h>
#include <vafs/directory.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 48) {
        return 0;
    }

    struct VaFs* vafs = NULL;
    int result = vafs_open_memory(data, size, &vafs);

    if (result == 0 && vafs != NULL) {
        // Try to open and traverse the root directory
        struct VaFsDirectoryHandle* rootHandle = NULL;
        result = vafs_directory_open(vafs, "/", &rootHandle);

        if (result == 0 && rootHandle != NULL) {
            struct VaFsEntry entry;
            char pathBuffer[VAFS_PATH_MAX];

            // Read all entries in root directory
            while (vafs_directory_read(rootHandle, &entry) == 0) {
                // For each entry, try to open it if it's a directory
                if (entry.Type == VaFsEntryType_Directory) {
                    snprintf(pathBuffer, sizeof(pathBuffer), "/%s", entry.Name);
                    struct VaFsDirectoryHandle* subDirHandle = NULL;

                    if (vafs_directory_open(vafs, pathBuffer, &subDirHandle) == 0) {
                        // Try to read subdirectory entries
                        struct VaFsEntry subEntry;
                        while (vafs_directory_read(subDirHandle, &subEntry) == 0) {
                            // Exercise subdirectory parsing
                        }
                        vafs_directory_close(subDirHandle);
                    }
                }
            }
            vafs_directory_close(rootHandle);
        }

        vafs_close(vafs);
    }

    return 0;
}
