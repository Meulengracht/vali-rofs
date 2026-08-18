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
 * Vali Container Filesystem
 * - Contains the implementation of the Vali Container Filesystem.
 *   This filesystem is used to store the initrd of the kernel.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#include <io.h>
#include <windows.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif

#include <vafs/backend.h>
#include <vafs/platform.h>

#include "../core/core.h"

#define __TRANSFER_BUFFER_SIZE 1024*1024

static int  __file_get_size(void*, uint64_t*);
static int  __file_read_at(void*, uint64_t, void*, size_t, size_t*);
static int  __file_write(void*, const void*, size_t, size_t*);
static int  __file_flush(void*);
static int  __file_close(void*);

static struct VaFsReaderBackendOps g_fileReaderBackendOps = {
    .readAt  = __file_read_at,
    .getSize = __file_get_size,
    .close   = __file_close
};

static struct VaFsBuilderBackendOps g_fileWriterBackendOps = {
    .write = __file_write,
    .flush = __file_flush,
    .close = __file_close
};

static int  __memory_get_size(void*, uint64_t*);
static int  __memory_read_at(void*, uint64_t, void*, size_t, size_t*);
static int  __memory_write(void*, const void*, size_t, size_t*);
static int  __memory_close(void*);

static struct VaFsReaderBackendOps g_memReaderBackendOps = {
    .readAt  = __memory_read_at,
    .getSize = __memory_get_size,
    .close   = __memory_close
};

static struct VaFsBuilderBackendOps g_memWriterBackendOps = {
    .write = __memory_write,
    .close = __memory_close
};

struct VaFsStreamDevice {
    int   ReadOnly;
    mtx_t Lock;
    void* UserData;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4201)
#endif
    union {
        struct {
            char* Buffer;
            // Current byte capacity of Buffer
            long Capacity;
            // The number of valid bytes in Buffer
            long Size;
            // The current position into buffer. This can not
            // be beyond Size.
            long Position;
            // Whether the streamdevice owns Buffer.
            int Owned;
        } Memory;
        FILE* File;
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif
};

struct VaFsStreamDeviceReader {
    struct VaFsStreamDevice     Base;
    struct VaFsReaderBackendOps Backend;
};

struct VaFsStreamDeviceWriter {
    struct VaFsStreamDevice       Base;
    struct VaFsBuilderBackendOps  Backend;
    uint64_t Position;
};

static int __new_streamdevice_reader(
    struct VaFsReaderBackendOps* backend,
    void*                        userData,
    struct VaFsStreamDevice**    deviceOut)
{
    struct VaFsStreamDeviceReader* device;

    // Validate the operations provided, based on the read-only status
    // of the vafs image, there must be some of the operations set. The
    // minimum is seek/read, but if it's not read-only, then write must
    // also be provided. Close is always optional.
    if (backend->readAt == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    device = (struct VaFsStreamDeviceReader*)malloc(sizeof(struct VaFsStreamDeviceReader));
    if (!device) {
        errno = ENOMEM;
        return -1;
    }

    memset(device, 0, sizeof(struct VaFsStreamDeviceReader));
    memcpy(&device->Backend, backend, sizeof(struct VaFsReaderBackendOps));

    mtx_init(&device->Base.Lock, mtx_plain);
    device->Base.ReadOnly = 1;
    device->Base.UserData = userData;

    *deviceOut = &device->Base;
    return 0;
}

static int __new_streamdevice_writer(
    struct VaFsBuilderBackendOps* backend,
    void*                         userData,
    struct VaFsStreamDevice**     deviceOut)
{
    struct VaFsStreamDeviceWriter* device;

    // Validate the operations provided, based on the read-only status
    // of the vafs image, there must be some of the operations set. The
    // minimum is seek/read, but if it's not read-only, then write must
    // also be provided. Close is always optional.
    if (backend->write == NULL) {
        errno = EINVAL;
        return -1;
    }

    device = (struct VaFsStreamDeviceWriter*)malloc(sizeof(struct VaFsStreamDeviceWriter));
    if (!device) {
        errno = ENOMEM;
        return -1;
    }

    memset(device, 0, sizeof(struct VaFsStreamDeviceWriter));
    memcpy(&device->Backend, backend, sizeof(struct VaFsBuilderBackendOps));

    mtx_init(&device->Base.Lock, mtx_plain);
    device->Base.ReadOnly = 0;
    device->Base.UserData = userData;

    *deviceOut = &device->Base;
    return 0;
}

int vafs_streamdevice_open_file(
    const char*               path,
    struct VaFsStreamDevice** deviceOut)
{
    struct VaFsStreamDevice* device;
    FILE*                    handle;
    int                      status;

