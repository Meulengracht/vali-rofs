/*
 * Minimal VaFS reader API example.
 *
 * Usage:
 *   reader_example <image.vafs>
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <vafs/reader.h>
#include <vafs/stat.h>

static const char* entry_type_name(enum VaFsEntryType type)
{
    switch (type) {
        case VaFsEntryType_File:
            return "file";
        case VaFsEntryType_Directory:
            return "directory";
        case VaFsEntryType_Symlink:
            return "symlink";
        case VaFsEntryType_CharacterDevice:
            return "character-device";
        case VaFsEntryType_BlockDevice:
            return "block-device";
        case VaFsEntryType_Fifo:
            return "fifo";
        case VaFsEntryType_Hardlink:
            return "hardlink";
        default:
            return "unknown";
    }
}

static void list_directory(struct VaFs* vafs, const char* path)
{
    struct VaFsDirectoryReader* directory = NULL;
    struct VaFsEntry entry;

    /* Directory readers enumerate one opened directory. VaFsLookup_None follows symlinks. */
    if (vafs_directory_reader_open(vafs, path, VaFsLookup_None, &directory) != 0) {
        perror("vafs_directory_reader_open");
        return;
    }

    printf("entries in %s:\n", path);
    /* Each successful next call fills entry with borrowed strings valid until the reader closes. */
    while (vafs_directory_reader_next(directory, &entry) == 0) {
        printf("  %-16s %s\n", entry_type_name(entry.Type), entry.Name);
    }

    vafs_directory_reader_close(directory);
}

static void print_xattrs(struct VaFsObjectReader* object)
{
    char names[256];
    size_t bytesWritten = 0;
    size_t offset = 0;

    /* Query the packed name-list size first. A zero-byte list means no xattrs. */
    if (vafs_object_reader_listxattr(object, NULL, 0, &bytesWritten) != 0 || bytesWritten == 0) {
        return;
    }
    if (bytesWritten > sizeof(names)) {
        printf("xattrs: %zu bytes required\n", bytesWritten);
        return;
    }
    if (vafs_object_reader_listxattr(object, names, sizeof(names), &bytesWritten) != 0) {
        perror("vafs_object_reader_listxattr");
        return;
    }

    printf("xattrs:\n");
    /* Names are returned as consecutive NUL-terminated strings. */
    while (offset < bytesWritten) {
        char value[128];
        size_t valueSize = 0;
        const char* name = names + offset;

        /* This example assumes text xattrs so it can print values directly. */
        if (vafs_object_reader_getxattr(object, name, value, sizeof(value) - 1, &valueSize) == 0) {
            value[valueSize < sizeof(value) ? valueSize : sizeof(value) - 1] = '\0';
            printf("  %s=%s\n", name, value);
        }
        offset += strlen(name) + 1;
    }
}

int main(int argc, char** argv)
{
    struct VaFs* vafs = NULL;
    struct VaFsObjectReader* object = NULL;
    struct VaFsMetadata metadata;
    char buffer[256];
    uint64_t bytesRead;
    int status = 1;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <image.vafs>\n", argv[0]);
        return 1;
    }

    /* Passing NULL configuration uses the default reader codecs and policy. */
    if (vafs_reader_open_file(argv[1], NULL, &vafs) != 0) {
        perror("vafs_reader_open_file");
        return 1;
    }

    list_directory(vafs, "/");
    list_directory(vafs, "/docs");

    /* Object readers are the common read handle for files, symlinks, and special entries. */
    if (vafs_object_reader_open(vafs, "/docs/hello.txt", VaFsLookup_None, &object) != 0) {
        perror("vafs_object_reader_open(file)");
        goto cleanup;
    }

    /* Stat reads metadata for the already-open object. */
    if (vafs_object_reader_stat(object, &metadata) == 0) {
        printf("/docs/hello.txt: type=%s size=%" PRIu64 " mode=%o\n",
               entry_type_name(metadata.Type), metadata.Size, metadata.Mode);
    }

    /* Reads advance the object's cursor. Use seek if you need to reread from another offset. */
    bytesRead = vafs_object_reader_read(object, buffer, sizeof(buffer) - 1);
    if (bytesRead != UINT64_MAX) {
        buffer[bytesRead] = '\0';
        printf("content: %s", buffer);
    }

    print_xattrs(object);
    vafs_object_reader_close(object);
    object = NULL;

    /* NoFollow opens the symlink object itself, so reading returns the stored target path. */
    if (vafs_object_reader_open(vafs, "/docs/latest", VaFsLookup_NoFollow, &object) == 0) {
        bytesRead = vafs_object_reader_read(object, buffer, sizeof(buffer) - 1);
        if (bytesRead != UINT64_MAX) {
            buffer[bytesRead] = '\0';
            printf("/docs/latest symlink target: %s\n", buffer);
        }
        vafs_object_reader_close(object);
        object = NULL;
    }

    status = 0;

cleanup:
    if (object != NULL) {
        vafs_object_reader_close(object);
    }
    vafs_reader_close(vafs);
    return status;
}