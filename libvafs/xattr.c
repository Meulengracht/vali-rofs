/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Vali Initrd Filesystem
 * - Contains the implementation of the Vali Initrd Filesystem.
 *   This filesystem is used to store the initrd of the kernel.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

// Both reader and builder code is in this file, so we include
// both here.
#include <vafs/reader.h>
#include <vafs/builder.h>

#include "private.h"

static struct VaFsGuid g_xattrGuid = VA_FS_FEATURE_XATTRS;

static int __resolve_xattr_entry_internal(
    struct VaFs*                vafs,
    const char*                 path,
    struct VaFsDirectoryEntry** entryOut,
    int                         followLinks,
    int                         symlinkDepth);

static void __advance_block_position(
    VaFsBlockPosition_t* position,
    uint32_t             blockSize,
    uint32_t             bytes)
{
    // The cold xattr section is addressed in block-relative terms, so readers
    // need a cheap way to walk variable-length records without reopening the
    // question of where descriptor blocks begin in the image.
    uint64_t absoluteOffset = (uint64_t)position->Offset + bytes;

    position->Index += (vafsblock_t)(absoluteOffset / blockSize);
    position->Offset = (uint32_t)(absoluteOffset % blockSize);
}

void __vafs_xattr_set_destroy(
    struct VaFsXattrSet* set)
{
    struct VaFsXattr* entry;

    if (set == NULL) {
        return;
    }

    entry = set->Entries;
    while (entry != NULL) {
        struct VaFsXattr* next = entry->Link;

        free(entry->Name);
        free(entry->Value);
        free(entry);
        entry = next;
    }
    free(set);
}

void __vafs_xattr_store_destroy(
    struct VaFs* vafs)
{
    if (vafs == NULL) {
        return;
    }

    free(vafs->XattrStore.Positions);
    free(vafs->XattrStore.Sets);
    memset(&vafs->XattrStore, 0, sizeof(struct VaFsXattrStore));
}

// Xattr operations follow resolved objects instead of path spellings, so the
// helper layer normalizes the per-entry storage accessors behind one surface.
static struct VaFs* __entry_vafs(
    struct VaFsDirectoryEntry* entry)
{
    switch (entry->Type) {
        case VA_FS_DESCRIPTOR_TYPE_FILE:
            return entry->File->VaFs;
        case VA_FS_DESCRIPTOR_TYPE_DIRECTORY:
            return entry->Directory->VaFs;
        case VA_FS_DESCRIPTOR_TYPE_SYMLINK:
            return entry->Symlink->VaFs;
        case VA_FS_DESCRIPTOR_TYPE_SPECIAL:
            return entry->Special->VaFs;
        default:
            errno = EINVAL;
            return NULL;
    }
}

static const VaFsDescriptorMetadata_t* __entry_descriptor_metadata(
    struct VaFsDirectoryEntry* entry)
{
    switch (entry->Type) {
        case VA_FS_DESCRIPTOR_TYPE_FILE:
            return &entry->File->Descriptor.Metadata;
        case VA_FS_DESCRIPTOR_TYPE_DIRECTORY:
            return &entry->Directory->Descriptor.Metadata;
        case VA_FS_DESCRIPTOR_TYPE_SYMLINK:
            return &entry->Symlink->Descriptor.Metadata;
        case VA_FS_DESCRIPTOR_TYPE_SPECIAL:
            return &entry->Special->Descriptor.Metadata;
        default:
            errno = EINVAL;
            return NULL;
    }
}

static struct VaFsMetadata* __entry_stat_metadata(
    struct VaFsDirectoryEntry* entry)
{
    switch (entry->Type) {
        case VA_FS_DESCRIPTOR_TYPE_FILE:
            return &entry->File->Stat;
        case VA_FS_DESCRIPTOR_TYPE_DIRECTORY:
            return &entry->Directory->Stat;
        case VA_FS_DESCRIPTOR_TYPE_SYMLINK:
            return &entry->Symlink->Stat;
        case VA_FS_DESCRIPTOR_TYPE_SPECIAL:
            return &entry->Special->Stat;
        default:
            errno = EINVAL;
            return NULL;
    }
}

static struct VaFsXattrSet** __entry_xattr_slot(
    struct VaFsDirectoryEntry* entry)
{
    switch (entry->Type) {
        case VA_FS_DESCRIPTOR_TYPE_FILE:
            return &entry->File->Xattrs;
        case VA_FS_DESCRIPTOR_TYPE_DIRECTORY:
            return &entry->Directory->Xattrs;
        case VA_FS_DESCRIPTOR_TYPE_SYMLINK:
            return &entry->Symlink->Xattrs;
        case VA_FS_DESCRIPTOR_TYPE_SPECIAL:
            return &entry->Special->Xattrs;
        default:
            errno = EINVAL;
            return NULL;
    }
}

