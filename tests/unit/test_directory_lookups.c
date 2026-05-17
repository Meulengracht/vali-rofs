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
#include <vafs/directory.h>
#include <vafs/file.h>
#include <vafs/stat.h>
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

static int test_wide_directory_lookup(void)
{
    struct VaFs* vafs = NULL;
    struct VaFsConfiguration config;
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

    vafs_config_initialize(&config);
    status = vafs_create(TEST_IMAGE_PATH, &config, &vafs);
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
    vafs_close(vafs);
    vafs = NULL;

    status = vafs_open_file(TEST_IMAGE_PATH, &vafs);
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
    vafs_close(vafs);
    remove(TEST_IMAGE_PATH);

    TEST_PASS("Directory lookup handles both threshold fallback paths correctly");
}

int main(int argc, char** argv)
{
    printf("Running VaFS wide directory lookup tests...\n\n");

    test_wide_directory_lookup();

    printf("\n========================================\n");
    printf("Test Results:\n");
    printf("  Passed: %d\n", g_test_passed);
    printf("  Failed: %d\n", g_test_failed);
    printf("========================================\n");

    return g_test_failed > 0 ? 1 : 0;
}
