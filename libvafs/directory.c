/**
 * Copyright 2022, Philip Meulengracht
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
#include "private.h"
#include <stdlib.h>
#include <string.h>

struct VaFsDirectoryEntry;

enum VaFsDirectoryState {
    VaFsDirectoryState_Open,
    VaFsDirectoryState_Loaded
};

struct VaFsDirectoryReader {
    struct VaFsDirectory       Base;
    enum VaFsDirectoryState    State;
    struct VaFsDirectoryEntry* Entries;
};

struct VaFsDirectoryWriter {
    struct VaFsDirectory       Base;
    struct VaFsDirectoryEntry* Entries;
};

struct VaFsDirectoryEntry {
    int Type;
    union {
        struct VaFsFile*      File;
        struct VaFsDirectory* Directory;
        struct VaFsSymlink*   Symlink;
    };
    struct VaFsDirectoryEntry* Link;
};

struct VaFsDirectoryHandle {
    struct VaFsDirectory* Directory;
    int                   Index;
};

static void __initialize_file_descriptor(
    VaFsFileDescriptor_t* descriptor,
    uint32_t              permissions)
{
    descriptor->Base.Type = VA_FS_DESCRIPTOR_TYPE_FILE;
    descriptor->Base.Length = sizeof(VaFsFileDescriptor_t);

    descriptor->Data.Index = VA_FS_INVALID_BLOCK;
    descriptor->Data.Offset = VA_FS_INVALID_OFFSET;
    descriptor->FileLength = 0;
    descriptor->Permissions = permissions;
}

static void __initialize_directory_descriptor(
    VaFsDirectoryDescriptor_t* descriptor,
    uint32_t                   permissions)
{
    descriptor->Base.Type = VA_FS_DESCRIPTOR_TYPE_DIRECTORY;
    descriptor->Base.Length = sizeof(VaFsDirectoryDescriptor_t);

    descriptor->Descriptor.Index = VA_FS_INVALID_BLOCK;
    descriptor->Descriptor.Offset = VA_FS_INVALID_OFFSET;
    descriptor->Permissions = permissions;
}

static void __initialize_symlink_descriptor(
    VaFsSymlinkDescriptor_t* descriptor)
{
    descriptor->Base.Type = VA_FS_DESCRIPTOR_TYPE_SYMLINK;
    descriptor->Base.Length = sizeof(VaFsSymlinkDescriptor_t);

    descriptor->NameLength = 0;
    descriptor->TargetLength = 0;
}

int vafs_directory_create_root(
    struct VaFs*           vafs,
    struct VaFsDirectory** directoryOut)
{
    struct VaFsDirectoryWriter* directory;
    
    if (vafs == NULL || directoryOut == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    directory = (struct VaFsDirectoryWriter*)malloc(sizeof(struct VaFsDirectoryWriter));
    if (!directory) {
        errno = ENOMEM;
        return -1;
    }
    memset(directory, 0, sizeof(struct VaFsDirectoryWriter));

    directory->Base.VaFs = vafs;
    directory->Base.Name = strdup("root");
    __initialize_directory_descriptor(&directory->Base.Descriptor, 0777);

    *directoryOut = (struct VaFsDirectory*)directory;
    return 0;
}

static int __get_descriptor_size(
    int type)
{
    switch (type) {
        case VA_FS_DESCRIPTOR_TYPE_FILE:
            return sizeof(VaFsFileDescriptor_t);
        case VA_FS_DESCRIPTOR_TYPE_DIRECTORY:
            return sizeof(VaFsDirectoryDescriptor_t);
        case VA_FS_DESCRIPTOR_TYPE_SYMLINK:
            return sizeof(VaFsSymlinkDescriptor_t);
        default:
            return 0;
    }
}

static int __read_descriptor(
    struct VaFsDirectoryReader* reader,
    char*                       buffer,
    char**                      extendedBufferOut)
{
    VaFsDescriptor_t* base = (VaFsDescriptor_t*)buffer;
    char*             ext  = (char*)buffer + sizeof(VaFsDescriptor_t);
    int               status;
    int               size;

    if (buffer == NULL || extendedBufferOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = vafs_stream_read(
        reader->Base.VaFs->DescriptorStream, 
        buffer, sizeof(VaFsDescriptor_t)
    );
    if (status) {
        VAFS_ERROR("__read_descriptor: failed to read base descriptor: %i\n", status);
        return status;
    }

    VAFS_INFO("__read_descriptor: desciptor found type=%u, length=%u\n",
        base->Type, base->Length);
    
    size = __get_descriptor_size(base->Type);
    if (base->Length < sizeof(VaFsDescriptor_t) || !size) {
        VAFS_ERROR("__read_descriptor: invalid descriptor size: %i for type %i\n", base->Length, base->Type);
        errno = EINVAL;
        return -1;
    }

    if (base->Length > sizeof(VaFsDescriptor_t)) {
        VAFS_DEBUG("__read_descriptor: read %u/%u descriptor bytes, reading rest\n", 
            sizeof(VaFsDescriptor_t), size);

        status = vafs_stream_read(
            reader->Base.VaFs->DescriptorStream, 
            ext, size - sizeof(VaFsDescriptor_t)
        );
        if (status) {
            VAFS_ERROR("__read_descriptor: failed to read extension descriptor: %i\n", status);
            return status;
        }

        if (base->Length > size) {
            VAFS_DEBUG("__read_descriptor: read %u/%u bytes, reading descriptor extension data\n",
                size, base->Length);

            // read rest of descriptor
            char* extendedBuffer = (char*)malloc(base->Length - size);
            if (!extendedBuffer) {
                VAFS_ERROR("__read_descriptor: failed to allocate extended buffer: %i\n", status);
                errno = ENOMEM;
                return -1;
            }

            status = vafs_stream_read(
                reader->Base.VaFs->DescriptorStream, 
                extendedBuffer, base->Length - size
            );
            if (status) {
                VAFS_ERROR("__read_descriptor: failed to read extended data: %i\n", status);
                free(extendedBuffer);
                return status;
            }
            *extendedBufferOut = extendedBuffer;
        }
    }

    return 0;
}

static const char* __read_extended_string(const char* buffer, size_t length)
{
    char* str = (char*)malloc(length + 1);
    if (!str) {
        VAFS_ERROR("__read_extended_string: failed to allocate string\n");
        errno = ENOMEM;
        return NULL;
    }

    memcpy(str, buffer, length);
    str[length] = '\0';
    return str;
}

static struct VaFsFile* __create_file_from_descriptor(
    struct VaFs*          vafs,
    VaFsFileDescriptor_t* descriptor,
    const char*           extendedData)
{
    struct VaFsFile* file;
    
    file = (struct VaFsFile*)malloc(sizeof(struct VaFsFile));
    if (!file) {
        errno = ENOMEM;
        return NULL;
    }
    
    memcpy(&file->Descriptor, descriptor, sizeof(VaFsFileDescriptor_t));
    file->Name = __read_extended_string(extendedData, descriptor->Base.Length - sizeof(VaFsFileDescriptor_t));
    file->VaFs = vafs;
    return file;
}

static struct VaFsDirectory* __create_directory_from_descriptor(
    struct VaFs*               vafs,
    VaFsDirectoryDescriptor_t* descriptor,
    const char*                extendedData)
{
    struct VaFsDirectoryReader* directory;
    
    directory = (struct VaFsDirectoryReader*)malloc(sizeof(struct VaFsDirectoryReader));
    if (!directory) {
        errno = ENOMEM;
        return NULL;
    }
    
    directory->State     = VaFsDirectoryState_Open;
    directory->Entries   = NULL;
    directory->Base.Name = __read_extended_string(extendedData, descriptor->Base.Length - sizeof(VaFsDirectoryDescriptor_t));
    directory->Base.VaFs = vafs;
    memcpy(&directory->Base.Descriptor, descriptor, sizeof(VaFsDirectoryDescriptor_t));
    return &directory->Base;
}

static struct VaFsSymlink* __create_symlink_from_descriptor(
    struct VaFs*             vafs,
    VaFsSymlinkDescriptor_t* descriptor,
    const char*              extendedData)
{
    struct VaFsSymlink* symlink;
    
    symlink = (struct VaFsSymlink*)malloc(sizeof(struct VaFsSymlink));
    if (!symlink) {
        errno = ENOMEM;
        return NULL;
    }
    
    memcpy(&symlink->Descriptor, descriptor, sizeof(VaFsSymlinkDescriptor_t));
    symlink->Name   = __read_extended_string(extendedData, descriptor->NameLength);
    symlink->Target = __read_extended_string(extendedData + descriptor->NameLength, descriptor->TargetLength);
    symlink->VaFs   = vafs;
    return symlink;
}

static struct VaFsDirectoryEntry* __create_entry_from_descriptor(
    struct VaFs*      vafs,
    VaFsDescriptor_t* descriptor,
    const char*       extendedData)
{
    struct VaFsDirectoryEntry* entry;
    
    entry = (struct VaFsDirectoryEntry*)malloc(sizeof(struct VaFsDirectoryEntry));
    if (!entry) {
        errno = ENOMEM;
        return NULL;
    }
    memset(entry, 0, sizeof(struct VaFsDirectoryEntry));

    entry->Type = descriptor->Type;
    if (entry->Type == VA_FS_DESCRIPTOR_TYPE_FILE) {
        entry->File = __create_file_from_descriptor(vafs, (VaFsFileDescriptor_t*)descriptor, extendedData);
    } else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
        entry->Directory = __create_directory_from_descriptor(vafs, (VaFsDirectoryDescriptor_t*)descriptor, extendedData);
    } else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_SYMLINK) {
        entry->Symlink = __create_symlink_from_descriptor(vafs, (VaFsSymlinkDescriptor_t*)descriptor, extendedData);
    } else {
        errno = EINVAL;
        return NULL;
    }
    return entry;
}

static int __load_directory(
    struct VaFsDirectoryReader* reader)
{
    VaFsDirectoryHeader_t header;
    int                   status;

    VAFS_DEBUG("__load_directory(directory=%s)\n", reader->Base.Name);

    // if the directory has no entries, we can skip loading it
    if (reader->Base.Descriptor.Descriptor.Index == VA_FS_INVALID_BLOCK) {
        reader->State = VaFsDirectoryState_Loaded;
        return 0;
    }

    status = vafs_stream_lock(reader->Base.VaFs->DescriptorStream);
    if (status) {
        VAFS_ERROR("__load_directory: failed to get lock on stream\n");
        return status;
    }

    // we lock the descriptor stream while reading the directory
    // as only one can access the underlying media at the time due
    // the c file interface.
    status = vafs_stream_seek(
        reader->Base.VaFs->DescriptorStream,
        reader->Base.Descriptor.Descriptor.Index,
        reader->Base.Descriptor.Descriptor.Offset
    );
    if (status) {
        VAFS_ERROR("__load_directory: failed to seek to directory data\n");
        vafs_stream_unlock(reader->Base.VaFs->DescriptorStream);
        return status;
    }

    // read the directory descriptor
    status = vafs_stream_read(
        reader->Base.VaFs->DescriptorStream,
        &header, sizeof(VaFsDirectoryHeader_t)
    );
    if (status) {
        VAFS_ERROR("__load_directory: failed to read directory header\n");
        vafs_stream_unlock(reader->Base.VaFs->DescriptorStream);
        return status;
    }

    // read the directory entries
    VAFS_INFO("__load_directory: reading %u entries\n", header.Count);
    for (uint32_t i = 0; i < header.Count; i++) {
        struct VaFsDirectoryEntry* entry;
        char                       buffer[64];
        char*                      extendedData = NULL;
        VAFS_INFO("__load_directory: reading entry %i/%u\n", i, header.Count);
        
        status = __read_descriptor(reader, &buffer[0], &extendedData);
        if (status) {
            VAFS_ERROR("__load_directory: failed to read descriptor\n");
            vafs_stream_unlock(reader->Base.VaFs->DescriptorStream);
            return status;
        }

        // create a new entry
        entry = __create_entry_from_descriptor(reader->Base.VaFs, (VaFsDescriptor_t*)&buffer[0], extendedData);
        free(extendedData);

        if (!entry) {
            VAFS_ERROR("__load_directory: failed to create entry\n");
            vafs_stream_unlock(reader->Base.VaFs->DescriptorStream);
            return -1;
        }

        // add the entry to the directory
        entry->Link = reader->Entries;
        reader->Entries = entry;
    }

    // unlock the descriptor stream
    vafs_stream_unlock(reader->Base.VaFs->DescriptorStream);

    // set state to loaded
    reader->State = VaFsDirectoryState_Loaded;
    return 0;
}

int vafs_directory_open_root(
    struct VaFs*           vafs,
    VaFsBlockPosition_t*   position,
    struct VaFsDirectory** directoryOut)
{
    struct VaFsDirectoryReader* reader;
    
    if (vafs == NULL || position == NULL || directoryOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    VAFS_DEBUG("vafs_directory_open_root(pos=%u/%u)\n", position->Index, position->Offset);
    
    reader = (struct VaFsDirectoryReader*)malloc(sizeof(struct VaFsDirectoryReader));
    if (!reader) {
        VAFS_ERROR("vafs_directory_open_root: failed to allocate directory reader\n");
        errno = ENOMEM;
        return -1;
    }

    reader->Base.VaFs = vafs;
    reader->Base.Name = strdup("root");
    reader->State     = VaFsDirectoryState_Open;
    reader->Entries   = NULL;
    
    // initialize the root descriptor for the directory
    reader->Base.Descriptor.Base.Length = sizeof(VaFsDirectoryDescriptor_t);
    reader->Base.Descriptor.Base.Type   = VA_FS_DESCRIPTOR_TYPE_DIRECTORY;
    reader->Base.Descriptor.Descriptor.Index = position->Index;
    reader->Base.Descriptor.Descriptor.Offset = position->Offset;

    *directoryOut = (struct VaFsDirectory*)reader;
    return 0;
}

static int __get_path_token(
    const char* path,
    char*       token,
    size_t      tokenSize)
{
    size_t i;
    size_t j;
    size_t remainingLength;

    if (path == NULL || token == NULL) {
        errno = EINVAL;
        return 0;
    }

    remainingLength = strlen(path);
    if (remainingLength == 0) {
        errno = ENOENT;
        return 0;
    }

    // skip leading slashes
    for (i = 0; i < remainingLength; i++) {
        if (path[i] != '/') {
            break;
        }
    }

    // copy over token untill \0 or /
    for (j = 0; i < remainingLength; i++, j++) {
        if (path[i] == '/' || path[i] == '\0') {
            break;
        }

        if (j >= tokenSize) {
            errno = ENAMETOOLONG;
            return 0;
        }
        token[j] = path[i];
    }
    token[j] = '\0';
    return (int)i;
}

static struct VaFsDirectoryEntry* __get_first_entry(
    struct VaFsDirectory* directory)
{
    VAFS_INFO("__get_first_entry(directory=%s)\n", directory->Name);
    if (directory->VaFs->Mode == VaFsMode_Read) {
        struct VaFsDirectoryReader* reader = (struct VaFsDirectoryReader*)directory;
        if (reader->State != VaFsDirectoryState_Loaded) {
            if (__load_directory(reader)) {
                VAFS_ERROR("__get_first_entry: directory not loaded\n");
                return NULL;
            }
        }
        return reader->Entries;
    }
    else {
        struct VaFsDirectoryWriter* writer = (struct VaFsDirectoryWriter*)directory;
        return writer->Entries;
    }
}

static int __is_root(
    const char* path)
{
    size_t len = strlen(path);
    if (len == 1 && path[0] == '/') {
        return 1;
    }
    if (len == 0) {
        return 1;
    }
    return 0;
}

static const char* __get_entry_name(
    struct VaFsDirectoryEntry* entry)
{
    if (entry->Type == VA_FS_DESCRIPTOR_TYPE_FILE) {
        return entry->File->Name;
    } else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
        return entry->Directory->Name;
    } else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_SYMLINK) {
        return entry->Symlink->Name;
    }
    return NULL;
}

static struct VaFsDirectoryHandle* __create_handle(
    struct VaFsDirectory* directory)
{
    struct VaFsDirectoryHandle* handle;

    handle = (struct VaFsDirectoryHandle*)malloc(sizeof(struct VaFsDirectoryHandle));
    if (!handle) {
        errno = ENOMEM;
        return NULL;
    }

    handle->Directory = directory;
    handle->Index     = 0;
    return handle;
}

int vafs_directory_open(
    struct VaFs*                 vafs,
    const char*                  path,
    struct VaFsDirectoryHandle** handleOut)
{
    struct VaFsDirectoryEntry* entries;
    const char*                remainingPath = path;
    char                       token[128];

    if (vafs == NULL || path == NULL || handleOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (__is_root(path)) {
        *handleOut = __create_handle(vafs->RootDirectory);
        return 0;
    }

    // get initial entry
    entries = __get_first_entry(vafs->RootDirectory);

    do {
        // get the next token
        int charsConsumed = __get_path_token(remainingPath, token, sizeof(token));
        if (!charsConsumed) {
            break;
        }
        remainingPath += charsConsumed;

        // find the name in the directory
        while (entries != NULL) {
            if (!strcmp(__get_entry_name(entries), token)) {
                if (entries->Type != VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
                    errno = ENOTDIR;
                    return -1;
                }

                if (remainingPath[0] == '\0') {
                    // we found the directory
                    *handleOut = __create_handle(entries->Directory);
                    return 0;
                }

                entries = __get_first_entry(entries->Directory);
                break;
            }
            entries = entries->Link;
        }
    } while (1);
    return -1;
}

static int __write_directory_header(
    struct VaFsDirectoryWriter* writer,
    int                         count)
{
    VaFsDirectoryHeader_t header;
    VAFS_DEBUG("vafs_directory_write_header(count=%d)\n", count);

    header.Count = count;

    return vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        &header,
        sizeof(VaFsDirectoryHeader_t)
    );
}

static int __write_file_descriptor(
    struct VaFsDirectoryWriter* writer,
    struct VaFsDirectoryEntry*  entry)
{
    int status;
    VAFS_DEBUG("vafs_directory_write_file_descriptor(name=%s)\n",
        entry->File->Name);

    // increase descriptor length by name, do not account
    // for the null terminator
    entry->File->Descriptor.Base.Length += (uint16_t)strlen(entry->File->Name);

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        &entry->File->Descriptor,
        sizeof(VaFsFileDescriptor_t)
    );
    if (status != 0) {
        return status;
    }

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        entry->File->Name,
        strlen(entry->File->Name)
    );
    return status;
}

static int __write_directory_descriptor(
    struct VaFsDirectoryWriter* writer,
    struct VaFsDirectoryEntry*  entry)
{
    int status;
    VAFS_DEBUG("vafs_directory_write_directory_descriptor(name=%s)\n",
        entry->Directory->Name);
    
    // increase descriptor length by name, do not account
    // for the null terminator
    entry->Directory->Descriptor.Base.Length += (uint16_t)strlen(entry->Directory->Name);

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        &entry->Directory->Descriptor,
        sizeof(VaFsDirectoryDescriptor_t)
    );
    if (status != 0) {
        return status;
    }

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        entry->Directory->Name,
        strlen(entry->Directory->Name)
    );
    return status;
}

static int __write_symlink_descriptor(
    struct VaFsDirectoryWriter* writer,
    struct VaFsDirectoryEntry*  entry)
{
    int status;
    VAFS_DEBUG("__write_symlink_descriptor(name=%s)\n",
        entry->Symlink->Name);

    entry->Symlink->Descriptor.NameLength   = (uint16_t)strlen(entry->Symlink->Name);
    entry->Symlink->Descriptor.TargetLength = (uint16_t)strlen(entry->Symlink->Target);

    // increase descriptor length by names, do not account
    // for the null terminator
    entry->Symlink->Descriptor.Base.Length += entry->Symlink->Descriptor.NameLength;
    entry->Symlink->Descriptor.Base.Length += entry->Symlink->Descriptor.TargetLength;

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        &entry->Symlink->Descriptor,
        sizeof(VaFsSymlinkDescriptor_t)
    );
    if (status != 0) {
        return status;
    }

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        entry->Symlink->Name,
        strlen(entry->Symlink->Name)
    );
    if (status != 0) {
        return status;
    }

    status = vafs_stream_write(
        writer->Base.VaFs->DescriptorStream,
        entry->Symlink->Target,
        strlen(entry->Symlink->Target)
    );
    return status;
}

int vafs_directory_flush(
    struct VaFsDirectory* directory)
{
    struct VaFsDirectoryWriter* writer = (struct VaFsDirectoryWriter*)directory;
    struct VaFsDirectoryEntry*  entry;
    int                         status;
    int                         entryCount = 0;
    vafsblock_t                 block;
    uint32_t                    offset;
    VAFS_DEBUG("vafs_directory_flush(name=%s)\n", directory->Name);

    // We must flush all subdirectories first to initalize their
    // index and offset. Otherwise, we will be writing empty descriptors
    // for subdirectories.
    entry = writer->Entries;
    while (entry != NULL) {
        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
            // flush the directory
            vafs_directory_flush(entry->Directory);
        }
        entryCount++;
        entry = entry->Link;
    }

    // get current stream position;
    status = vafs_stream_position(
        writer->Base.VaFs->DescriptorStream,
        &block, &offset
    );
    if (status) {
        VAFS_ERROR("vafs_directory_flush: failed to get stream position\n");
        return status;
    }

    directory->Descriptor.Descriptor.Index  = block;
    directory->Descriptor.Descriptor.Offset = offset;
    VAFS_DEBUG("vafs_directory_flush  name=%s index=%d offset=%d\n", directory->Name, block, offset);

    status = __write_directory_header(writer, entryCount);
    if (status) {
        VAFS_ERROR("vafs_directory_flush: failed to write directory header\n");
        return status;
    }

    // now we actually write all the descriptors
    entry = writer->Entries;
    while (entry != NULL) {
        VAFS_DEBUG("vafs_directory_flush: writing entry=%s, type=%i\n",
            entry->File->Name, entry->Type);
        if (entry->Type == VA_FS_DESCRIPTOR_TYPE_FILE) {
            status = __write_file_descriptor(writer, entry);
        } else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
            status = __write_directory_descriptor(writer, entry);
        } else if (entry->Type == VA_FS_DESCRIPTOR_TYPE_SYMLINK) {
            status = __write_symlink_descriptor(writer, entry);
        } else {
            VAFS_ERROR("vafs_directory_flush: unknown descriptor type\n");
            return -1;
        }

        if (status) {
            VAFS_ERROR("vafs_directory_flush: failed to write descriptor: %i\n", status);
            return status;
        }
        entry = entry->Link;
    }
    return 0;
}

uint32_t vafs_directory_permissions(
    struct VaFsDirectoryHandle* handle)
{
    if (handle == NULL) {
        errno = EINVAL;
        return (uint32_t)-1;
    }

    return handle->Directory->Descriptor.Permissions;
}

int vafs_directory_read(
    struct VaFsDirectoryHandle* handle,
    struct VaFsEntry*           entryOut)
{
    struct VaFsDirectoryEntry* entry;
    int                        i;
    VAFS_INFO("vafs_directory_read(handle=%p)\n", handle);

    if (handle == NULL || entryOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    VAFS_DEBUG("vafs_directory_read: locate index %i\n", handle->Index);
    entry = __get_first_entry(handle->Directory);
    i       = 0;
    while (entry != NULL) {
        if (i == handle->Index) {
            break;
        }
        entry = entry->Link;
        i++;
    }

    if (entry == NULL) {
        VAFS_INFO("vafs_directory_read: end of directory\n");
        errno = ENOENT;
        return -1;
    }
    VAFS_DEBUG("vafs_directory_read: found entry %s\n", entry->File->Name);

    // we found an entry, move to next
    handle->Index++;

    // initialize the entry structure
    entryOut->Name = __get_entry_name(entry);
    entryOut->Type = (enum VaFsEntryType)entry->Type;
    return 0;
}

int vafs_directory_close(
    struct VaFsDirectoryHandle* handle)
{
    if (handle == NULL) {
        errno = EINVAL;
        return -1;
    }

    // free handle
    free(handle);
    return 0;
}

static int __add_file_entry(
    struct VaFsDirectoryWriter* writer,
    struct VaFsFile*            entry)
{
    struct VaFsDirectoryEntry* newEntry;

    newEntry = (struct VaFsDirectoryEntry*)malloc(sizeof(struct VaFsDirectoryEntry));
    if (!newEntry) {
        errno = ENOMEM;
        return -1;
    }

    newEntry->Type = VA_FS_DESCRIPTOR_TYPE_FILE;
    newEntry->File = entry;
    newEntry->Link = writer->Entries;
    writer->Entries = newEntry;
    return 0;
}

static int __create_file_entry(
    struct VaFsDirectoryWriter* writer,
    const char*                 name,
    uint32_t                    permissions)
{
    struct VaFsFile* entry;
    int              status;

    entry = (struct VaFsFile*)malloc(sizeof(struct VaFsFile));
    if (!entry) {
        errno = ENOMEM;
        return -1;
    }

    entry->VaFs = writer->Base.VaFs;
    entry->Name = strdup(name);
    if (!entry->Name) {
        free(entry);
        errno = ENOMEM;
        return -1;
    }

    __initialize_file_descriptor(&entry->Descriptor, permissions);
    status = __add_file_entry(writer, entry);
    if (status) {
        free((void*)entry->Name);
        free(entry);
        return status;
    }

    writer->Base.VaFs->Overview.Counts.Files++;
    return 0;
}

static int __add_symlink_entry(
    struct VaFsDirectoryWriter* writer,
    struct VaFsSymlink*         entry)
{
    struct VaFsDirectoryEntry* newEntry;

    newEntry = (struct VaFsDirectoryEntry*)malloc(sizeof(struct VaFsDirectoryEntry));
    if (!newEntry) {
        errno = ENOMEM;
        return -1;
    }

    newEntry->Type    = VA_FS_DESCRIPTOR_TYPE_SYMLINK;
    newEntry->Symlink = entry;
    newEntry->Link    = writer->Entries;

    writer->Entries = newEntry;
    return 0;
}

static int __create_symlink_entry(
    struct VaFsDirectoryWriter* writer,
    const char*                 name,
    const char*                 target)
{
    struct VaFsSymlink* entry;
    int                 status;

    entry = (struct VaFsSymlink*)malloc(sizeof(struct VaFsSymlink));
    if (!entry) {
        errno = ENOMEM;
        return -1;
    }

    entry->VaFs = writer->Base.VaFs;
    entry->Name = strdup(name);
    if (!entry->Name) {
        free(entry);
        errno = ENOMEM;
        return -1;
    }

    entry->Target = strdup(target);
    if (!entry->Target) {
        free((void*)entry->Name);
        free(entry);
        errno = ENOMEM;
        return -1;
    }

    __initialize_symlink_descriptor(&entry->Descriptor);
    status = __add_symlink_entry(writer, entry);
    if (status) {
        free((void*)entry->Target);
        free((void*)entry->Name);
        free(entry);
        return status;
    }

    writer->Base.VaFs->Overview.Counts.Symlinks++;
    return 0;
}

static int __add_directory_entry(
    struct VaFsDirectoryWriter* writer,
    struct VaFsDirectory*       entry)
{
    struct VaFsDirectoryEntry* newEntry;

    newEntry = (struct VaFsDirectoryEntry*)malloc(sizeof(struct VaFsDirectoryEntry));
    if (!newEntry) {
        errno = ENOMEM;
        return -1;
    }

    newEntry->Type = VA_FS_DESCRIPTOR_TYPE_DIRECTORY;
    newEntry->Directory = entry;
    newEntry->Link = writer->Entries;
    writer->Entries = newEntry;
    return 0;
}

static int __create_directory_entry(
    struct VaFsDirectoryWriter* writer,
    const char*                 name,
    uint32_t                    permissions)
{
    struct VaFsDirectoryWriter* entry;
    int                         status;
    VAFS_DEBUG("__create_directory_entry(name=%s)\n", name);

    entry = (struct VaFsDirectoryWriter*)malloc(sizeof(struct VaFsDirectoryWriter));
    if (!entry) {
        errno = ENOMEM;
        return -1;
    }

    entry->Entries = NULL;
    entry->Base.VaFs = writer->Base.VaFs;
    entry->Base.Name = strdup(name);
    if (!entry->Base.Name) {
        free(entry);
        errno = ENOMEM;
        return -1;
    }

    __initialize_directory_descriptor(&entry->Base.Descriptor, permissions);
    status = __add_directory_entry(writer, &entry->Base);
    if (status) {
        free((void*)entry->Base.Name);
        free(entry);
        return status;
    }

    writer->Base.VaFs->Overview.Counts.Directories++;
    return 0;
}

static struct VaFsDirectoryEntry* __find_entry(
    struct VaFsDirectory* directory,
    const char*           token)
{
    struct VaFsDirectoryEntry* entries;

    // find the name in the directory
    entries = __get_first_entry(directory);
    while (entries != NULL) {
        if (!strcmp(__get_entry_name(entries), token)) {
            return entries;
        }
        entries = entries->Link;
    }
    return NULL;
}

int vafs_directory_open_directory(
    struct VaFsDirectoryHandle*  handle,
    const char*                  name,
    struct VaFsDirectoryHandle** handleOut)
{
    struct VaFsDirectoryEntry* entry;
    char                       token[128];
    VAFS_DEBUG("vafs_directory_open_directory(handle=%p, name=%s, handleOut=%p)\n", handle, name, handleOut);

    if (handle == NULL || name == NULL || handleOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (handle->Directory->VaFs->Mode != VaFsMode_Read) {
        errno = EACCES;
        return -1;
    }

    // do this to verify the incoming name
    if (!__get_path_token(name, token, sizeof(token))) {
        errno = ENOENT;
        return -1;
    }

    // find the name in the directory
    entry = __find_entry(handle->Directory, token);
    if (entry == NULL) {
        errno = ENOENT;
        return -1;
    }

    if (entry->Type != VA_FS_DESCRIPTOR_TYPE_DIRECTORY) {
        errno = ENOTDIR;
        return -1;
    }

    *handleOut = __create_handle(entry->Directory);
    return 0;
}

int vafs_directory_create_directory(
    struct VaFsDirectoryHandle*  handle,
    const char*                  name,
    uint32_t                     permissions,
    struct VaFsDirectoryHandle** handleOut)
{

    struct VaFsDirectoryWriter* writer;
    int                         status;
    struct VaFsDirectoryEntry*  entry;
    char                        token[128];
    VAFS_DEBUG("vafs_directory_create_directory(handle=%p, name=%s, handleOut=%p)\n", handle, name, handleOut);

    if (handle == NULL || name == NULL || handleOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (handle->Directory->VaFs->Mode != VaFsMode_Write) {
        errno = EACCES;
        return -1;
    }

    // do this to verify the incoming name
    if (!__get_path_token(name, token, sizeof(token))) {
        errno = ENOENT;
        return -1;
    }

    // find the name in the directory
    entry = __find_entry(handle->Directory, token);
    if (entry != NULL) {
        errno = EEXIST;
        return -1;
    }

    writer = (struct VaFsDirectoryWriter*)handle->Directory;
    status = __create_directory_entry(writer, token, permissions);
    if (status != 0) {
        return status;
    }
    entry = __find_entry(handle->Directory, token);

    *handleOut = __create_handle(entry->Directory);
    return 0;
}

int vafs_directory_open_file(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    struct VaFsFileHandle**     handleOut)
{
    struct VaFsDirectoryEntry* entry;
    char                       token[128];
    VAFS_DEBUG("vafs_directory_open_file(name=%s)\n", name);

    if (handle == NULL || name == NULL || handleOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    // verify read mode
    if (handle->Directory->VaFs->Mode != VaFsMode_Read) {
        errno = EACCES;
        return -1;
    }

    // do this to verify the incoming name
    if (!__get_path_token(name, token, sizeof(token))) {
        errno = ENOENT;
        return -1;
    }

    // find the name in the directory
    entry = __find_entry(handle->Directory, token);
    if (entry == NULL) {
        errno = ENOENT;
        return -1;
    }

    if (entry->Type != VA_FS_DESCRIPTOR_TYPE_FILE) {
        errno = ENFILE;
        return -1;
    }

    *handleOut = vafs_file_create_handle(entry->File);
    return 0;
}

int vafs_directory_create_file(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    uint32_t                    permissions,
    struct VaFsFileHandle**     handleOut)
{
    struct VaFsDirectoryWriter* writer;
    int                         status;
    struct VaFsDirectoryEntry*  entry;
    char                        token[128];
    VAFS_DEBUG("vafs_directory_create_file(name=%s)\n", name);

    if (handle == NULL || name == NULL || handleOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    // verify write mode
    if (handle->Directory->VaFs->Mode != VaFsMode_Write) {
        errno = EACCES;
        return -1;
    }

    // do this to verify the incoming name
    if (!__get_path_token(name, token, sizeof(token))) {
        errno = ENOENT;
        return -1;
    }

    // find the name in the directory
    entry = __find_entry(handle->Directory, token);
    if (entry != NULL) {
        errno = EEXIST;
        return -1;
    }

    writer = (struct VaFsDirectoryWriter*)handle->Directory;
    status = __create_file_entry(writer, token, permissions);
    if (status != 0) {
        return status;
    }
    entry = __find_entry(handle->Directory, token);
    
    *handleOut = vafs_file_create_handle(entry->File);
    return 0;
}

int vafs_directory_create_symlink(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    const char*                 target)
{
    struct VaFsDirectoryEntry* entry;
    char                       token[128];
    VAFS_DEBUG("vafs_directory_create_symlink(name=%s, target=%s)\n", name, target);

    if (handle == NULL || name == NULL || target == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (handle->Directory->VaFs->Mode != VaFsMode_Write) {
        errno = EACCES;
        return -1;
    }

    // do this to verify the incoming name
    if (!__get_path_token(name, token, sizeof(token))) {
        errno = ENOENT;
        return -1;
    }

    // find the name in the directory
    VAFS_DEBUG("vafs_directory_create_symlink: locating %s\n", token);
    entry = __find_entry(handle->Directory, token);
    if (entry == NULL) {
        struct VaFsDirectoryWriter* writer = (struct VaFsDirectoryWriter*)handle->Directory;
        return __create_symlink_entry(writer, token, target);
    }

    errno = EEXIST;
    return -1;
}

int vafs_directory_read_symlink(
    struct VaFsDirectoryHandle* handle,
    const char*                 name,
    const char**                targetOut)
{
    struct VaFsDirectoryEntry* entry;
    char                       token[128];
    VAFS_DEBUG("vafs_directory_read_symlink(name=%s)\n", name);

    if (handle == NULL || name == NULL || targetOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (handle->Directory->VaFs->Mode != VaFsMode_Read) {
        errno = EACCES;
        return -1;
    }

    // do this to verify the incoming name
    if (!__get_path_token(name, token, sizeof(token))) {
        errno = ENOENT;
        return -1;
    }

    // find the name in the directory
    VAFS_DEBUG("vafs_directory_read_symlink: locating %s\n", token);
    entry = __find_entry(handle->Directory, token);
    if (entry == NULL) {
        errno = ENOENT;
        return -1;
    }
    
    if (entry->Type != VA_FS_DESCRIPTOR_TYPE_SYMLINK) {
        errno = EINVAL;
        return -1;
    }

    *targetOut = entry->Symlink->Target;
    return 0;
}