static int* __entry_xattr_loaded_slot(
    struct VaFsDirectoryEntry* entry)
{
    switch (entry->Type) {
        case VA_FS_DESCRIPTOR_TYPE_FILE:
            return &entry->File->XattrsLoaded;
        case VA_FS_DESCRIPTOR_TYPE_DIRECTORY:
            return &entry->Directory->XattrsLoaded;
        case VA_FS_DESCRIPTOR_TYPE_SYMLINK:
            return &entry->Symlink->XattrsLoaded;
        case VA_FS_DESCRIPTOR_TYPE_SPECIAL:
            return &entry->Special->XattrsLoaded;
        default:
            errno = EINVAL;
            return NULL;
    }
}

static int __resolve_xattr_entry(
    struct VaFs*                vafs,
    const char*                 path,
    int                         followLinks,
    struct VaFsDirectoryEntry*  rootEntry,
    struct VaFsDirectoryEntry** entryOut)
{
    if (vafs == NULL || path == NULL || rootEntry == NULL || entryOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (__vafs_ensure_root_open(vafs) != 0) {
        return -1;
    }

    if (__vafs_is_root_path(path)) {
        // Xattr helpers are entry-centric, so root gets a transient directory
        // entry wrapper instead of a separate root-only access path.
        memset(rootEntry, 0, sizeof(struct VaFsDirectoryEntry));
        rootEntry->Type = VA_FS_DESCRIPTOR_TYPE_DIRECTORY;
        rootEntry->Directory = vafs->RootDirectory;
        *entryOut = rootEntry;
        return 0;
    }
    return __resolve_xattr_entry_internal(vafs, path, entryOut, followLinks, 0);
}

static int __resolve_xattr_entry_internal(
    struct VaFs*            vafs,
    const char*             path,
    struct VaFsDirectoryEntry** entryOut,
    int                     followLinks,
    int                     symlinkDepth)
{
    struct VaFsDirectory*      currentDirectory;
    struct VaFsDirectoryEntry* entry;
    const char*                remainingPath = path;
    char                       token[VAFS_NAME_MAX + 1];

    if (vafs == NULL || path == NULL || entryOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (symlinkDepth > VAFS_SYMLINK_MAX_DEPTH) {
        errno = ELOOP;
        return -1;
    }

    // Callers handle the root path directly because the token walker below is
    // only responsible for traversing named descendants beneath that root.
    if (__vafs_is_root_path(path)) {
        errno = EINVAL;
        return -1;
    }

    // xattr resolution follows the same tree walk as regular file access, but
    // it has one extra rule: a symlink can be treated as the link object itself
    // when callers request non-following semantics. That distinction is why the
    // traversal here keeps the final "terminal object" decision explicit.
    currentDirectory = vafs->RootDirectory;
    do {
        const char* previousPath = remainingPath;
        int         charsConsumed = __vafs_pathtoken(remainingPath, token, sizeof(token));

        if (!charsConsumed) {
            break;
        }
        remainingPath += charsConsumed;

        entry = __vafs_directory_find_entry(currentDirectory, token);
        if (entry == NULL) {
            return -1;
        }

        // Hardlink aliases intentionally share one canonical xattr set so
        // lookups and later mutations cannot diverge by pathname.
        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_HARDLINK) {
            entry = __vafs_resolve_hardlink(vafs, entry);
            if (entry == NULL) {
                return -1;
            }
        }

        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_SYMLINK) {
            if (!followLinks) {
                if (remainingPath[0] != '\0') {
                    errno = ENOTDIR;
                    return -1;
                }

                // Tooling needs a non-following mode so host symlink xattrs can
                // bind to the link object itself instead of collapsing onto the target.
                *entryOut = entry;
                return 0;
            }

            char* pathBuffer = malloc(VAFS_PATH_MAX);
            int   written;
            int   status;

            if (!pathBuffer) {
                errno = ENOMEM;
                return -1;
            }

            // Path-based xattr operations follow symlinks so they behave like
            // the normal path stat/open helpers and target the resolved object.
            written = __vafs_resolve_symlink(
                pathBuffer,
                VAFS_PATH_MAX,
                path,
                previousPath - path,
                entry->Symlink->Target
            );
            if (written < 0) {
                free(pathBuffer);
                return -1;
            }

            status = __resolve_xattr_entry_internal(vafs, pathBuffer, entryOut, followLinks, symlinkDepth + 1);
            free(pathBuffer);
            return status;
        }

        if (remainingPath[0] == '\0') {
            *entryOut = entry;
            return 0;
        }

        // Any remaining path segment must descend through a real directory;
        // otherwise the caller is asking for metadata beneath a leaf object.
        if (entry->Type != VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
            errno = ENOTDIR;
            return -1;
        }

        currentDirectory = entry->Directory;
    } while (1);

