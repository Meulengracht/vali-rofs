/*
 * Copyright, Philip Meulengracht
 *
 * Wide directory lookup regression tests.
 * Verifies deterministic ordering and lookup correctness with many entries.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/vafs.h>
#include <vafs/reader.h>
#include <vafs/builder.h>
#include <vafs/directory.h>
#include <vafs/file.h>
#include <vafs/stat.h>
#include <vafs/xattr.h>
#include "../../libvafs/private.h"

#define TEST_IMAGE_PATH "test_directory_lookups.vafs"
#define SMALL_DIR_ENTRY_COUNT 64
#define LARGE_DIR_ENTRY_COUNT 1024

static int g_test_passed = 0;
static int g_test_failed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL: %s (line %d): %s\n", __func__, __LINE__, message); \
            g_test_failed++; \
            return -1; \
        } \
    } while (0)

#define TEST_PASS(message) \
    do { \
        fprintf(stdout, "PASS: %s: %s\n", __func__, message); \
        g_test_passed++; \
        return 0; \
    } while (0)

static void make_entry_name(char* buffer, size_t buffer_size, int index)
{
    snprintf(buffer, buffer_size, "entry_%04d", index);
}

static void make_prefixed_entry_name(char* buffer, size_t buffer_size, const char* prefix, int index)
{
    snprintf(buffer, buffer_size, "%s_%04d", prefix, index);
}

static struct VaFsMetadata metadata_for_mode(
    enum VaFsEntryType type,
    uint32_t           mode)
{
    struct VaFsMetadata metadata;

    vafs_metadata_initialize(&metadata);
    vafs_metadata_set_mode(&metadata, type, mode);
    return metadata;
}

static int lookup_cache_has_state(
    struct VaFs*                vafs,
    struct VaFsDirectory*       parent,
    const char*                 name,
    enum VaFsLookupCacheState   state)
{
    size_t i;

    for (i = 0; i < VAFS_LOOKUP_CACHE_CAPACITY; i++) {
        struct VaFsLookupCacheEntry* entry = &vafs->LookupCache.Entries[i];
        if (entry->State == state && entry->Parent == parent && strcmp(entry->Name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int lookup_cache_has_name(
    struct VaFs*          vafs,
    struct VaFsDirectory* parent,
    const char*           name)
{
    return lookup_cache_has_state(vafs, parent, name, VaFsLookupCacheState_Hit) ||
        lookup_cache_has_state(vafs, parent, name, VaFsLookupCacheState_Miss);
}

static int find_collision_names(
    struct VaFsDirectory* directory,
    char                  names[VAFS_LOOKUP_CACHE_SET_ASSOCIATIVITY + 1][32])
{
    char   candidates[VAFS_LOOKUP_CACHE_SET_COUNT][VAFS_LOOKUP_CACHE_SET_ASSOCIATIVITY + 1][32];
    size_t counts[VAFS_LOOKUP_CACHE_SET_COUNT] = { 0 };
    char   name[32];
    size_t setIndex;
    int    i;

    memset(candidates, 0, sizeof(candidates));
    for (i = 0; i < LARGE_DIR_ENTRY_COUNT; i++) {
        make_entry_name(name, sizeof(name), i);
        if (strcmp(name, "entry_0000") == 0 || strcmp(name, "entry_0512") == 0 || strcmp(name, "entry_1023") == 0) {
            continue;
        }

        setIndex = __vafs_directory_lookup_cache_set(directory, name);
        if (counts[setIndex] < (VAFS_LOOKUP_CACHE_SET_ASSOCIATIVITY + 1)) {
            strcpy(candidates[setIndex][counts[setIndex]], name);
        }
        counts[setIndex]++;
        if (counts[setIndex] == (VAFS_LOOKUP_CACHE_SET_ASSOCIATIVITY + 1)) {
            size_t j;
            for (j = 0; j < (VAFS_LOOKUP_CACHE_SET_ASSOCIATIVITY + 1); j++) {
                strcpy(names[j], candidates[setIndex][j]);
            }
            return 0;
        }
    }

    return -1;
}

static int assert_repeated_path_stat(
    struct VaFs*  vafs,
    const char*   path,
    uint32_t      expected_mode,
    size_t        expected_size,
    const char*   message)
{
    struct VaFsMetadata stat;
    int              status;
    int              i;

    for (i = 0; i < 32; i++) {
        status = vafs_path_stat(vafs, path, 1, &stat);
        TEST_ASSERT(status == 0, message);
        TEST_ASSERT(stat.Mode == expected_mode, "Unexpected mode returned from repeated path stat");
        TEST_ASSERT(stat.Size == expected_size, "Unexpected size returned from repeated path stat");
    }
    return 0;
}

static int timestamps_equal(
    const struct VaFsTimestamp* left,
    const struct VaFsTimestamp* right)
{
    return left->Seconds == right->Seconds && left->Nanoseconds == right->Nanoseconds;
}

static int xattr_list_contains(
    const char* buffer,
    size_t      size,
    const char* name)
{
    size_t offset = 0;

    while (offset < size) {
        size_t entryLength = strlen(buffer + offset);
        if (strcmp(buffer + offset, name) == 0) {
            return 1;
        }
        offset += entryLength + 1;
    }
    return 0;
}

static int test_metadata_roundtrip(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsBuilderConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    struct VaFsDirectoryHandle* meta = NULL;
    struct VaFsDirectoryHandle* reopened = NULL;
    struct VaFsFileHandle* file_handle = NULL;
    struct VaFsMetadata dirMetadata = metadata_for_mode(VaFsEntryType_Directory, 0711);
    struct VaFsMetadata fileMetadata = metadata_for_mode(VaFsEntryType_File, 0640);
    struct VaFsMetadata symlinkMetadata = metadata_for_mode(VaFsEntryType_Symlink, 0700);
    struct VaFsMetadata statbuf;
    struct VaFsEntry entry;
    const char* payload = "metadata-roundtrip";
    int saw_payload = 0;
    int saw_alias = 0;
    int status;

    dirMetadata.Uid = 11;
    dirMetadata.Gid = 12;
    dirMetadata.ObjectId = 0x1001;
    dirMetadata.MTime.Seconds = 1715980800;
    dirMetadata.MTime.Nanoseconds = 1234;
    dirMetadata.Mask |= VaFsMetadataMask_Uid |
        VaFsMetadataMask_Gid |
        VaFsMetadataMask_ObjectId |
        VaFsMetadataMask_MTime;

    fileMetadata.Uid = 21;
    fileMetadata.Gid = 22;
    fileMetadata.ObjectId = 0x1020304050607080ULL;
    fileMetadata.MTime.Seconds = 1715980900;
    fileMetadata.MTime.Nanoseconds = 5678;
    fileMetadata.ATime.Seconds = 1715980910;
    fileMetadata.ATime.Nanoseconds = 111;
    fileMetadata.CTime.Seconds = 1715980920;
    fileMetadata.CTime.Nanoseconds = 222;
    fileMetadata.BirthTime.Seconds = 1715980930;
    fileMetadata.BirthTime.Nanoseconds = 333;
    fileMetadata.WindowsAttributes = 0x20;
    fileMetadata.Mask |= VaFsMetadataMask_Uid |
        VaFsMetadataMask_Gid |
        VaFsMetadataMask_ObjectId |
        VaFsMetadataMask_MTime |
        VaFsMetadataMask_ATime |
        VaFsMetadataMask_CTime |
        VaFsMetadataMask_BirthTime |
        VaFsMetadataMask_WindowsAttributes;

    symlinkMetadata.Uid = 31;
    symlinkMetadata.Gid = 32;
    symlinkMetadata.ObjectId = 0x2002;
    symlinkMetadata.MTime.Seconds = 1715980940;
    symlinkMetadata.MTime.Nanoseconds = 444;
    symlinkMetadata.Mask |= VaFsMetadataMask_Uid |
        VaFsMetadataMask_Gid |
        VaFsMetadataMask_ObjectId |
        VaFsMetadataMask_MTime;

    vafs_builder_config_initialize(&config);
    status = vafs_builder_new(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create metadata roundtrip image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory for metadata roundtrip");

    status = vafs_directory_create_directory(root, "meta", &dirMetadata, &meta);
    TEST_ASSERT(status == 0, "Failed to create metadata test directory");

    status = vafs_directory_create_file(meta, "payload", &fileMetadata, &file_handle);
    TEST_ASSERT(status == 0, "Failed to create metadata test file");

    status = vafs_file_write(file_handle, (void*)payload, strlen(payload));
    TEST_ASSERT(status == 0, "Failed to write metadata test file payload");
    vafs_file_close(file_handle);
    file_handle = NULL;

    status = vafs_directory_create_symlink(meta, "alias", "/meta/payload", &symlinkMetadata);
    TEST_ASSERT(status == 0, "Failed to create metadata test symlink");

    vafs_directory_close(meta);
    meta = NULL;
    vafs_directory_close(root);
    root = NULL;
    vafs_builder_close(vafs);
    vafs = NULL;

    status = vafs_reader_open_file(TEST_IMAGE_PATH, NULL, &vafs);
    TEST_ASSERT(status == 0, "Failed to reopen metadata test image");

    status = vafs_path_stat(vafs, "/meta", 1, &statbuf);
    TEST_ASSERT(status == 0, "Failed to stat metadata test directory");
    TEST_ASSERT(statbuf.Mode == dirMetadata.Mode, "Directory mode did not round-trip");
    TEST_ASSERT(statbuf.Uid == dirMetadata.Uid, "Directory uid did not round-trip");
    TEST_ASSERT(statbuf.Gid == dirMetadata.Gid, "Directory gid did not round-trip");
    TEST_ASSERT(statbuf.ObjectId == dirMetadata.ObjectId, "Directory object id did not round-trip");
    TEST_ASSERT(timestamps_equal(&statbuf.MTime, &dirMetadata.MTime), "Directory mtime did not round-trip");

    status = vafs_path_stat(vafs, "/meta/payload", 1, &statbuf);
    TEST_ASSERT(status == 0, "Failed to stat metadata test file");
    TEST_ASSERT(statbuf.Mode == fileMetadata.Mode, "File mode did not round-trip");
    TEST_ASSERT(statbuf.Uid == fileMetadata.Uid, "File uid did not round-trip");
    TEST_ASSERT(statbuf.Gid == fileMetadata.Gid, "File gid did not round-trip");
    TEST_ASSERT(statbuf.ObjectId == fileMetadata.ObjectId, "File object id did not round-trip");
    TEST_ASSERT(statbuf.Size == strlen(payload), "File size did not round-trip");
    TEST_ASSERT(statbuf.WindowsAttributes == fileMetadata.WindowsAttributes, "File Windows attributes did not round-trip");
    TEST_ASSERT(timestamps_equal(&statbuf.MTime, &fileMetadata.MTime), "File mtime did not round-trip");
    TEST_ASSERT(timestamps_equal(&statbuf.ATime, &fileMetadata.ATime), "File atime did not round-trip");
    TEST_ASSERT(timestamps_equal(&statbuf.CTime, &fileMetadata.CTime), "File ctime did not round-trip");
    TEST_ASSERT(timestamps_equal(&statbuf.BirthTime, &fileMetadata.BirthTime), "File birthtime did not round-trip");

    status = vafs_path_stat(vafs, "/meta/alias", 0, &statbuf);
    TEST_ASSERT(status == 0, "Failed to stat metadata test symlink");
    TEST_ASSERT(statbuf.Mode == symlinkMetadata.Mode, "Symlink mode did not round-trip");
    TEST_ASSERT(statbuf.Uid == symlinkMetadata.Uid, "Symlink uid did not round-trip");
    TEST_ASSERT(statbuf.Gid == symlinkMetadata.Gid, "Symlink gid did not round-trip");
    TEST_ASSERT(statbuf.ObjectId == symlinkMetadata.ObjectId, "Symlink object id did not round-trip");
    TEST_ASSERT(statbuf.Size == strlen("/meta/payload"), "Symlink size did not round-trip");
    TEST_ASSERT(timestamps_equal(&statbuf.MTime, &symlinkMetadata.MTime), "Symlink mtime did not round-trip");

    status = vafs_directory_open(vafs, "/meta", &reopened);
    TEST_ASSERT(status == 0, "Failed to reopen metadata directory for enumeration");

    while (vafs_directory_read(reopened, &entry) == 0) {
        if (strcmp(entry.Name, "payload") == 0) {
            TEST_ASSERT(entry.ObjectId == fileMetadata.ObjectId, "Enumerated file object id did not round-trip");
            TEST_ASSERT((entry.MetadataMask & VaFsMetadataMask_MTime) != 0, "Enumerated file metadata mask lost mtime");
            saw_payload = 1;
        } else if (strcmp(entry.Name, "alias") == 0) {
            TEST_ASSERT(entry.ObjectId == symlinkMetadata.ObjectId, "Enumerated symlink object id did not round-trip");
            TEST_ASSERT((entry.MetadataMask & VaFsMetadataMask_Uid) != 0, "Enumerated symlink metadata mask lost uid");
            saw_alias = 1;
        }
    }

    TEST_ASSERT(saw_payload, "Enumerated directory did not include payload entry");
    TEST_ASSERT(saw_alias, "Enumerated directory did not include alias entry");

    vafs_directory_close(reopened);
    reopened = NULL;
    vafs_reader_close(vafs);
    remove(TEST_IMAGE_PATH);

    TEST_PASS("Metadata survives descriptor-stream round-trip");
}

static int test_special_roundtrip(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsBuilderConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    struct VaFsDirectoryHandle* specials = NULL;
    struct VaFsDirectoryHandle* reopened = NULL;
    struct VaFsMetadata dirMetadata = metadata_for_mode(VaFsEntryType_Directory, 0755);
    struct VaFsMetadata invalidMetadata = metadata_for_mode(VaFsEntryType_File, 0644);
    struct VaFsMetadata charMetadata = metadata_for_mode(VaFsEntryType_CharacterDevice, 0600);
    struct VaFsMetadata blockMetadata = metadata_for_mode(VaFsEntryType_BlockDevice, 0640);
    struct VaFsMetadata fifoMetadata = metadata_for_mode(VaFsEntryType_Fifo, 0660);
    struct VaFsMetadata statbuf;
    struct VaFsEntry entry;
    int saw_null = 0;
    int saw_loop = 0;
    int saw_pipe = 0;
    int status;

    charMetadata.Uid = 41;
    charMetadata.ObjectId = 0x4100;
    charMetadata.Device.Major = 1;
    charMetadata.Device.Minor = 3;
    charMetadata.MTime.Seconds = 1715981000;
    charMetadata.MTime.Nanoseconds = 10;
    charMetadata.Mask |= VaFsMetadataMask_Uid |
        VaFsMetadataMask_ObjectId |
        VaFsMetadataMask_Device |
        VaFsMetadataMask_MTime;

    blockMetadata.Gid = 51;
    blockMetadata.ObjectId = 0x5100;
    blockMetadata.Device.Major = 8;
    blockMetadata.Device.Minor = 1;
    blockMetadata.MTime.Seconds = 1715981010;
    blockMetadata.MTime.Nanoseconds = 20;
    blockMetadata.Mask |= VaFsMetadataMask_Gid |
        VaFsMetadataMask_ObjectId |
        VaFsMetadataMask_Device |
        VaFsMetadataMask_MTime;

    fifoMetadata.ObjectId = 0x6100;
    fifoMetadata.MTime.Seconds = 1715981020;
    fifoMetadata.MTime.Nanoseconds = 30;
    fifoMetadata.Mask |= VaFsMetadataMask_ObjectId |
        VaFsMetadataMask_MTime;

    vafs_builder_config_initialize(&config);
    status = vafs_builder_new(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create special-file test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory for special-file test");

    status = vafs_directory_create_directory(root, "specials", &dirMetadata, &specials);
    TEST_ASSERT(status == 0, "Failed to create special-file test directory");

    status = vafs_directory_create_special(specials, "invalid", &invalidMetadata);
    TEST_ASSERT(status != 0 && errno == EINVAL, "Regular file metadata should be rejected for special entries");

    status = vafs_directory_create_special(specials, "null", &charMetadata);
    TEST_ASSERT(status == 0, "Failed to create character device entry");

    status = vafs_directory_create_special(specials, "loop", &blockMetadata);
    TEST_ASSERT(status == 0, "Failed to create block device entry");

    status = vafs_directory_create_special(specials, "pipe", &fifoMetadata);
    TEST_ASSERT(status == 0, "Failed to create fifo entry");

    vafs_directory_close(specials);
    specials = NULL;
    vafs_directory_close(root);
    root = NULL;
    vafs_builder_close(vafs);
    vafs = NULL;

    status = vafs_reader_open_file(TEST_IMAGE_PATH, NULL, &vafs);
    TEST_ASSERT(status == 0, "Failed to reopen special-file test image");

    status = vafs_path_stat(vafs, "/specials/null", 1, &statbuf);
    TEST_ASSERT(status == 0, "Failed to stat character device entry");
    TEST_ASSERT(statbuf.Type == VaFsEntryType_CharacterDevice, "Character device type did not round-trip");
    TEST_ASSERT(statbuf.Mode == charMetadata.Mode, "Character device mode did not round-trip");
    TEST_ASSERT(statbuf.Device.Major == charMetadata.Device.Major, "Character device major did not round-trip");
    TEST_ASSERT(statbuf.Device.Minor == charMetadata.Device.Minor, "Character device minor did not round-trip");
    TEST_ASSERT(statbuf.ObjectId == charMetadata.ObjectId, "Character device object id did not round-trip");
    TEST_ASSERT(timestamps_equal(&statbuf.MTime, &charMetadata.MTime), "Character device mtime did not round-trip");

    status = vafs_path_stat(vafs, "/specials/loop", 1, &statbuf);
    TEST_ASSERT(status == 0, "Failed to stat block device entry");
    TEST_ASSERT(statbuf.Type == VaFsEntryType_BlockDevice, "Block device type did not round-trip");
    TEST_ASSERT(statbuf.Mode == blockMetadata.Mode, "Block device mode did not round-trip");
    TEST_ASSERT(statbuf.Device.Major == blockMetadata.Device.Major, "Block device major did not round-trip");
    TEST_ASSERT(statbuf.Device.Minor == blockMetadata.Device.Minor, "Block device minor did not round-trip");
    TEST_ASSERT(statbuf.ObjectId == blockMetadata.ObjectId, "Block device object id did not round-trip");
    TEST_ASSERT(timestamps_equal(&statbuf.MTime, &blockMetadata.MTime), "Block device mtime did not round-trip");

    status = vafs_path_stat(vafs, "/specials/pipe", 1, &statbuf);
    TEST_ASSERT(status == 0, "Failed to stat fifo entry");
    TEST_ASSERT(statbuf.Type == VaFsEntryType_Fifo, "Fifo type did not round-trip");
    TEST_ASSERT(statbuf.Mode == fifoMetadata.Mode, "Fifo mode did not round-trip");
    TEST_ASSERT(statbuf.ObjectId == fifoMetadata.ObjectId, "Fifo object id did not round-trip");
    TEST_ASSERT(timestamps_equal(&statbuf.MTime, &fifoMetadata.MTime), "Fifo mtime did not round-trip");

    status = vafs_directory_open(vafs, "/specials", &reopened);
    TEST_ASSERT(status == 0, "Failed to reopen special-file directory");

    while (vafs_directory_read(reopened, &entry) == 0) {
        if (strcmp(entry.Name, "null") == 0) {
            TEST_ASSERT(entry.Type == VaFsEntryType_CharacterDevice, "Enumerated character device type did not round-trip");
            TEST_ASSERT((entry.MetadataMask & VaFsMetadataMask_Device) != 0, "Enumerated character device lost device metadata");
            saw_null = 1;
        } else if (strcmp(entry.Name, "loop") == 0) {
            TEST_ASSERT(entry.Type == VaFsEntryType_BlockDevice, "Enumerated block device type did not round-trip");
            TEST_ASSERT((entry.MetadataMask & VaFsMetadataMask_Device) != 0, "Enumerated block device lost device metadata");
            saw_loop = 1;
        } else if (strcmp(entry.Name, "pipe") == 0) {
            TEST_ASSERT(entry.Type == VaFsEntryType_Fifo, "Enumerated fifo type did not round-trip");
            TEST_ASSERT((entry.MetadataMask & VaFsMetadataMask_Device) == 0, "Enumerated fifo should not advertise device metadata");
            saw_pipe = 1;
        }
    }

    TEST_ASSERT(saw_null, "Enumerated directory did not include character device entry");
    TEST_ASSERT(saw_loop, "Enumerated directory did not include block device entry");
    TEST_ASSERT(saw_pipe, "Enumerated directory did not include fifo entry");

    vafs_directory_close(reopened);
    reopened = NULL;
    vafs_reader_close(vafs);
    remove(TEST_IMAGE_PATH);

    TEST_PASS("Special files survive descriptor-stream round-trip");
}

static int test_hardlink_roundtrip(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsBuilderConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    struct VaFsDirectoryHandle* links = NULL;
    struct VaFsDirectoryHandle* reopened = NULL;
    struct VaFsFileHandle* file_handle = NULL;
    struct VaFsMetadata dirMetadata = metadata_for_mode(VaFsEntryType_Directory, 0755);
    struct VaFsMetadata fileMetadata = metadata_for_mode(VaFsEntryType_File, 0644);
    struct VaFsMetadata statbuf;
    struct VaFsEntry entry;
    const char* payload = "hardlink-roundtrip";
    char buffer[32];
    size_t read;
    int saw_payload = 0;
    int saw_alias = 0;
    int status;

    fileMetadata.ObjectId = 0x70010002ULL;
    fileMetadata.Mask |= VaFsMetadataMask_ObjectId;

    vafs_builder_config_initialize(&config);
    status = vafs_builder_new(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create hardlink test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory for hardlink test");

    status = vafs_directory_create_directory(root, "links", &dirMetadata, &links);
    TEST_ASSERT(status == 0, "Failed to create hardlink test directory");

    status = vafs_directory_create_file(links, "payload", &fileMetadata, &file_handle);
    TEST_ASSERT(status == 0, "Failed to create hardlink target file");

    status = vafs_file_write(file_handle, (void*)payload, strlen(payload));
    TEST_ASSERT(status == 0, "Failed to write hardlink test payload");
    vafs_file_close(file_handle);
    file_handle = NULL;

    status = vafs_directory_create_hardlink(links, "payload_alias", fileMetadata.ObjectId);
    TEST_ASSERT(status == 0, "Failed to create hardlink alias");

    vafs_directory_close(links);
    links = NULL;
    vafs_directory_close(root);
    root = NULL;
    vafs_builder_close(vafs);
    vafs = NULL;

    status = vafs_reader_open_file(TEST_IMAGE_PATH, NULL, &vafs);
    TEST_ASSERT(status == 0, "Failed to reopen hardlink test image");

    status = vafs_path_stat(vafs, "/links/payload", 1, &statbuf);
    TEST_ASSERT(status == 0, "Failed to stat hardlink target file");
    TEST_ASSERT(statbuf.Type == VaFsEntryType_File, "Hardlink target type should remain a file");
    TEST_ASSERT(statbuf.ObjectId == fileMetadata.ObjectId, "Hardlink target object id did not round-trip");
    TEST_ASSERT(statbuf.LinkCount == 2, "Hardlink target link count should be incremented");
    TEST_ASSERT(statbuf.Size == strlen(payload), "Hardlink target size did not round-trip");

    status = vafs_path_stat(vafs, "/links/payload_alias", 1, &statbuf);
    TEST_ASSERT(status == 0, "Failed to stat hardlink alias path");
    TEST_ASSERT(statbuf.Type == VaFsEntryType_File, "Hardlink alias path should resolve to file metadata");
    TEST_ASSERT(statbuf.ObjectId == fileMetadata.ObjectId, "Hardlink alias object id did not round-trip");
    TEST_ASSERT(statbuf.LinkCount == 2, "Hardlink alias link count should match shared target");
    TEST_ASSERT(statbuf.Size == strlen(payload), "Hardlink alias size did not round-trip");

    status = vafs_directory_open(vafs, "/links", &reopened);
    TEST_ASSERT(status == 0, "Failed to reopen hardlink directory");

    status = vafs_directory_open_file(reopened, "payload_alias", &file_handle);
    TEST_ASSERT(status == 0, "Failed to open hardlink alias through directory lookup");

    TEST_ASSERT(vafs_file_length(file_handle) == strlen(payload), "Hardlink alias length did not resolve to target file length");
    memset(buffer, 0, sizeof(buffer));
    read = vafs_file_read(file_handle, buffer, sizeof(buffer) - 1);
    TEST_ASSERT(read == strlen(payload), "Hardlink alias read returned unexpected length");
    TEST_ASSERT(strcmp(buffer, payload) == 0, "Hardlink alias read returned unexpected payload");
    vafs_file_close(file_handle);
    file_handle = NULL;

    while (vafs_directory_read(reopened, &entry) == 0) {
        if (strcmp(entry.Name, "payload") == 0) {
            TEST_ASSERT(entry.Type == VaFsEntryType_File, "Enumerated hardlink target should remain a file entry");
            TEST_ASSERT(entry.ObjectId == fileMetadata.ObjectId, "Enumerated hardlink target object id did not round-trip");
            saw_payload = 1;
        } else if (strcmp(entry.Name, "payload_alias") == 0) {
            TEST_ASSERT(entry.Type == VaFsEntryType_Hardlink, "Enumerated alias should report hardlink entry type");
            TEST_ASSERT(entry.ObjectId == fileMetadata.ObjectId, "Enumerated hardlink alias object id did not round-trip");
            saw_alias = 1;
        }
    }

    TEST_ASSERT(saw_payload, "Enumerated hardlink directory did not include target file");
    TEST_ASSERT(saw_alias, "Enumerated hardlink directory did not include alias entry");

    vafs_directory_close(reopened);
    reopened = NULL;
    vafs_reader_close(vafs);
    remove(TEST_IMAGE_PATH);

    TEST_PASS("Hardlinks resolve shared metadata while preserving alias entry type");
}

static int test_xattr_roundtrip(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsBuilderConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    struct VaFsDirectoryHandle* meta = NULL;
    struct VaFsFileHandle* file_handle = NULL;
    struct VaFsMetadata dirMetadata = metadata_for_mode(VaFsEntryType_Directory, 0755);
    struct VaFsMetadata fileMetadata = metadata_for_mode(VaFsEntryType_File, 0644);
    struct VaFsMetadata peerMetadata = metadata_for_mode(VaFsEntryType_File, 0644);
    struct VaFsMetadata statbuf;
    struct VaFsFeatureHeader* feature;
    struct VaFsGuid xattrGuid = VA_FS_FEATURE_XATTRS;
    char xattrList[128];
    char valueBuffer[32];
    size_t bytesWritten = 0;
    int status;

    fileMetadata.ObjectId = 0x73000011ULL;
    fileMetadata.Mask |= VaFsMetadataMask_ObjectId;
    peerMetadata.ObjectId = 0x73000012ULL;
    peerMetadata.Mask |= VaFsMetadataMask_ObjectId;

    vafs_builder_config_initialize(&config);
    status = vafs_builder_new(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create xattr test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory for xattr test");

    status = vafs_directory_create_directory(root, "meta", &dirMetadata, &meta);
    TEST_ASSERT(status == 0, "Failed to create xattr test directory");

    status = vafs_directory_create_file(meta, "xattr_target", &fileMetadata, &file_handle);
    TEST_ASSERT(status == 0, "Failed to create xattr target file");
    status = vafs_file_write(file_handle, (void*)"payload", strlen("payload"));
    TEST_ASSERT(status == 0, "Failed to write xattr target payload");
    vafs_file_close(file_handle);
    file_handle = NULL;

    status = vafs_directory_create_file(meta, "xattr_peer", &peerMetadata, &file_handle);
    TEST_ASSERT(status == 0, "Failed to create xattr peer file");
    vafs_file_close(file_handle);
    file_handle = NULL;

    status = vafs_directory_create_hardlink(meta, "xattr_alias", fileMetadata.ObjectId);
    TEST_ASSERT(status == 0, "Failed to create xattr hardlink alias");

    status = vafs_path_setxattr(vafs, "/meta", "user.kind", "meta", strlen("meta"));
    TEST_ASSERT(status == 0, "Failed to set directory xattr");

    status = vafs_path_setxattr(vafs, "/meta/xattr_target", "user.mime", "text/plain", strlen("text/plain"));
    TEST_ASSERT(status == 0, "Failed to set file mime xattr");
    status = vafs_path_setxattr(vafs, "/meta/xattr_target", "user.empty", NULL, 0);
    TEST_ASSERT(status == 0, "Failed to set empty file xattr");

    status = vafs_path_setxattr(vafs, "/meta/xattr_peer", "user.mime", "text/plain", strlen("text/plain"));
    TEST_ASSERT(status == 0, "Failed to set peer mime xattr");
    status = vafs_path_setxattr(vafs, "/meta/xattr_peer", "user.empty", NULL, 0);
    TEST_ASSERT(status == 0, "Failed to set peer empty xattr");

    vafs_directory_close(meta);
    meta = NULL;
    vafs_directory_close(root);
    root = NULL;
    vafs_builder_close(vafs);
    vafs = NULL;

    status = vafs_reader_open_file(TEST_IMAGE_PATH, NULL, &vafs);
    TEST_ASSERT(status == 0, "Failed to reopen xattr test image");

    status = vafs_reader_query_feature(vafs, &xattrGuid, &feature);
    TEST_ASSERT(status == 0, "Failed to query xattr feature");
    TEST_ASSERT(((VaFsFeatureXattrs_t*)feature)->Count == 2, "Expected deduplicated xattr set count");

    status = vafs_path_stat(vafs, "/meta", 1, &statbuf);
    TEST_ASSERT(status == 0, "Failed to stat xattr directory");
    TEST_ASSERT(statbuf.XattrCount == 1, "Directory xattr count did not round-trip");

    status = vafs_path_stat(vafs, "/meta/xattr_target", 1, &statbuf);
    TEST_ASSERT(status == 0, "Failed to stat xattr target");
    TEST_ASSERT(statbuf.XattrCount == 2, "Target xattr count did not round-trip");

    status = vafs_path_stat(vafs, "/meta/xattr_alias", 1, &statbuf);
    TEST_ASSERT(status == 0, "Failed to stat xattr alias");
    TEST_ASSERT(statbuf.XattrCount == 2, "Hardlink alias should expose target xattr count");

    status = vafs_path_listxattr(vafs, "/meta/xattr_target", NULL, 0, &bytesWritten);
    TEST_ASSERT(status == 0, "Failed to query xattr list size");
    TEST_ASSERT(bytesWritten != 0, "Expected non-empty xattr list");

    memset(xattrList, 0, sizeof(xattrList));
    status = vafs_path_listxattr(vafs, "/meta/xattr_target", xattrList, sizeof(xattrList), &bytesWritten);
    TEST_ASSERT(status == 0, "Failed to list xattrs for target");
    TEST_ASSERT(xattr_list_contains(xattrList, bytesWritten, "user.mime"), "Target xattr list missing user.mime");
    TEST_ASSERT(xattr_list_contains(xattrList, bytesWritten, "user.empty"), "Target xattr list missing user.empty");

    status = vafs_path_getxattr(vafs, "/meta/xattr_target", "user.mime", NULL, 0, &bytesWritten);
    TEST_ASSERT(status == 0, "Failed to query mime xattr size");
    TEST_ASSERT(bytesWritten == strlen("text/plain"), "Mime xattr size did not round-trip");

    memset(valueBuffer, 0, sizeof(valueBuffer));
    status = vafs_path_getxattr(vafs, "/meta/xattr_alias", "user.mime", valueBuffer, sizeof(valueBuffer), &bytesWritten);
    TEST_ASSERT(status == 0, "Failed to get mime xattr through hardlink alias");
    TEST_ASSERT(bytesWritten == strlen("text/plain"), "Alias mime xattr size did not round-trip");
    TEST_ASSERT(strcmp(valueBuffer, "text/plain") == 0, "Alias mime xattr value did not round-trip");

    status = vafs_path_getxattr(vafs, "/meta/xattr_target", "user.empty", NULL, 0, &bytesWritten);
    TEST_ASSERT(status == 0, "Failed to query empty xattr size");
    TEST_ASSERT(bytesWritten == 0, "Empty xattr size should round-trip as zero");

    status = vafs_path_getxattr(vafs, "/meta", "user.kind", valueBuffer, sizeof(valueBuffer), &bytesWritten);
    TEST_ASSERT(status == 0, "Failed to get directory xattr");
    TEST_ASSERT(bytesWritten == strlen("meta"), "Directory xattr size did not round-trip");
    TEST_ASSERT(memcmp(valueBuffer, "meta", bytesWritten) == 0, "Directory xattr value did not round-trip");

    vafs_reader_close(vafs);
    remove(TEST_IMAGE_PATH);

    TEST_PASS("Xattrs survive descriptor-stream sidecar round-trip");
}

static int test_root_xattr_roundtrip(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsBuilderConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    struct VaFsFileHandle* file_handle = NULL;
    struct VaFsMetadata fileMetadata = metadata_for_mode(VaFsEntryType_File, 0644);
    struct VaFsMetadata statbuf;
    struct VaFsFeatureHeader* feature;
    struct VaFsGuid xattrGuid = VA_FS_FEATURE_XATTRS;
    char xattrList[64];
    char valueBuffer[32];
    size_t bytesWritten = 0;
    int status;

    vafs_builder_config_initialize(&config);
    status = vafs_builder_new(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create root xattr test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory for root xattr test");

    status = vafs_directory_create_file(root, "child", &fileMetadata, &file_handle);
    TEST_ASSERT(status == 0, "Failed to create root child file");
    status = vafs_file_write(file_handle, (void*)"root-child", strlen("root-child"));
    TEST_ASSERT(status == 0, "Failed to write root child payload");
    vafs_file_close(file_handle);
    file_handle = NULL;

    status = vafs_path_setxattr(vafs, "/", "user.root", "init", strlen("init"));
    TEST_ASSERT(status == 0, "Failed to set root xattr");

    status = vafs_path_stat(vafs, "/", 1, &statbuf);
    TEST_ASSERT(status == 0, "Failed to stat root while writing");
    TEST_ASSERT(statbuf.XattrCount == 1, "Root xattr count should update immediately in write mode");

    vafs_directory_close(root);
    root = NULL;
    vafs_builder_close(vafs);
    vafs = NULL;

    status = vafs_reader_open_file(TEST_IMAGE_PATH, NULL, &vafs);
    TEST_ASSERT(status == 0, "Failed to reopen root xattr test image");

    status = vafs_reader_query_feature(vafs, &xattrGuid, &feature);
    TEST_ASSERT(status == 0, "Failed to query root xattr feature");
    TEST_ASSERT(((VaFsFeatureXattrs_t*)feature)->Count == 1, "Expected one root xattr set in feature table");

    status = vafs_path_stat(vafs, "/", 1, &statbuf);
    TEST_ASSERT(status == 0, "Failed to stat root after reopen");
    TEST_ASSERT(statbuf.XattrCount == 1, "Root xattr count did not round-trip");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to reopen root directory handle");
    status = vafs_directory_stat(root, &statbuf);
    TEST_ASSERT(status == 0, "Failed to stat root through directory handle");
    TEST_ASSERT(statbuf.XattrCount == 1, "Root handle stat should expose persisted xattr count");

    status = vafs_path_listxattr(vafs, "/", NULL, 0, &bytesWritten);
    TEST_ASSERT(status == 0, "Failed to query root xattr list size");
    TEST_ASSERT(bytesWritten != 0, "Expected non-empty root xattr list");

    memset(xattrList, 0, sizeof(xattrList));
    status = vafs_path_listxattr(vafs, "/", xattrList, sizeof(xattrList), &bytesWritten);
    TEST_ASSERT(status == 0, "Failed to list root xattrs");
    TEST_ASSERT(xattr_list_contains(xattrList, bytesWritten, "user.root"), "Root xattr list missing user.root");

    memset(valueBuffer, 0, sizeof(valueBuffer));
    status = vafs_path_getxattr(vafs, "/", "user.root", valueBuffer, sizeof(valueBuffer), &bytesWritten);
    TEST_ASSERT(status == 0, "Failed to get root xattr");
    TEST_ASSERT(bytesWritten == strlen("init"), "Root xattr size did not round-trip");
    TEST_ASSERT(memcmp(valueBuffer, "init", bytesWritten) == 0, "Root xattr value did not round-trip");

    vafs_directory_close(root);
    root = NULL;
    vafs_reader_close(vafs);
    remove(TEST_IMAGE_PATH);

    TEST_PASS("Root xattrs survive a real persisted root descriptor");
}

static int test_symlink_xattr_nofollow_roundtrip(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsBuilderConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    struct VaFsDirectoryHandle* meta = NULL;
    struct VaFsFileHandle* file_handle = NULL;
    struct VaFsMetadata dirMetadata = metadata_for_mode(VaFsEntryType_Directory, 0755);
    struct VaFsMetadata fileMetadata = metadata_for_mode(VaFsEntryType_File, 0644);
    struct VaFsMetadata symlinkMetadata = metadata_for_mode(VaFsEntryType_Symlink, 0777);
    struct VaFsMetadata statbuf;
    char xattrList[64];
    char valueBuffer[32];
    size_t bytesWritten = 0;
    int status;

    vafs_builder_config_initialize(&config);
    status = vafs_builder_new(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create symlink xattr test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory for symlink xattr test");

    status = vafs_directory_create_directory(root, "meta", &dirMetadata, &meta);
    TEST_ASSERT(status == 0, "Failed to create symlink xattr test directory");

    status = vafs_directory_create_file(meta, "target", &fileMetadata, &file_handle);
    TEST_ASSERT(status == 0, "Failed to create symlink xattr target file");
    status = vafs_file_write(file_handle, (void*)"payload", strlen("payload"));
    TEST_ASSERT(status == 0, "Failed to write symlink xattr target payload");
    vafs_file_close(file_handle);
    file_handle = NULL;

    status = vafs_directory_create_symlink(meta, "link", "/meta/target", &symlinkMetadata);
    TEST_ASSERT(status == 0, "Failed to create symlink xattr test link");

    status = vafs_path_setxattr(vafs, "/meta/target", "user.target", "file", strlen("file"));
    TEST_ASSERT(status == 0, "Failed to set target xattr");

    status = __vafs_path_setxattr(vafs, "/meta/link", 0, "user.link", "symlink", strlen("symlink"));
    TEST_ASSERT(status == 0, "Failed to set symlink-object xattr");

    vafs_directory_close(meta);
    meta = NULL;
    vafs_directory_close(root);
    root = NULL;
    vafs_builder_close(vafs);
    vafs = NULL;

    status = vafs_reader_open_file(TEST_IMAGE_PATH, NULL, &vafs);
    TEST_ASSERT(status == 0, "Failed to reopen symlink xattr test image");

    status = vafs_path_stat(vafs, "/meta/link", 0, &statbuf);
    TEST_ASSERT(status == 0, "Failed to stat symlink path in nofollow mode");
    TEST_ASSERT(statbuf.Type == VaFsEntryType_Symlink, "Nofollow stat should preserve the symlink entry type");
    TEST_ASSERT(statbuf.XattrCount == 1, "Nofollow stat should expose symlink-object xattrs");

    memset(valueBuffer, 0, sizeof(valueBuffer));
    status = vafs_path_getxattr(vafs, "/meta/link", "user.target", valueBuffer, sizeof(valueBuffer), &bytesWritten);
    TEST_ASSERT(status == 0, "Failed to resolve target xattr through public symlink path");
    TEST_ASSERT(bytesWritten == strlen("file"), "Follow-mode symlink xattr size did not round-trip");
    TEST_ASSERT(memcmp(valueBuffer, "file", bytesWritten) == 0, "Follow-mode symlink xattr value did not round-trip");

    memset(xattrList, 0, sizeof(xattrList));
    status = __vafs_path_listxattr(vafs, "/meta/link", 0, xattrList, sizeof(xattrList), &bytesWritten);
    TEST_ASSERT(status == 0, "Failed to list symlink-object xattrs in nofollow mode");
    TEST_ASSERT(xattr_list_contains(xattrList, bytesWritten, "user.link"), "Nofollow xattr list missing symlink-object entry");
    TEST_ASSERT(!xattr_list_contains(xattrList, bytesWritten, "user.target"), "Nofollow xattr list should not include target xattrs");

    memset(valueBuffer, 0, sizeof(valueBuffer));
    status = __vafs_path_getxattr(vafs, "/meta/link", 0, "user.link", valueBuffer, sizeof(valueBuffer), &bytesWritten);
    TEST_ASSERT(status == 0, "Failed to read symlink-object xattr in nofollow mode");
    TEST_ASSERT(bytesWritten == strlen("symlink"), "Nofollow symlink xattr size did not round-trip");
    TEST_ASSERT(memcmp(valueBuffer, "symlink", bytesWritten) == 0, "Nofollow symlink xattr value did not round-trip");

    vafs_reader_close(vafs);
    remove(TEST_IMAGE_PATH);

    TEST_PASS("Symlink-object xattrs stay distinct from target xattrs in nofollow mode");
}

static int test_wide_directory_lookup(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsBuilderConfiguration config;
    struct VaFsDirectoryHandle* root = NULL;
    struct VaFsDirectoryHandle* small_dir = NULL;
    struct VaFsDirectoryHandle* large_dir = NULL;
    struct VaFsDirectoryHandle* nested = NULL;
    struct VaFsFileHandle* file_handle = NULL;
    struct VaFsDirectoryEntry* small_dir_entry = NULL;
    struct VaFsDirectoryEntry* large_dir_entry = NULL;
    struct VaFsMetadata statbuf;
    struct VaFsMetadata dirMetadata = metadata_for_mode(VaFsEntryType_Directory, 0755);
    struct VaFsMetadata fileMetadata = metadata_for_mode(VaFsEntryType_File, 0644);
    char name[32];
    char collision_names[VAFS_LOOKUP_CACHE_SET_ASSOCIATIVITY + 1][32];
    char path_buffer[128];
    struct VaFsEntry entry;
    size_t read_count = 0;
    int saw_small_first = 0;
    int saw_small_middle = 0;
    int saw_small_last = 0;
    int saw_large_first = 0;
    int saw_large_middle = 0;
    int saw_large_last = 0;
    int saw_nested = 0;
    int status;
    int i;

    vafs_builder_config_initialize(&config);
    status = vafs_builder_new(TEST_IMAGE_PATH, &config, &vafs);
    TEST_ASSERT(status == 0, "Failed to create test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to open root directory");

    status = vafs_directory_create_directory(root, "small_dir", &dirMetadata, &small_dir);
    TEST_ASSERT(status == 0, "Failed to create small directory");

    status = vafs_directory_create_directory(root, "large_dir", &dirMetadata, &large_dir);
    TEST_ASSERT(status == 0, "Failed to create large directory");

    // Insert names in reverse order so lookup correctness does not depend on insertion order.
    for (i = SMALL_DIR_ENTRY_COUNT - 1; i >= 0; i--) {
        make_prefixed_entry_name(name, sizeof(name), "small", i);
        status = vafs_directory_create_file(small_dir, name, &fileMetadata, &file_handle);
        TEST_ASSERT(status == 0, "Failed to create small-directory file entry");

        if (i == 0) {
            const char* content = "small-directory-data";
            status = vafs_file_write(file_handle, (void*)content, strlen(content));
            TEST_ASSERT(status == 0, "Failed to write small-directory file payload");
        }

        vafs_file_close(file_handle);
        file_handle = NULL;
    }

    for (i = LARGE_DIR_ENTRY_COUNT - 1; i >= 0; i--) {
        make_entry_name(name, sizeof(name), i);
        status = vafs_directory_create_file(large_dir, name, &fileMetadata, &file_handle);
        TEST_ASSERT(status == 0, "Failed to create file entry");

        if (i == 0) {
            // Keep one file non-empty so the test image produces a valid data stream on close.
            const char* content = "wide-directory-data";
            status = vafs_file_write(file_handle, (void*)content, strlen(content));
            TEST_ASSERT(status == 0, "Failed to write file payload");
        }

        vafs_file_close(file_handle);
        file_handle = NULL;
    }

    status = vafs_directory_create_directory(root, "nested_dir", &dirMetadata, &nested);
    TEST_ASSERT(status == 0, "Failed to create nested directory");

    status = vafs_directory_create_file(nested, "inner_file", &fileMetadata, &file_handle);
    TEST_ASSERT(status == 0, "Failed to create nested file");
    vafs_file_close(file_handle);
    file_handle = NULL;

    vafs_directory_close(large_dir);
    large_dir = NULL;
    vafs_directory_close(small_dir);
    small_dir = NULL;
    vafs_directory_close(nested);
    nested = NULL;
    vafs_directory_close(root);
    root = NULL;
    vafs_builder_close(vafs);
    vafs = NULL;

    status = vafs_reader_open_file(TEST_IMAGE_PATH, NULL, &vafs);
    TEST_ASSERT(status == 0, "Failed to reopen test image");

    status = vafs_directory_open(vafs, "/", &root);
    TEST_ASSERT(status == 0, "Failed to reopen root directory");

    while (vafs_directory_read(root, &entry) == 0) {
        if (strcmp(entry.Name, "small_dir") == 0 || strcmp(entry.Name, "large_dir") == 0 || strcmp(entry.Name, "nested_dir") == 0) {
            saw_nested = 1;
        }
        read_count++;
    }

    TEST_ASSERT(read_count == 3, "Unexpected root directory entry count");
    TEST_ASSERT(saw_nested, "Missing expected root directories during enumeration");

    status = vafs_directory_open(vafs, "/small_dir", &small_dir);
    TEST_ASSERT(status == 0, "Failed to reopen small directory");

    status = assert_repeated_path_stat(vafs, "/small_dir", S_IFDIR | 0755, 0, "Failed to stat small directory through path lookup");
    TEST_ASSERT(status == 0, "Repeated small-directory path stat failed");

    status = assert_repeated_path_stat(vafs, "/small_dir/small_0000", S_IFREG | 0644, strlen("small-directory-data"), "Failed to stat small-directory file through path lookup");
    TEST_ASSERT(status == 0, "Repeated small-directory file path stat failed");

    small_dir_entry = __vafs_directory_find_entry(vafs->RootDirectory, "small_dir");
    TEST_ASSERT(small_dir_entry != NULL, "Failed to resolve small directory for cache assertions");
    TEST_ASSERT(lookup_cache_has_state(vafs, vafs->RootDirectory, "small_dir", VaFsLookupCacheState_Hit), "Small directory lookup should be cached as a hit");
    TEST_ASSERT(lookup_cache_has_state(vafs, small_dir_entry->Directory, "small_0000", VaFsLookupCacheState_Hit), "Small directory file lookup should be cached as a hit");

    while (vafs_directory_read(small_dir, &entry) == 0) {
        if (strcmp(entry.Name, "small_0000") == 0) {
            saw_small_first = 1;
        } else if (strcmp(entry.Name, "small_0032") == 0) {
            saw_small_middle = 1;
        } else if (strcmp(entry.Name, "small_0063") == 0) {
            saw_small_last = 1;
        }
    }

    TEST_ASSERT(saw_small_first, "Missing first small-directory entry during enumeration");
    TEST_ASSERT(saw_small_middle, "Missing middle small-directory entry during enumeration");
    TEST_ASSERT(saw_small_last, "Missing last small-directory entry during enumeration");

    status = vafs_directory_open_file(small_dir, "small_0000", &file_handle);
    TEST_ASSERT(status == 0, "Failed to open first small-directory entry through lookup");
    vafs_file_close(file_handle);
    file_handle = NULL;

    status = vafs_directory_open_file(small_dir, "small_0032", &file_handle);
    TEST_ASSERT(status == 0, "Failed to open middle small-directory entry through lookup");
    vafs_file_close(file_handle);
    file_handle = NULL;

    status = vafs_directory_open_file(small_dir, "small_0063", &file_handle);
    TEST_ASSERT(status == 0, "Failed to open last small-directory entry through lookup");
    vafs_file_close(file_handle);
    file_handle = NULL;

    vafs_directory_close(small_dir);
    small_dir = NULL;

    status = vafs_directory_open(vafs, "/large_dir", &large_dir);
    TEST_ASSERT(status == 0, "Failed to reopen large directory");

    status = assert_repeated_path_stat(vafs, "/large_dir", S_IFDIR | 0755, 0, "Failed to stat large directory through path lookup");
    TEST_ASSERT(status == 0, "Repeated large-directory path stat failed");

    status = assert_repeated_path_stat(vafs, "/large_dir/entry_1023", S_IFREG | 0644, 0, "Failed to stat large-directory file through path lookup");
    TEST_ASSERT(status == 0, "Repeated large-directory file path stat failed");

    large_dir_entry = __vafs_directory_find_entry(vafs->RootDirectory, "large_dir");
    TEST_ASSERT(large_dir_entry != NULL, "Failed to resolve large directory for cache assertions");

    status = vafs_path_stat(vafs, "/large_dir/missing_entry", 1, &statbuf);
    TEST_ASSERT(status != 0 && errno == ENOENT, "Missing large-directory entry should return ENOENT");
    status = vafs_path_stat(vafs, "/large_dir/missing_entry", 1, &statbuf);
    TEST_ASSERT(status != 0 && errno == ENOENT, "Repeated missing large-directory entry should return ENOENT");
    TEST_ASSERT(lookup_cache_has_state(vafs, large_dir_entry->Directory, "missing_entry", VaFsLookupCacheState_Miss), "Missing entry should be cached as a miss");

    status = find_collision_names(large_dir_entry->Directory, collision_names);
    TEST_ASSERT(status == 0, "Failed to find colliding names for lookup-cache eviction test");

    for (i = 0; i < VAFS_LOOKUP_CACHE_SET_ASSOCIATIVITY; i++) {
        snprintf(path_buffer, sizeof(path_buffer), "/large_dir/%s", collision_names[i]);
        status = vafs_path_stat(vafs, path_buffer, 1, &statbuf);
        TEST_ASSERT(status == 0, "Failed to warm lookup-cache eviction test entry");
    }

    snprintf(path_buffer, sizeof(path_buffer), "/large_dir/%s", collision_names[0]);
    status = vafs_path_stat(vafs, path_buffer, 1, &statbuf);
    TEST_ASSERT(status == 0, "Failed to refresh most-recent lookup-cache entry");

    snprintf(path_buffer, sizeof(path_buffer), "/large_dir/%s", collision_names[VAFS_LOOKUP_CACHE_SET_ASSOCIATIVITY]);
    status = vafs_path_stat(vafs, path_buffer, 1, &statbuf);
    TEST_ASSERT(status == 0, "Failed to trigger lookup-cache eviction");

    TEST_ASSERT(lookup_cache_has_state(vafs, large_dir_entry->Directory, collision_names[0], VaFsLookupCacheState_Hit), "Most-recent lookup-cache entry should remain resident after eviction");
    TEST_ASSERT(!lookup_cache_has_name(vafs, large_dir_entry->Directory, collision_names[1]), "Least-recent lookup-cache entry should be evicted");
    TEST_ASSERT(lookup_cache_has_state(vafs, large_dir_entry->Directory, collision_names[VAFS_LOOKUP_CACHE_SET_ASSOCIATIVITY], VaFsLookupCacheState_Hit), "Newest lookup-cache entry should be resident after eviction");

    while (vafs_directory_read(large_dir, &entry) == 0) {
        if (strcmp(entry.Name, "entry_0000") == 0) {
            saw_large_first = 1;
        } else if (strcmp(entry.Name, "entry_0512") == 0) {
            saw_large_middle = 1;
        } else if (strcmp(entry.Name, "entry_1023") == 0) {
            saw_large_last = 1;
        }
    }

    TEST_ASSERT(saw_large_first, "Missing first large-directory entry during enumeration");
    TEST_ASSERT(saw_large_middle, "Missing middle large-directory entry during enumeration");
    TEST_ASSERT(saw_large_last, "Missing last large-directory entry during enumeration");

    status = vafs_directory_open_file(large_dir, "entry_0000", &file_handle);
    TEST_ASSERT(status == 0, "Failed to open first large-directory entry through lookup");
    vafs_file_close(file_handle);
    file_handle = NULL;

    status = vafs_directory_open_file(large_dir, "entry_0512", &file_handle);
    TEST_ASSERT(status == 0, "Failed to open middle large-directory entry through lookup");
    vafs_file_close(file_handle);
    file_handle = NULL;

    status = vafs_directory_open_file(large_dir, "entry_1023", &file_handle);
    TEST_ASSERT(status == 0, "Failed to open last large-directory entry through lookup");
    vafs_file_close(file_handle);
    file_handle = NULL;

    status = vafs_file_open(vafs, "/nested_dir/inner_file", &file_handle);
    TEST_ASSERT(status == 0, "Failed to open nested file through path lookup");
    vafs_file_close(file_handle);
    file_handle = NULL;

    status = assert_repeated_path_stat(vafs, "/nested_dir/inner_file", S_IFREG | 0644, 0, "Failed to stat nested file through path lookup");
    TEST_ASSERT(status == 0, "Repeated nested-file path stat failed");

    status = vafs_directory_open(vafs, "/nested_dir", &nested);
    TEST_ASSERT(status == 0, "Failed to open nested directory through path lookup");
    vafs_directory_close(nested);
    nested = NULL;

    status = vafs_directory_open_file(large_dir, "missing_entry", &file_handle);
    TEST_ASSERT(status != 0 && errno == ENOENT, "Missing entry should return ENOENT");

    vafs_directory_close(large_dir);
    large_dir = NULL;
    vafs_directory_close(root);
    vafs_reader_close(vafs);
    remove(TEST_IMAGE_PATH);

    TEST_PASS("Directory lookup handles both threshold fallback paths correctly");
}

int main(int argc, char** argv)
{
    printf("Running VaFS wide directory lookup tests...\n\n");

    test_metadata_roundtrip();
    test_special_roundtrip();
    test_hardlink_roundtrip();
    test_xattr_roundtrip();
    test_root_xattr_roundtrip();
    test_symlink_xattr_nofollow_roundtrip();
    test_wide_directory_lookup();

    printf("\n========================================\n");
    printf("Test Results:\n");
    printf("  Passed: %d\n", g_test_passed);
    printf("  Failed: %d\n", g_test_failed);
    printf("========================================\n");

    return g_test_failed > 0 ? 1 : 0;
}
