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
#include "private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32) || defined(_WIN64)
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#define __TRANSFER_BUFFER_SIZE 1024*1024

static long __file_seek(void*, long, int);
static int  __file_read(void*, void*, size_t, size_t*);
static int  __file_read_at(void*, long, void*, size_t, size_t*);
static int  __file_write(void*, const void*, size_t, size_t*);
static int  __file_close(void*);

static long __memory_seek(void*, long, int);
static int  __memory_read(void*, void*, size_t, size_t*);
static int  __memory_read_at(void*, long, void*, size_t, size_t*);
static int  __memory_write(void*, const void*, size_t, size_t*);
static int  __memory_close(void*);

static struct VaFsOperations g_fileOperations = {
    .seek = __file_seek,
    .read = __file_read,
    .readAt = __file_read_at,
    .write = __file_write,
    .close = __file_close
};
static struct VaFsOperations g_memoryOperations = {
    .seek = __memory_seek,
    .read = __memory_read,
    .readAt = __memory_read_at,
    .write = __memory_write,
    .close = __memory_close
};

struct VaFsStreamDevice {
    int                   ReadOnly;
    mtx_t                 Lock;
    struct VaFsOperations Operations;
    void*                 UserData;

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
};

static int __validate_ops(
    struct VaFsOperations* operations,
    int                    readOnly)
{
    if (readOnly) {
        if (operations->readAt == NULL && (operations->read == NULL || operations->seek == NULL)) {
            errno = EINVAL;
            return -1;
        }
    }
    else if (operations->read == NULL || operations->seek == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (!readOnly && operations->write == NULL) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int __new_streamdevice(
    int                       readOnly,
    void*                     userData,
    struct VaFsOperations*    operations,
    struct VaFsStreamDevice** deviceOut)
{
    struct VaFsStreamDevice* device;

    // Validate the operations provided, based on the read-only status
    // of the vafs image, there must be some of the operations set. The
    // minimum is seek/read, but if it's not read-only, then write must
    // also be provided. Close is always optional.
    if (__validate_ops(operations, readOnly)) {
        return -1;
    }
    
    device = (struct VaFsStreamDevice*)malloc(sizeof(struct VaFsStreamDevice));
    if (!device) {
        errno = ENOMEM;
        return -1;
    }

    memset(device, 0, sizeof(struct VaFsStreamDevice));
    memcpy(&device->Operations, operations, sizeof(struct VaFsOperations));

    mtx_init(&device->Lock, mtx_plain);
    device->ReadOnly = readOnly;
    device->UserData = userData;

    *deviceOut = device;
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

    status = __new_streamdevice(1, NULL, &g_fileOperations, &device);
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

    status = __new_streamdevice(1, NULL, &g_memoryOperations, &device);
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

int vafs_streamdevice_open_ops(
    struct VaFsOperations*    operations,
    void*                     userData,
    struct VaFsStreamDevice** deviceOut)
{
    struct VaFsStreamDevice* device;
    int                      status;

    if (operations == NULL || deviceOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = __new_streamdevice(1, userData, operations, &device);
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

    status = __new_streamdevice(0, NULL, &g_fileOperations, &device);
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

    status = __new_streamdevice(0, NULL, &g_memoryOperations, &device);
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

    if (device->Operations.close) {
        device->Operations.close(device->UserData);
    }

    mtx_destroy(&device->Lock);
    free(device);
    return 0;
}

long vafs_streamdevice_seek(
    struct VaFsStreamDevice* device,
    long                     offset,
    int                      whence)
{
    VAFS_DEBUG("vafs_streamdevice_seek(offset=%ld, whence=%i)\n", offset, whence);
    if (device == NULL) {
        errno = EINVAL;
        return -1;
    }
    return device->Operations.seek(device->UserData, offset, whence);
}

int vafs_streamdevice_read(
    struct VaFsStreamDevice* device,
    void*                    buffer,
    size_t                   length,
    size_t*                  bytesRead)
{
    if (device == NULL || buffer == NULL || length == 0 || bytesRead == NULL) {
        errno = EINVAL;
        return -1;
    }
    return device->Operations.read(device->UserData, buffer, length, bytesRead);
}

int vafs_streamdevice_read_at(
    struct VaFsStreamDevice* device,
    long                     offset,
    void*                    buffer,
    size_t                   length,
    size_t*                  bytesRead)
{
    long original;
    int  status;

    if (device == NULL || buffer == NULL || length == 0 || bytesRead == NULL || offset < 0) {
        errno = EINVAL;
        return -1;
    }

    if (device->Operations.readAt != NULL) {
        return device->Operations.readAt(device->UserData, offset, buffer, length, bytesRead);
    }

    if (device->Operations.read == NULL || device->Operations.seek == NULL) {
        errno = ENOTSUP;
        return -1;
    }

    if (mtx_lock(&device->Lock) != thrd_success) {
        errno = EBUSY;
        return -1;
    }

    original = device->Operations.seek(device->UserData, 0, SEEK_CUR);
    if (original < 0) {
        mtx_unlock(&device->Lock);
        return -1;
    }

    if (device->Operations.seek(device->UserData, offset, SEEK_SET) < 0) {
        mtx_unlock(&device->Lock);
        return -1;
    }

    status = device->Operations.read(device->UserData, buffer, length, bytesRead);
    if (device->Operations.seek(device->UserData, original, SEEK_SET) < 0 && status == 0) {
        status = -1;
    }

    if (mtx_unlock(&device->Lock) != thrd_success && status == 0) {
        errno = ENOTSUP;
        return -1;
    }
    return status;
}

int vafs_streamdevice_write(
    struct VaFsStreamDevice* device,
    void*                    buffer,
    size_t                   length,
    size_t*                  bytesWritten)
{
    VAFS_DEBUG("vafs_streamdevice_write(length=%zu)\n", length);
    if (device == NULL || buffer == NULL || length == 0 || bytesWritten == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (device->ReadOnly) {
        errno = EACCES;
        return -1;
    }
    return device->Operations.write(device->UserData, buffer, length, bytesWritten);
}

int vafs_streamdevice_copy(
    struct VaFsStreamDevice* destination,
    struct VaFsStreamDevice* source)
{
    char*  transferBuffer;
    int    status = 0;
    size_t bytesRead;
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

    // seek source back to start
    status = source->Operations.seek(source->UserData, 0, SEEK_SET);
    if (status) {
        VAFS_ERROR("vafs_streamdevice_copy failed to seek source back to beginning\n");
        return -1;
    }

    // copy all contents of source to destination using an intermediate buffer
    // of size __TRANSFER_BUFFER_SIZE.
    do {
        size_t bytesWritten;

        status = source->Operations.read(source->UserData, transferBuffer, __TRANSFER_BUFFER_SIZE, &bytesRead);
        VAFS_DEBUG("vafs_streamdevice_copy read %zu bytes\n", bytesRead);
        if (status || bytesRead == 0) {
            break;
        }

        status = destination->Operations.write(destination->UserData, transferBuffer, bytesRead, &bytesWritten);
        VAFS_DEBUG("vafs_streamdevice_copy wrote %zu bytes\n", bytesWritten);
        if (status || bytesWritten != bytesRead) {
            break;
        }
    } while (1);

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

static long __file_seek(void* data, long offset, int whence)
{
    struct VaFsStreamDevice* device = data;

    if (offset == 0 && whence == SEEK_CUR) {
        return ftell(device->File);
    }

    int status = fseek(device->File, offset, whence);
    if (status != 0) {
        return -1;
    }
    return ftell(device->File);
}

static int __file_read(void* data, void* buffer, size_t length, size_t* bytesRead)
{
    struct VaFsStreamDevice* device = data;
    *bytesRead = fread(buffer, 1, length, device->File);
    if (*bytesRead != length) {
        return -1;
    }
    return 0;
}

static int __file_read_at(void* data, long offset, void* buffer, size_t length, size_t* bytesRead)
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
        uint64_t   currentOffset = (uint64_t)(uint32_t)offset + (uint64_t)totalRead;
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

static long __memory_seek(void* data, long offset, int whence)
{
    struct VaFsStreamDevice* device = data;

    if (offset == 0 && whence == SEEK_CUR) {
        return device->Memory.Position;
    }

    switch (whence) {
        case SEEK_SET:
            device->Memory.Position = offset;
            break;
        case SEEK_CUR:
            device->Memory.Position += offset;
            break;
        case SEEK_END:
            device->Memory.Position = device->Memory.Size + offset;
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    device->Memory.Position = MIN(MAX(device->Memory.Position, 0), device->Memory.Size);
    return device->Memory.Position;
}

static int __memory_read(void* data, void* buffer, size_t length, size_t* bytesRead)
{
    struct VaFsStreamDevice* device = data;
    size_t byteCount = MIN(length, (size_t)(device->Memory.Size - device->Memory.Position));
    memcpy(buffer, device->Memory.Buffer + device->Memory.Position, byteCount);
    device->Memory.Position += (long)byteCount;
    *bytesRead = byteCount;
    return 0;
}

static int __memory_read_at(void* data, long offset, void* buffer, size_t length, size_t* bytesRead)
{
    struct VaFsStreamDevice* device = data;
    size_t                   byteCount;

    if (offset < 0 || offset > device->Memory.Size) {
        errno = EINVAL;
        return -1;
    }

    byteCount = MIN(length, (size_t)(device->Memory.Size - offset));
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