    errno = ENOENT;
    return -1;
}

static struct VaFsXattr* __xattr_find(
    struct VaFsXattrSet* set,
    const char*          name)
{
    struct VaFsXattr* entry;

    if (set == NULL || name == NULL) {
        return NULL;
    }

    entry = set->Entries;
    while (entry != NULL) {
        if (strcmp(entry->Name, name) == 0) {
            return entry;
        }
        entry = entry->Link;
    }
    return NULL;
}

static int __xattr_name_value_equal(
    const struct VaFsXattr* entry,
    const char*             name,
    const void*             value,
    uint32_t                valueLength)
{
    if (strcmp(entry->Name, name) != 0 || entry->ValueLength != valueLength) {
        return 0;
    }

    if (valueLength == 0) {
        return 1;
    }
    return memcmp(entry->Value, value, valueLength) == 0;
}

static int __xattr_sets_equal(
    const struct VaFsXattrSet* left,
    const struct VaFsXattrSet* right)
{
    const struct VaFsXattr* entry;

    if (left == right) {
        return 1;
    }
    if (left == NULL || right == NULL || left->Count != right->Count) {
        return 0;
    }

    // Section deduplication is content-based so unrelated entries can share a
    // single cold record without introducing another writer-only intern table.
    entry = left->Entries;
    while (entry != NULL) {
        struct VaFsXattr* match = __xattr_find((struct VaFsXattrSet*)right, entry->Name);

        if (match == NULL ||
            !__xattr_name_value_equal(match, entry->Name, entry->Value, entry->ValueLength)) {
            return 0;
        }
        entry = entry->Link;
    }
    return 1;
}

