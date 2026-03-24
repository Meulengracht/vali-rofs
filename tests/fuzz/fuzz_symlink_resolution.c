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
 * - vafs_symlink_open() and vafs_symlink_target()
 * - __vafs_resolve_symlink()
 * - __vafs_file_open_internal() with symlinks
 * - __vafs_directory_open_internal() with symlinks
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <vafs/vafs.h>
#include <vafs/directory.h>
#include <vafs/file.h>
#include <vafs/symlink.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 48) {
        return 0;
    }

    struct VaFs* vafs = NULL;
    int result = vafs_open_memory(data, size, &vafs);

    if (result == 0 && vafs != NULL) {
        // Enumerate all entries looking for symlinks
        struct VaFsDirectoryHandle* dirHandle = NULL;
        result = vafs_directory_open(vafs, "/", &dirHandle);

        if (result == 0 && dirHandle != NULL) {
            struct VaFsEntry entry;
            char pathBuffer[VAFS_PATH_MAX];
            char targetBuffer[VAFS_PATH_MAX];

            while (vafs_directory_read(dirHandle, &entry) == 0) {
                snprintf(pathBuffer, sizeof(pathBuffer), "/%s", entry.Name);

                // Try to open symlinks
                if (entry.Type == VaFsEntryType_Symlink) {
                    struct VaFsSymlinkHandle* symlinkHandle = NULL;

                    if (vafs_symlink_open(vafs, pathBuffer, &symlinkHandle) == 0) {
                        // Read symlink target
                        if (vafs_symlink_target(symlinkHandle, targetBuffer, sizeof(targetBuffer)) == 0) {
                            // Try to follow the symlink by opening it as a file
                            struct VaFsFileHandle* fileHandle = NULL;
                            vafs_file_open(vafs, pathBuffer, &fileHandle);
                            if (fileHandle != NULL) {
                                vafs_file_close(fileHandle);
                            }

                            // Try to follow the symlink by opening it as a directory
                            struct VaFsDirectoryHandle* subDirHandle = NULL;
                            vafs_directory_open(vafs, pathBuffer, &subDirHandle);
                            if (subDirHandle != NULL) {
                                vafs_directory_close(subDirHandle);
                            }
                        }

                        vafs_symlink_close(symlinkHandle);
                    }
                }
                // Also try to open regular files/directories through potential symlink paths
                else if (entry.Type == VaFsEntryType_File) {
                    struct VaFsFileHandle* fileHandle = NULL;
                    vafs_file_open(vafs, pathBuffer, &fileHandle);
                    if (fileHandle != NULL) {
                        vafs_file_close(fileHandle);
                    }
                }
                else if (entry.Type == VaFsEntryType_Directory) {
                    struct VaFsDirectoryHandle* subDirHandle = NULL;
                    vafs_directory_open(vafs, pathBuffer, &subDirHandle);
                    if (subDirHandle != NULL) {
                        vafs_directory_close(subDirHandle);
                    }
                }
            }
            vafs_directory_close(dirHandle);
        }

        vafs_close(vafs);
    }

    return 0;
}
