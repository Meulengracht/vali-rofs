/*
 * Minimal VaFS builder API example.
 *
 * Usage:
 *   builder_example <output.vafs>
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <vafs/builder.h>
#include <vafs/stat.h>

static struct VaFsMetadata metadata_for_mode(enum VaFsEntryType type, uint32_t mode)
{
    struct VaFsMetadata metadata;

    /*
     * Metadata fields are opt-in. Initializing first keeps every field marked
     * unavailable until we explicitly set it below.
     */
    vafs_metadata_initialize(&metadata);
    vafs_metadata_set_mode(&metadata, type, mode);
    return metadata;
}

int main(int argc, char** argv)
{
    const char* payload = "Hello from a VaFS image.\n";
    struct VaFsBuilderConfiguration config;
    struct VaFs* vafs = NULL;
    struct VaFsDirectoryBuilder* root = NULL;
    struct VaFsDirectoryBuilder* docs = NULL;
    struct VaFsFileBuilder* file = NULL;
    struct VaFsObjectBuilder* docsObject = NULL;
    struct VaFsObjectBuilder* fileObject = NULL;
    struct VaFsMetadata rootFileMetadata = metadata_for_mode(VaFsEntryType_File, 0644);
    struct VaFsMetadata docsMetadata = metadata_for_mode(VaFsEntryType_Directory, 0755);
    struct VaFsMetadata symlinkMetadata = metadata_for_mode(VaFsEntryType_Symlink, 0777);
    struct VaFsMetadata fifoMetadata = metadata_for_mode(VaFsEntryType_Fifo, 0644);
    size_t bytesWritten = 0;
    int status = 1;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <output.vafs>\n", argv[0]);
        return 1;
    }

    /*
     * Hardlinks need a stable, non-zero object id on their target. Most images
     * use ids derived from host inode metadata; this example just picks one.
     */
    rootFileMetadata.ObjectId = 1;
    rootFileMetadata.Mask |= VaFsMetadataMask_ObjectId;

    /* Start with defaults, then add only the policy this example cares about. */
    vafs_builder_config_initialize(&config);
    vafs_builder_config_set_architecture(&config, VaFsArchitecture_ALL);

    /*
     * vafs_builder_new returns two handles:
     *   - vafs owns the image being created
     *   - root is an owned builder handle for the image root directory
     */
    if (vafs_builder_new(argv[1], &config, &vafs, &root) != 0) {
        perror("vafs_builder_new");
        goto cleanup;
    }

    /*
     * Directory creation returns an owned child directory builder in docs.
     * docsObject is borrowed and is useful for object-level operations such as
     * xattrs. Borrowed object builders live until the image builder closes.
     */
    if (vafs_directory_builder_create_directory(root, "docs", &docsMetadata, &docs, &docsObject) != 0) {
        perror("vafs_directory_builder_create_directory");
        goto cleanup;
    }

    /* Extended attributes are attached to object builders while the image is open for writing. */
    if (vafs_object_builder_setxattr(docsObject, "user.description", "example docs", strlen("example docs")) != 0) {
        perror("vafs_object_builder_setxattr(directory)");
        goto cleanup;
    }

    /*
     * File creation returns an owned file writer and, optionally, a borrowed
     * object handle. Keep the object handle if later calls need to refer to the
     * created file, as the hardlink call below does.
     */
    if (vafs_directory_builder_create_file(docs, "hello.txt", &rootFileMetadata, &file, &fileObject) != 0) {
        perror("vafs_directory_builder_create_file");
        goto cleanup;
    }

    /* File builders append bytes. The output count lets callers detect short writes. */
    if (vafs_file_builder_write(file, payload, strlen(payload), &bytesWritten) != 0 || bytesWritten != strlen(payload)) {
        errno = EIO;
        perror("vafs_file_builder_write");
        goto cleanup;
    }

    /* Closing the file builder finalizes the payload extent for hello.txt. */
    if (vafs_file_builder_close(file) != 0) {
        perror("vafs_file_builder_close");
        file = NULL;
        goto cleanup;
    }
    file = NULL;

    /* The borrowed file object remains valid after the file writer is closed. */
    if (vafs_object_builder_setxattr(fileObject, "user.mime", "text/plain", strlen("text/plain")) != 0) {
        perror("vafs_object_builder_setxattr(file)");
        goto cleanup;
    }

    /* A hardlink is another directory entry pointing at an existing non-directory object. */
    if (vafs_directory_builder_link(docs, "hello-alias.txt", fileObject) != 0) {
        perror("vafs_directory_builder_link");
        goto cleanup;
    }

    /* Symlink payloads are stored as the target path string. */
    if (vafs_directory_builder_create_symlink(docs, "latest", "/docs/hello.txt", &symlinkMetadata, NULL) != 0) {
        perror("vafs_directory_builder_create_symlink");
        goto cleanup;
    }

    /* Special entries use their entry type explicitly; FIFOs do not need a device number. */
    if (vafs_directory_builder_create_special(docs, "events.fifo", VaFsEntryType_Fifo, &fifoMetadata, NULL, NULL) != 0) {
        perror("vafs_directory_builder_create_special");
        goto cleanup;
    }

    status = 0;

cleanup:
    /* Close owned handles in reverse order. Borrowed object handles are not closed directly. */
    if (file != NULL) {
        vafs_file_builder_close(file);
    }
    if (docs != NULL) {
        vafs_directory_builder_close(docs);
    }
    if (root != NULL) {
        vafs_directory_builder_close(root);
    }
    /* Closing the builder writes the final image metadata and streams. */
    if (vafs != NULL && vafs_builder_close(vafs) != 0) {
        perror("vafs_builder_close");
        status = 1;
    }
    return status;
}