    if (path == NULL  || deviceOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    handle = fopen(path, "rb");
    if (!handle) {
        return -1;
    }

    status = __new_streamdevice_reader(&g_fileReaderBackendOps, NULL, &device);
    if (status) {
        fclose(handle);
        return -1;
    }

    device->File     = handle;
    device->UserData = device;
    
    *deviceOut = device;
    return 0;
}

int vafs_streamdevice_open_memory(
    const void*               buffer,
    size_t                    length,
    struct VaFsStreamDevice** deviceOut)
{
    struct VaFsStreamDevice* device;
    int                      status;

    if (buffer == NULL || length == 0 || deviceOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = __new_streamdevice_reader(&g_memReaderBackendOps, NULL, &device);
    if (status) {
        return -1;
    }

    device->UserData        = device;
    device->Memory.Buffer   = (void*)buffer;
    device->Memory.Capacity = (long)length;
    device->Memory.Size     = (long)length;
    device->Memory.Position = 0;
    device->Memory.Owned    = 0;
    
    *deviceOut = device;
    return 0;
}

int vafs_streamdevice_reader_new(
    struct VaFsReaderBackendOps* backend,
    void*                        userData,
    struct VaFsStreamDevice**    deviceOut)
{
    struct VaFsStreamDevice* device;
    int                      status;

    if (backend == NULL || deviceOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = __new_streamdevice_reader(backend, userData, &device);
    if (status) {
        return -1;
    }

    *deviceOut = device;
    return 0;
}

int vafs_streamdevice_writer_new(
    struct VaFsBuilderBackendOps* backend,
    void*                        userData,
    struct VaFsStreamDevice**    deviceOut)
{
    struct VaFsStreamDevice* device;
    int                      status;

    if (backend == NULL || deviceOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = __new_streamdevice_writer(backend, userData, &device);
    if (status) {
        return -1;
    }

    *deviceOut = device;
    return 0;
}

int vafs_streamdevice_create_file(
    const char*               path,
    struct VaFsStreamDevice** deviceOut)
{
    struct VaFsStreamDevice* device;
    FILE*                    handle;
    int                      status;

    if (path == NULL  || deviceOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    handle = fopen(path, "wb+");
    if (!handle) {
        return -1;
    }

    status = __new_streamdevice_writer(&g_fileWriterBackendOps, NULL, &device);
    if (status) {
        fclose(handle);
        return -1;
    }

    device->UserData = device;
    device->File     = handle;
    
    *deviceOut = device;
    return 0;
}

int vafs_streamdevice_create_memory(
    size_t                    blockSize,
    struct VaFsStreamDevice** deviceOut)
{
    struct VaFsStreamDevice* device;
    void*                    buffer;
    int                      status;