static int __xattr_set_put(
    struct VaFsXattrSet* set,
    const char*          name,
    const void*          value,
    uint32_t             valueLength)
{
    struct VaFsXattr* entry;
    void*             valueCopy = NULL;

    if (set == NULL || name == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (valueLength != 0) {
        valueCopy = malloc(valueLength);
        if (valueCopy == NULL) {
            errno = ENOMEM;
            return -1;
        }
        memcpy(valueCopy, value, valueLength);
    }

    entry = __xattr_find(set, name);
    if (entry != NULL) {
        // Replacing in place preserves one value per name and keeps list order
        // stable, which makes round-tripped listxattr output deterministic.
        free(entry->Value);
        entry->Value = valueCopy;
        entry->ValueLength = valueLength;
        return 0;
    }

    entry = calloc(1, sizeof(struct VaFsXattr));
    if (entry == NULL) {
        free(valueCopy);
        errno = ENOMEM;
        return -1;
    }

    entry->Name = strdup(name);
    if (entry->Name == NULL) {
        free(valueCopy);
        free(entry);
        errno = ENOMEM;
        return -1;
    }

    entry->Value = valueCopy;
    entry->ValueLength = valueLength;
    if (set->Entries == NULL) {
        set->Entries = entry;
    } else {
        struct VaFsXattr* tail = set->Entries;
        while (tail->Link != NULL) {
            tail = tail->Link;
        }
        tail->Link = entry;
    }

    set->Count++;
    return 0;
}

static int __xattr_registry_add(
    struct VaFs*          vafs,
    struct VaFsXattrSet*  set)
{
    struct VaFsXattrSet** sets;
    uint32_t              index;

    // The cold section stores unique sets rather than per-entry copies so the
    // hot descriptors only need a compact index and repeated metadata stays shared.
    for (index = 0; index < vafs->XattrStore.Count; index++) {
        if (__xattr_sets_equal(vafs->XattrStore.Sets[index], set)) {
            set->Index = index;
            return 0;
        }
    }

    sets = realloc(
        vafs->XattrStore.Sets,
        sizeof(struct VaFsXattrSet*) * (vafs->XattrStore.Count + 1)
    );
    if (sets == NULL) {
        errno = ENOMEM;
        return -1;
    }

    vafs->XattrStore.Sets = sets;
    set->Index = vafs->XattrStore.Count;
    vafs->XattrStore.Sets[vafs->XattrStore.Count++] = set;
    return 0;
}

static int __collect_directory_xattrs(
    struct VaFsDirectory* directory,
    int                   includeDirectory)
{
    struct VaFsDirectoryEntry* entry;
    struct VaFs*               vafs;

    vafs = directory->VaFs;
    // Root now participates like any other directory so its xattr set can be
    // assigned a stable hot index before the standalone root descriptor is emitted.
    if (includeDirectory && directory->Xattrs != NULL && directory->Xattrs->Count != 0) {
        if (__xattr_registry_add(vafs, directory->Xattrs) != 0) {
            return -1;
        }
    }

    entry = __vafs_directory_entries(directory);
    while (entry != NULL) {
        switch (entry->Type) {
            case VA_FS_DESCRIPTOR_TYPE_FILE:
                if (entry->File->Xattrs != NULL && entry->File->Xattrs->Count != 0) {
                    if (__xattr_registry_add(vafs, entry->File->Xattrs) != 0) {
                        return -1;
                    }
                }
                break;
            case VA_FS_DESCRIPTOR_TYPE_DIRECTORY:
                if (__collect_directory_xattrs(entry->Directory, 1) != 0) {
                    return -1;
                }
                break;
            case VA_FS_DESCRIPTOR_TYPE_SYMLINK:
                if (entry->Symlink->Xattrs != NULL && entry->Symlink->Xattrs->Count != 0) {
                    if (__xattr_registry_add(vafs, entry->Symlink->Xattrs) != 0) {
                        return -1;
                    }
                }
                break;
            case VA_FS_DESCRIPTOR_TYPE_SPECIAL:
                if (entry->Special->Xattrs != NULL && entry->Special->Xattrs->Count != 0) {
                    if (__xattr_registry_add(vafs, entry->Special->Xattrs) != 0) {
                        return -1;
                    }
                }
                break;
            default:
                break;
        }
        entry = entry->Link;
    }
    return 0;
}

int __vafs_xattr_prepare_write(
    struct VaFs* vafs)
{
    if (vafs == NULL || vafs->Mode != VaFsMode_Write) {
        errno = EINVAL;
        return -1;
    }

    __vafs_xattr_store_destroy(vafs);

    // Xattr sets are assigned stable section-local indices up front so the hot
    // descriptors can point at them before the colder section is serialized.
    // That now includes the standalone root descriptor as well as named entries.
    return __collect_directory_xattrs(vafs->RootDirectory, 1);
}

static int __write_xattr_set(
    struct VaFs*               vafs,
    const struct VaFsXattrSet* set)
{
    VaFsXattrSetDescriptor_t setDescriptor;
    const struct VaFsXattr*  entry;
    int                      status;

    // Xattrs use a dedicated record format because directory readers only know
    // how to parse child descriptors; reusing that format here would make the
    // cold section look like a second child list.
    setDescriptor.Count = set->Count;
    status = vafs_stream_write(vafs->DescriptorStream, &setDescriptor, sizeof(VaFsXattrSetDescriptor_t));
    if (status != 0) {
        return status;
    }

    entry = set->Entries;
    while (entry != NULL) {
        VaFsXattrRecordDescriptor_t recordDescriptor;

        recordDescriptor.NameLength = (uint16_t)strlen(entry->Name);
        recordDescriptor.Reserved = 0;
        recordDescriptor.ValueLength = entry->ValueLength;

        status = vafs_stream_write(vafs->DescriptorStream, &recordDescriptor, sizeof(VaFsXattrRecordDescriptor_t));
        if (status != 0) {
            return status;
        }

        status = vafs_stream_write(vafs->DescriptorStream, entry->Name, recordDescriptor.NameLength);
        if (status != 0) {
            return status;
        }

        if (entry->ValueLength != 0) {
            status = vafs_stream_write(vafs->DescriptorStream, entry->Value, entry->ValueLength);
            if (status != 0) {
                return status;
            }
        }
        entry = entry->Link;
    }
    return 0;
}

int __vafs_xattr_write_section(
    struct VaFs* vafs)
{
    VaFsFeatureXattrs_t feature;
    vafsblock_t         block;
    uint32_t            offset;
    uint32_t            index;
    int                 status;

    if (vafs == NULL || vafs->Mode != VaFsMode_Write) {
        errno = EINVAL;
        return -1;
    }

    if (vafs->XattrStore.Count == 0) {
        return 0;
    }

    // Capture the cold-section anchor before writing anything so the feature
    // can later give readers one stable jump point into the descriptor tail.
    status = vafs_stream_position(vafs->DescriptorStream, &block, &offset);
    if (status != 0) {
        return status;
    }

    vafs->XattrStore.Present = 1;
    vafs->XattrStore.Start.Index = block;
    vafs->XattrStore.Start.Offset = offset;

    for (index = 0; index < vafs->XattrStore.Count; index++) {
        status = __write_xattr_set(vafs, vafs->XattrStore.Sets[index]);
        if (status != 0) {
            return status;
        }
    }

    // Hot descriptors carry only indices, so the feature is the single source
    // of truth that tells readers where the indexed cold records begin.
    memcpy(&feature.Header.Guid, &g_xattrGuid, sizeof(struct VaFsGuid));
    feature.Header.Length = sizeof(VaFsFeatureXattrs_t);
    feature.DescriptorIndex = vafs->XattrStore.Start.Index;
    feature.DescriptorOffset = vafs->XattrStore.Start.Offset;
    feature.Count = vafs->XattrStore.Count;
    return vafs_builder_add_feature(vafs, &feature.Header);
}

static int __ensure_xattr_feature(
    struct VaFs* vafs)
{
    struct VaFsFeatureHeader* feature;
    VaFsFeatureXattrs_t*      xattrFeature;
    int                       status;

    if (vafs->XattrStore.Present || vafs->XattrStore.Count != 0) {
        return 0;
    }

    status = vafs_reader_query_feature(vafs, &g_xattrGuid, &feature);
    if (status != 0) {
        // Images that predate the feature simply have no persisted xattrs.
        if (errno == ENOENT) {
            return 0;
        }
        return status;
    }

    if (feature->Length < sizeof(VaFsFeatureXattrs_t)) {
        errno = EINVAL;
        return -1;
    }

    xattrFeature = (VaFsFeatureXattrs_t*)feature;
    vafs->XattrStore.Present = 1;
    vafs->XattrStore.Start.Index = xattrFeature->DescriptorIndex;
    vafs->XattrStore.Start.Offset = xattrFeature->DescriptorOffset;
    vafs->XattrStore.Count = xattrFeature->Count;
    return 0;
}

static int __ensure_xattr_positions(
    struct VaFs* vafs)
{
    struct VaFsStreamReader* reader;
    VaFsBlockPosition_t      current;
    uint32_t                 blockSize;
    uint32_t                 index;
    int                      status;
    size_t                   read;

    status = __ensure_xattr_feature(vafs);
    if (status != 0) {
        return status;
    }

    if (!vafs->XattrStore.Present || vafs->XattrStore.Count == 0 || vafs->XattrStore.PositionsLoaded) {
        return 0;
    }

    // Position indexing is deferred until the first real xattr lookup so the
    // common open/stat path does not pay to walk a section it never touches.
    vafs->XattrStore.Positions = calloc(vafs->XattrStore.Count, sizeof(VaFsBlockPosition_t));
    if (vafs->XattrStore.Positions == NULL) {
        errno = ENOMEM;
        return -1;
    }

    blockSize = vafs_stream_block_size(vafs->DescriptorStream);
    if (blockSize == 0) {
        errno = EINVAL;
        return -1;
    }

    status = vafs_stream_reader_open(vafs->DescriptorStream, &reader);
    if (status != 0) {
        return status;
    }

    current = vafs->XattrStore.Start;
    for (index = 0; index < vafs->XattrStore.Count; index++) {
        VaFsXattrSetDescriptor_t setDescriptor;
        uint32_t                 entryIndex;

        vafs->XattrStore.Positions[index] = current;
        status = vafs_stream_reader_seek(reader, current.Index, current.Offset);
        if (status != 0) {
            vafs_stream_reader_close(reader);
            return status;
        }

        status = vafs_stream_reader_read(reader, &setDescriptor, sizeof(VaFsXattrSetDescriptor_t), &read);
        if (status != 0 || read != sizeof(VaFsXattrSetDescriptor_t)) {
            vafs_stream_reader_close(reader);
            errno = EINVAL;
            return -1;
        }

        __advance_block_position(&current, blockSize, sizeof(VaFsXattrSetDescriptor_t));
        for (entryIndex = 0; entryIndex < setDescriptor.Count; entryIndex++) {
            VaFsXattrRecordDescriptor_t recordDescriptor;

            status = vafs_stream_reader_seek(reader, current.Index, current.Offset);
            if (status != 0) {
                vafs_stream_reader_close(reader);
                return status;
            }

            status = vafs_stream_reader_read(reader, &recordDescriptor, sizeof(VaFsXattrRecordDescriptor_t), &read);
            if (status != 0 || read != sizeof(VaFsXattrRecordDescriptor_t)) {
                vafs_stream_reader_close(reader);
                errno = EINVAL;
                return -1;
            }

            __advance_block_position(
                &current,
                blockSize,
                (uint32_t)(sizeof(VaFsXattrRecordDescriptor_t) + recordDescriptor.NameLength + recordDescriptor.ValueLength)
            );
        }
    }

    vafs_stream_reader_close(reader);
    vafs->XattrStore.PositionsLoaded = 1;
    return 0;
}

static int __load_xattr_set(
    struct VaFs*          vafs,
    uint32_t              index,
    struct VaFsXattrSet** setOut)
{
    struct VaFsStreamReader* reader;
    struct VaFsXattrSet*     set;
    uint32_t                 entryIndex;
    int                      status;
    size_t                   read;

    if (index >= vafs->XattrStore.Count) {
        errno = EINVAL;
        return -1;
    }

    status = __ensure_xattr_positions(vafs);
    if (status != 0) {
        return status;
    }

    status = vafs_stream_reader_open(vafs->DescriptorStream, &reader);
    if (status != 0) {
        return status;
    }

    status = vafs_stream_reader_seek(
        reader,
        vafs->XattrStore.Positions[index].Index,
        vafs->XattrStore.Positions[index].Offset
    );
    if (status != 0) {
        vafs_stream_reader_close(reader);
        return status;
    }

    set = calloc(1, sizeof(struct VaFsXattrSet));
    if (set == NULL) {
        vafs_stream_reader_close(reader);
        errno = ENOMEM;
        return -1;
    }

    // The cold record owns the authoritative per-set count so malformed images
    // cannot trick the reader into trusting only the hot descriptor metadata.
    status = vafs_stream_reader_read(reader, &set->Count, sizeof(uint32_t), &read);
    if (status != 0 || read != sizeof(uint32_t)) {
        __vafs_xattr_set_destroy(set);
        vafs_stream_reader_close(reader);
        errno = EINVAL;
        return -1;
    }

    set->Index = index;
    for (entryIndex = 0; entryIndex < set->Count; entryIndex++) {
        VaFsXattrRecordDescriptor_t recordDescriptor;
        struct VaFsXattr*           entry;
        char*                       name;

        status = vafs_stream_reader_read(reader, &recordDescriptor, sizeof(VaFsXattrRecordDescriptor_t), &read);
        if (status != 0 || read != sizeof(VaFsXattrRecordDescriptor_t)) {
            __vafs_xattr_set_destroy(set);
            vafs_stream_reader_close(reader);
            errno = EINVAL;
            return -1;
        }

        // Empty names would collapse list/get semantics, and the writer never
        // emits them, so treat them as on-disk corruption instead of tolerating
        // a partially meaningful xattr record.
        if (recordDescriptor.NameLength == 0) {
            __vafs_xattr_set_destroy(set);
            vafs_stream_reader_close(reader);
            errno = EINVAL;
            return -1;
        }

        name = malloc((size_t)recordDescriptor.NameLength + 1);
        if (name == NULL) {
            __vafs_xattr_set_destroy(set);
            vafs_stream_reader_close(reader);
            errno = ENOMEM;
            return -1;
        }

        status = vafs_stream_reader_read(reader, name, recordDescriptor.NameLength, &read);
        if (status != 0 || read != recordDescriptor.NameLength) {
            free(name);
            __vafs_xattr_set_destroy(set);
            vafs_stream_reader_close(reader);
            errno = EINVAL;
            return -1;
        }
        name[recordDescriptor.NameLength] = '\0';

        entry = calloc(1, sizeof(struct VaFsXattr));
        if (entry == NULL) {
            free(name);
            __vafs_xattr_set_destroy(set);
            vafs_stream_reader_close(reader);
            errno = ENOMEM;
            return -1;
        }

        entry->Name = name;
        entry->ValueLength = recordDescriptor.ValueLength;
        if (recordDescriptor.ValueLength != 0) {
            entry->Value = malloc(recordDescriptor.ValueLength);
            if (entry->Value == NULL) {
                free(entry->Name);
                free(entry);
                __vafs_xattr_set_destroy(set);
                vafs_stream_reader_close(reader);
                errno = ENOMEM;
                return -1;
            }

            status = vafs_stream_reader_read(reader, entry->Value, recordDescriptor.ValueLength, &read);
            if (status != 0 || read != recordDescriptor.ValueLength) {
                free(entry->Value);
                free(entry->Name);
                free(entry);
                __vafs_xattr_set_destroy(set);
                vafs_stream_reader_close(reader);
                errno = EINVAL;
                return -1;
            }
        }

        if (set->Entries == NULL) {
            set->Entries = entry;
        } else {
            struct VaFsXattr* tail = set->Entries;
            while (tail->Link != NULL) {
                tail = tail->Link;
            }
            tail->Link = entry;
        }
    }

    vafs_stream_reader_close(reader);
    *setOut = set;
    return 0;
}

static int __ensure_entry_xattrs_loaded(
    struct VaFsDirectoryEntry* entry)
{
    struct VaFs*                 vafs;
    const VaFsDescriptorMetadata_t* descriptorMetadata;
    struct VaFsXattrSet**        xattrSlot;
    int*                         loadedSlot;
    int                          status;

    vafs = __entry_vafs(entry);
    if (vafs == NULL) {
        return -1;
    }

    xattrSlot = __entry_xattr_slot(entry);
    loadedSlot = __entry_xattr_loaded_slot(entry);
    if (xattrSlot == NULL || loadedSlot == NULL) {
        return -1;
    }

    if (*loadedSlot) {
        return 0;
    }

    if (vafs->Mode == VaFsMode_Write) {
        *loadedSlot = 1;
        return 0;
    }

    descriptorMetadata = __entry_descriptor_metadata(entry);
    if (descriptorMetadata == NULL) {
        return -1;
    }

    // Remember that this entry has been examined so xattr-free objects do not
    // keep rescanning the feature table on every list/get call.
    *loadedSlot = 1;
    if ((descriptorMetadata->Mask & VaFsMetadataMask_XattrCount) == 0 ||
        descriptorMetadata->XattrCount == 0) {
        return 0;
    }

    // A non-zero count without an index means the hot descriptor and cold
    // section disagree, which is safer to treat as corruption than to guess at.
    if (descriptorMetadata->XattrIndex == VA_FS_INVALID_XATTR_INDEX) {
        *loadedSlot = 0;
        errno = EINVAL;
        return -1;
    }

    status = __ensure_xattr_feature(vafs);
    if (status != 0) {
        *loadedSlot = 0;
        return status;
    }

    if (!vafs->XattrStore.Present || descriptorMetadata->XattrIndex >= vafs->XattrStore.Count) {
        *loadedSlot = 0;
        errno = EINVAL;
        return -1;
    }

    status = __load_xattr_set(vafs, descriptorMetadata->XattrIndex, xattrSlot);
    if (status != 0) {
        *loadedSlot = 0;
        return status;
    }

    // Cross-check the hot count against the cold payload so callers never see
    // silently truncated or over-reported xattr lists on malformed images.
    if ((*xattrSlot)->Count != descriptorMetadata->XattrCount) {
        __vafs_xattr_set_destroy(*xattrSlot);
        *xattrSlot = NULL;
        *loadedSlot = 0;
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static size_t __listxattr_size(
    const struct VaFsXattrSet* set)
{
    const struct VaFsXattr* entry;
    size_t                  size = 0;

    if (set == NULL) {
        return 0;
    }

    entry = set->Entries;
    while (entry != NULL) {
        size += strlen(entry->Name) + 1;
        entry = entry->Link;
    }
    return size;
}

int __vafs_path_listxattr(
    struct VaFs* vafs,
    const char*  path,
    int          followLinks,
    char*        buffer,
    size_t       bufferSize,
    size_t*      bytesWritten)
{
    struct VaFsDirectoryEntry  rootEntry;
    struct VaFsDirectoryEntry* entry;
    struct VaFsXattrSet*       set;
    size_t                     required;
    size_t                     written = 0;
    int                        status;

    if (vafs == NULL || path == NULL || bytesWritten == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = __resolve_xattr_entry(vafs, path, followLinks, &rootEntry, &entry);
    if (status != 0) {
        return status;
    }

    status = __ensure_entry_xattrs_loaded(entry);
    if (status != 0) {
        return status;
    }

    set = *__entry_xattr_slot(entry);
    required = __listxattr_size(set);
    *bytesWritten = required;

    // POSIX-style callers often probe for the required size first and only
    // allocate a buffer once they know the full null-separated list length.
    if (buffer == NULL) {
        return 0;
    }

    if (bufferSize < required) {
        errno = ERANGE;
        return -1;
    }

    if (set != NULL) {
        struct VaFsXattr* xattr = set->Entries;
        while (xattr != NULL) {
            size_t nameLength = strlen(xattr->Name) + 1;
            memcpy(buffer + written, xattr->Name, nameLength);
            written += nameLength;
            xattr = xattr->Link;
        }
    }
    return 0;
}

int vafs_path_listxattr(
    struct VaFs* vafs,
    const char*  path,
    char*        buffer,
    size_t       bufferSize,
    size_t*      bytesWritten)
{
    return __vafs_path_listxattr(vafs, path, 1, buffer, bufferSize, bytesWritten);
}

int __vafs_path_getxattr(
    struct VaFs* vafs,
    const char*  path,
    int          followLinks,
    const char*  name,
    void*        value,
    size_t       valueSize,
    size_t*      bytesWritten)
{
    struct VaFsDirectoryEntry  rootEntry;
    struct VaFsDirectoryEntry* entry;
    struct VaFsXattrSet*       set;
    struct VaFsXattr*          xattr;
    int                        status;

    if (vafs == NULL || path == NULL || name == NULL || bytesWritten == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = __resolve_xattr_entry(vafs, path, followLinks, &rootEntry, &entry);
    if (status != 0) {
        return status;
    }

    status = __ensure_entry_xattrs_loaded(entry);
    if (status != 0) {
        return status;
    }

    set = *__entry_xattr_slot(entry);
    xattr = __xattr_find(set, name);
    if (xattr == NULL) {
        errno = ENODATA;
        return -1;
    }

    *bytesWritten = xattr->ValueLength;
    // Size probes follow the same pattern as listxattr and let callers avoid
    // guessing at buffer sizes for variable-length values.
    if (value == NULL) {
        return 0;
    }

    if (valueSize < xattr->ValueLength) {
        errno = ERANGE;
        return -1;
    }

    if (xattr->ValueLength != 0) {
        memcpy(value, xattr->Value, xattr->ValueLength);
    }
    return 0;
}

int vafs_path_getxattr(
    struct VaFs* vafs,
    const char*  path,
    const char*  name,
    void*        value,
    size_t       valueSize,
    size_t*      bytesWritten)
{
    return __vafs_path_getxattr(vafs, path, 1, name, value, valueSize, bytesWritten);
}

int __vafs_path_setxattr(
    struct VaFs* vafs,
    const char*  path,
    int          followLinks,
    const char*  name,
    const void*  value,
    size_t       valueSize)
{
    struct VaFsDirectoryEntry  rootEntry;
    struct VaFsDirectoryEntry* entry;
    struct VaFsXattrSet**      xattrSlot;
    struct VaFsMetadata*       metadata;
    int*                       loadedSlot;
    int                        status;

    if (vafs == NULL || path == NULL || name == NULL || name[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    if (valueSize != 0 && value == NULL) {
        errno = EINVAL;
        return -1;
    }

    // The on-disk record uses 16-bit name lengths and 32-bit value lengths, so
    // reject values that cannot be represented before mutating in-memory state.
    if (strlen(name) > UINT16_MAX || valueSize > UINT32_MAX) {
        errno = EINVAL;
        return -1;
    }

    // Read-mode instances reflect a frozen image, so exposing mutation there
    // would only create an in-memory state the persisted format can never honor.
    if (vafs->Mode != VaFsMode_Write) {
        errno = EACCES;
        return -1;
    }

    status = __resolve_xattr_entry(vafs, path, followLinks, &rootEntry, &entry);
    if (status != 0) {
        return status;
    }

    xattrSlot = __entry_xattr_slot(entry);
    loadedSlot = __entry_xattr_loaded_slot(entry);
    metadata = __entry_stat_metadata(entry);
    if (xattrSlot == NULL || loadedSlot == NULL || metadata == NULL) {
        return -1;
    }

    if (*xattrSlot == NULL) {
        *xattrSlot = calloc(1, sizeof(struct VaFsXattrSet));
        if (*xattrSlot == NULL) {
            errno = ENOMEM;
            return -1;
        }
        (*xattrSlot)->Index = VA_FS_INVALID_XATTR_INDEX;
    }

    status = __xattr_set_put(*xattrSlot, name, value, (uint32_t)valueSize);
    if (status != 0) {
        return status;
    }

    // Keep the live stat view aligned with the xattr set immediately so later
    // stat calls and the final descriptor flush observe the same metadata.
    *loadedSlot = 1;
    metadata->XattrCount = (*xattrSlot)->Count;
    metadata->Mask |= VaFsMetadataMask_XattrCount;
    return 0;
}

int vafs_path_setxattr(
    struct VaFs* vafs,
    const char*  path,
    const char*  name,
    const void*  value,
    size_t       valueSize)
{
    return __vafs_path_setxattr(vafs, path, 1, name, value, valueSize);
}