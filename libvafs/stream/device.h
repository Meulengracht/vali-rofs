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

#ifndef __VAFS_STREAM_DEVICE_H_
#define __VAFS_STREAM_DEVICE_H_

#include <stdint.h>
#include <stddef.h>

#include <vafs/backend.h>

// Forward declarations
struct VaFsStreamDevice;

/**
 * @brief Opens a file-backed stream device for read access to a VaFS image.
 *
 * @param[In]  path      Path to the backing file on the host filesystem.
 * @param[Out] deviceOut Receives the opened stream device on success.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_open_file(
    const char*               path,
    struct VaFsStreamDevice** deviceOut);

/**
 * @brief Wraps the provided buffer in a streamdevice object. Enabling the use
 * of the entire stremadevice API for the buffer. The buffer must stay valid untill
 * vafs_streamdevice_close has been called. This will not free the buffer.
 *
 * @param[In]  buffer    A pointer to the image buffer that should be used.
 * @param[In]  length    The length of the image buffer.
 * @param[Out] deviceOut A pointer to where to store the handle of the stream device.
 * @return Returns -1 if any error occured, otherwise 0.
 */
extern int vafs_streamdevice_open_memory(
    const void*               buffer,
    size_t                    length,
    struct VaFsStreamDevice** deviceOut);

/**
 * @brief Opens a stream device backed by user-provided callbacks.
 *
 * @param[In]  operations Callback table implementing the device operations.
 * @param[In]  userData   Opaque user context passed back to the callbacks.
 * @param[Out] deviceOut  Receives the opened stream device on success.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_reader_new(
    struct VaFsReaderBackendOps* backend,
    void*                        userData,
    struct VaFsStreamDevice**    deviceOut);
extern int vafs_streamdevice_writer_new(
    struct VaFsBuilderBackendOps* backend,
    void*                         userData,
    struct VaFsStreamDevice**     deviceOut);

/**
 * @brief Creates a writable file-backed stream device.
 *
 * @param[In]  path      Destination file path.
 * @param[Out] deviceOut Receives the created stream device on success.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_create_file(
    const char*               path,
    struct VaFsStreamDevice** deviceOut);

/**
 * @brief Creates an in-memory writable stream device.
 *
 * @param[In]  blockSize Initial allocation granularity used by the memory device.
 * @param[Out] deviceOut Receives the created stream device on success.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_create_memory(
    size_t                    blockSize,
    struct VaFsStreamDevice** deviceOut);

/**
 * @brief Closes a stream device and releases any owned resources.
 *
 * @param[In] device The stream device to close.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_close(
    struct VaFsStreamDevice* device);

/**
 * @brief Reads bytes from a stream device at an absolute offset.
 *
 * This is the read-only fast path used by stream and metadata loaders. Devices
 * with native positioned reads can avoid shared cursor mutations entirely.
 * Devices without that support fall back to an internal seek+read sequence.
 *
 * @param[In]  device    The stream device to read from.
 * @param[In]  offset    Absolute byte offset to read from.
 * @param[Out] buffer    Destination buffer for the bytes read.
 * @param[In]  length    Number of bytes requested.
 * @param[Out] bytesRead Receives the number of bytes actually read.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_read_at(
    struct VaFsStreamDevice* device,
    long                     offset,
    void*                    buffer,
    size_t                   length,
    size_t*                  bytesRead);

/**
 * @brief Writes bytes directly to a stream device.
 *
 * @param[In]  device       The stream device to write to.
 * @param[In]  buffer       Source buffer containing the bytes to write.
 * @param[In]  length       Number of bytes to write.
 * @param[Out] bytesWritten Receives the number of bytes actually written.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_write(
    struct VaFsStreamDevice* device,
    const void*              buffer,
    size_t                   length,
    size_t*                  bytesWritten);

/**
 * @brief Reports the number of bytes currently held by a stream device.
 *
 * Writable devices use this as their append position. Readable devices report
 * the backing image size when their backend supports it.
 *
 * @param[In]  device  The stream device to query.
 * @param[Out] sizeOut Receives the device size in bytes.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_size(
    struct VaFsStreamDevice* device,
    uint64_t*                sizeOut);

/**
 * @brief Copies the complete contents of one stream device into another.
 *
 * @param[In] destination Device to receive the copied bytes.
 * @param[In] source      Device to copy from.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_copy(
    struct VaFsStreamDevice* destination,
    struct VaFsStreamDevice* source);

/**
 * @brief Locks a stream device for exclusive access.
 *
 * @param[In] device The device to lock.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_lock(
    struct VaFsStreamDevice* device);

/**
 * @brief Unlocks a previously locked stream device.
 *
 * @param[In] device The device to unlock.
 * @return 0 on success, otherwise -1 with `errno` set.
 */
extern int vafs_streamdevice_unlock(
    struct VaFsStreamDevice* device);

#endif //!__VAFS_STREAM_DEVICE_H_