    if (blockSize == 0 || deviceOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    buffer = malloc(blockSize);
    if (!buffer) {
        errno = ENOMEM;
        return -1;
    }

    status = __new_streamdevice_writer(&g_memWriterBackendOps, NULL, &device);
    if (status) {
        free(buffer);
        return -1;
    }

    device->UserData = device;
    device->Memory.Buffer = buffer;
    device->Memory.Capacity = (long)blockSize;
    device->Memory.Size = 0;
    device->Memory.Position = 0;
    device->Memory.Owned = 1;
    
    *deviceOut = device;
    return 0;
}

int vafs_streamdevice_close(
    struct VaFsStreamDevice* device)
{
    if (device == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (device->ReadOnly) {
        struct VaFsStreamDeviceReader* reader = (struct VaFsStreamDeviceReader*)device;
        if (reader->Backend.close) {
            reader->Backend.close(reader->Base.UserData);
        }
    } else {
        struct VaFsStreamDeviceWriter* writer = (struct VaFsStreamDeviceWriter*)device;
        if (writer->Backend.close) {
            writer->Backend.close(writer->Base.UserData);
        }
    }

    mtx_destroy(&device->Lock);
    free(device);
    return 0;
}

int vafs_streamdevice_read_at(
    struct VaFsStreamDevice* device,
    long                     offset,
    void*                    buffer,
    size_t                   length,
    size_t*                  bytesRead)
{
    struct VaFsStreamDeviceReader* reader = (struct VaFsStreamDeviceReader*)device;
    
    if (device == NULL || buffer == NULL || length == 0 || bytesRead == NULL || offset < 0) {
        errno = EINVAL;
        return -1;
    }

    if (!device->ReadOnly) {
        if (device->Memory.Buffer == NULL) {
            errno = ENOTSUP;
            return -1;
        }
        return __memory_read_at(device, (uint64_t)offset, buffer, length, bytesRead);
    }

    if (reader->Backend.readAt == NULL) {
        errno = ENOTSUP;
        return -1;
    }
    return reader->Backend.readAt(device->UserData, (uint64_t)offset, buffer, length, bytesRead);
}

int vafs_streamdevice_write(
    struct VaFsStreamDevice* device,
    const void*              buffer,
    size_t                   length,
    size_t*                  bytesWritten)
{
    struct VaFsStreamDeviceWriter* writer = (struct VaFsStreamDeviceWriter*)device;
    VAFS_DEBUG("vafs_streamdevice_write(length=%zu)\n", length);
    
    if (device == NULL || buffer == NULL || length == 0 || bytesWritten == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (device->ReadOnly) {
        errno = EACCES;
        return -1;
    }
    if (writer->Backend.write == NULL) {
        errno = ENOTSUP;
        return -1;
    }

    int status = writer->Backend.write(writer->Base.UserData, buffer, length, bytesWritten);
    if (status == 0) {
        writer->Position += *bytesWritten;
    }
    return status;
}

int vafs_streamdevice_size(
    struct VaFsStreamDevice* device,
    uint64_t*                sizeOut)
{
    struct VaFsStreamDeviceReader* reader = (struct VaFsStreamDeviceReader*)device;
    
    if (device == NULL || sizeOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    // For writer streams, the position is dynamically updated as bytes are written.
    if (!device->ReadOnly) {
        struct VaFsStreamDeviceWriter* writer = (struct VaFsStreamDeviceWriter*)device;
        *sizeOut = writer->Position;
        return 0;
    }

    if (reader->Backend.getSize == NULL) {
        errno = ENOTSUP;
        return -1;
    }
    return reader->Backend.getSize(device->UserData, sizeOut);
}

int vafs_streamdevice_copy(
    struct VaFsStreamDevice* destination,
    struct VaFsStreamDevice* source)
{
    char*    transferBuffer;
    int      status = 0;
    uint64_t sourceSize;
    uint64_t offset = 0;
    size_t   bytesRead;
    VAFS_DEBUG("vafs_streamdevice_copy()\n");

    if (destination == NULL || source == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (destination->ReadOnly) {
        errno = EACCES;
        return -1;
    }

    transferBuffer = malloc(__TRANSFER_BUFFER_SIZE);
    if (transferBuffer == NULL) {
        return -1;
    }

    status = vafs_streamdevice_size(source, &sourceSize);
    if (status) {
        free(transferBuffer);
        return status;
    }

    while (offset < sourceSize) {
        size_t bytesWritten;
        size_t byteCount = MIN((size_t)(sourceSize - offset), (size_t)__TRANSFER_BUFFER_SIZE);

        status = vafs_streamdevice_read_at(source, (long)offset, transferBuffer, byteCount, &bytesRead);
        VAFS_DEBUG("vafs_streamdevice_copy read %zu bytes\n", bytesRead);
        if (status || bytesRead != byteCount) {
            break;
        }

        status = vafs_streamdevice_write(destination, transferBuffer, bytesRead, &bytesWritten);
        VAFS_DEBUG("vafs_streamdevice_copy wrote %zu bytes\n", bytesWritten);
        if (status || bytesWritten != bytesRead) {
            break;
        }
        offset += bytesRead;
    }

    free(transferBuffer);
    return status;
}

int vafs_streamdevice_lock(
    struct VaFsStreamDevice* device)
{
    if (!device) {
        errno = EINVAL;
        return -1;
    }

    if (mtx_trylock(&device->Lock) != thrd_success) {
        errno = EBUSY;
        return -1;
    }
    return 0;
}

int vafs_streamdevice_unlock(
    struct VaFsStreamDevice* device)
{
    if (!device) {
        errno = EINVAL;
        return -1;
    }

    if (mtx_unlock(&device->Lock) != thrd_success) {
        errno = ENOTSUP;
        return -1;
    }
    return 0;
}

static int __file_get_size(void* data, uint64_t* sizeOut)
{
    struct VaFsStreamDevice* device = data;
    long                     currentPosition;
    long                     endPosition;

    if (device == NULL || sizeOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    currentPosition = ftell(device->File);
    if (currentPosition < 0) {
        return -1;
    }

    if (fseek(device->File, 0, SEEK_END) != 0) {
        return -1;
    }
    endPosition = ftell(device->File);
    if (endPosition < 0 || fseek(device->File, currentPosition, SEEK_SET) != 0) {
        return -1;
    }

    *sizeOut = (uint64_t)endPosition;
    return 0;
}

static int __file_read_at(void* data, uint64_t offset, void* buffer, size_t length, size_t* bytesRead)
{
    struct VaFsStreamDevice* device = data;

#if defined(_WIN32) || defined(_WIN64)
    HANDLE handle;
    intptr_t osHandle;
    size_t totalRead = 0;

    osHandle = _get_osfhandle(_fileno(device->File));
    if (osHandle == -1) {
        errno = EIO;
        return -1;
    }

    handle = (HANDLE)osHandle;
    while (totalRead < length) {
        OVERLAPPED overlapped;
        uint64_t   currentOffset = offset + (uint64_t)totalRead;
        DWORD      chunkLength = (DWORD)MIN(length - totalRead, (size_t)0xFFFFFFFFu);
        DWORD      chunkRead = 0;

        memset(&overlapped, 0, sizeof(OVERLAPPED));
        overlapped.Offset = (DWORD)(currentOffset & 0xFFFFFFFFu);
        overlapped.OffsetHigh = (DWORD)(currentOffset >> 32);

        if (!ReadFile(handle, (uint8_t*)buffer + totalRead, chunkLength, &chunkRead, &overlapped)) {
            errno = EIO;
            *bytesRead = totalRead;
            return -1;
        }

        totalRead += chunkRead;
        if (chunkRead != chunkLength) {
            break;
        }
    }

    *bytesRead = totalRead;
    if (totalRead != length) {
        errno = EIO;
        return -1;
    }
    return 0;
#else
    int     descriptor;
    ssize_t result;

    descriptor = fileno(device->File);
    if (descriptor < 0) {
        errno = EIO;
        return -1;
    }

    result = pread(descriptor, buffer, length, (off_t)offset);
    if (result < 0) {
        return -1;
    }

    *bytesRead = (size_t)result;
    if ((size_t)result != length) {
        errno = EIO;
        return -1;
    }
    return 0;
#endif
}

static int __file_write(void* data, const void* buffer, size_t length, size_t* bytesWritten)
{
    struct VaFsStreamDevice* device = data;
    *bytesWritten = fwrite(buffer, 1, length, device->File);
    if (*bytesWritten != length) {
        return -1;
    }
    return 0;
}

static int __file_flush(void* data)
{
    struct VaFsStreamDevice* device = data;
    return fflush(device->File);
}

static int __file_close(void* data)
{
    struct VaFsStreamDevice* device = data;
    return fclose(device->File);
}

static int __grow_buffer(
    struct VaFsStreamDevice* device,
    size_t                   length)
{
    void*  buffer;
    size_t newSize;
    
    newSize = (size_t)device->Memory.Capacity + length;
    buffer = realloc(device->Memory.Buffer, newSize);
    if (!buffer) {
        errno = ENOMEM;
        return -1;
    }

    device->Memory.Buffer   = buffer;
    device->Memory.Capacity = (long)newSize;
    return 0;
}

static inline int __memsize_available(
    struct VaFsStreamDevice* device)
{
    return device->Memory.Capacity - device->Memory.Position;
}

static int __memory_get_size(void* data, uint64_t* sizeOut)
{
    struct VaFsStreamDevice* device = data;

    if (device == NULL || sizeOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    *sizeOut = (uint64_t)device->Memory.Size;
    return 0;
}

static int __memory_read_at(void* data, uint64_t offset, void* buffer, size_t length, size_t* bytesRead)
{
    struct VaFsStreamDevice* device = data;
    size_t                   byteCount;

    if (offset > (uint64_t)device->Memory.Size) {
        errno = EINVAL;
        return -1;
    }

    byteCount = MIN(length, (size_t)((uint64_t)device->Memory.Size - offset));
    memcpy(buffer, device->Memory.Buffer + offset, byteCount);
    *bytesRead = byteCount;

    if (byteCount != length) {
        errno = EIO;
        return -1;
    }
    return 0;
}

static int __memory_write(void* data, const void* buffer, size_t length, size_t* bytesWritten)
{
    struct VaFsStreamDevice* device = data;

    // if the stream is a memory stream, then ensure enough space in buffer
    while (length > __memsize_available(device)) {
        if (__grow_buffer(device, length - __memsize_available(device))) {
            return -1;
        }
    }

    memcpy(device->Memory.Buffer + device->Memory.Position, buffer, length);
    device->Memory.Position += (long)length;

    // Keep track of the number of valid bytes in the memory stream.
    if (device->Memory.Position > device->Memory.Size) {
        device->Memory.Size = device->Memory.Position;
    }

    *bytesWritten = length;
    return 0;
}

static int __memory_close(void* data)
{
    struct VaFsStreamDevice* device = data;

    if (device->Memory.Owned) {
        free(device->Memory.Buffer);
    }
    return 0;
}
