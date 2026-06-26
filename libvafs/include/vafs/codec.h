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
 * - Declares the draft codec registration surface for the next-generation VaFS public API.
 */

#ifndef __VAFS_CODEC_H__
#define __VAFS_CODEC_H__

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Encodes one logical payload buffer.
 *
 * The encoder allocates the output buffer and returns ownership to the caller on success.
 */
typedef int(*VaFsCodecEncodeFunc)(const void* input, size_t inputLength, void** output, size_t* outputLength);

/**
 * @brief Decodes one stored payload buffer.
 *
 * The decoder writes into a caller-provided destination buffer and reports the number of decoded
 * bytes that were produced.
 */
typedef int(*VaFsCodecDecodeFunc)(const void* input, size_t inputLength, void* output, size_t outputLength, size_t* bytesWrittenOut);

/**
 * @brief Stable codec descriptor used by readers and builders.
 *
 * `ID` must match the on-disk section codec id.
 * `Encode` may be NULL for read-only registries. 
 * `Decode` may be NULL for write-only registries, but readers must reject 
 *          images whose required codecs do not provide decode support.
 */
struct VaFsCodec {
    const char*         ID;
    VaFsCodecEncodeFunc Encode;
    VaFsCodecDecodeFunc Decode;
};

#endif //!__VAFS_CODEC_H__