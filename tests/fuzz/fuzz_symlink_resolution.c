/**
 * Fuzzing harness for VaFS symlink resolution
 *
 * This harness fuzzes symlink resolution and path canonicalization including:
 * - Symlink descriptor validation
 * - Circular symlink detection
 * - Path resolution with relative and absolute symlinks
 * - Symlink depth limit enforcement (max 40)
 * - Buffer overflow checks in path concatenation
 *
 * Target areas:
 * - vafs_object_reader_open() and vafs_object_reader_read()
 * - __vafs_resolve_symlink()
 * - Object and directory reader path lookup with symlinks
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
        // Enumerate all entries looking for symlinks
        struct VaFsDirectoryReader* dirHandle = NULL;
        result = vafs_directory_reader_open(vafs, "/", VaFsLookup_None, &dirHandle);

        if (result == 0 && dirHandle != NULL) {
            struct VaFsEntry entry;
            char pathBuffer[VAFS_PATH_MAX];
            char targetBuffer[VAFS_PATH_MAX];

            while (vafs_directory_reader_next(dirHandle, &entry) == 0) {
                snprintf(pathBuffer, sizeof(pathBuffer), "/%s", entry.Name);

                // Try to open symlinks
                if (entry.Type == VaFsEntryType_Symlink) {
                    struct VaFsObjectReader* symlinkHandle = NULL;

                    if (vafs_object_reader_open(vafs, pathBuffer, VaFsLookup_NoFollow, &symlinkHandle) == 0) {
                        // Read symlink target
                        if (vafs_object_reader_read(symlinkHandle, targetBuffer, sizeof(targetBuffer)) != (uint64_t)-1) {
                            // Try to follow the symlink by opening it as a file
                            struct VaFsObjectReader* fileHandle = NULL;
                            vafs_object_reader_open(vafs, pathBuffer, VaFsLookup_None, &fileHandle);
                            if (fileHandle != NULL) {
                                vafs_object_reader_close(fileHandle);
                            }

                            // Try to follow the symlink by opening it as a directory
                            struct VaFsDirectoryReader* subDirHandle = NULL;
                            vafs_directory_reader_open(vafs, pathBuffer, VaFsLookup_None, &subDirHandle);
                            if (subDirHandle != NULL) {
                                vafs_directory_reader_close(subDirHandle);
                            }
                        }

                        vafs_object_reader_close(symlinkHandle);
                    }
                }
                // Also try to open regular files/directories through potential symlink paths
                else if (entry.Type == VaFsEntryType_File) {
                    struct VaFsObjectReader* fileHandle = NULL;
                    vafs_object_reader_open(vafs, pathBuffer, VaFsLookup_None, &fileHandle);
                    if (fileHandle != NULL) {
                        vafs_object_reader_close(fileHandle);
                    }
                }
                else if (entry.Type == VaFsEntryType_Directory) {
                    struct VaFsDirectoryReader* subDirHandle = NULL;
                    vafs_directory_reader_open(vafs, pathBuffer, VaFsLookup_None, &subDirHandle);
                    if (subDirHandle != NULL) {
                        vafs_directory_reader_close(subDirHandle);
                    }
                }
            }
            vafs_directory_reader_close(dirHandle);
        }

        vafs_reader_close(vafs);
    }

    return 0;
}
