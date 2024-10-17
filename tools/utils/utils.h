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
 */

#ifndef __VAFS_UTILS_H__
#define __VAFS_UTILS_H__

#include "list.h"

// detect architecture
#if defined(__x86_64__) || defined(_M_X64)
#define __ARCHITECTURE_STR "amd64"
#elif defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
#define __ARCHITECTURE_STR "i386"
#elif defined(__ARM_ARCH_2__)
#define __ARCHITECTURE_STR "arm2"
#elif defined(__ARM_ARCH_3__) || defined(__ARM_ARCH_3M__)
#define __ARCHITECTURE_STR "arm3"
#elif defined(__ARM_ARCH_4T__) || defined(__TARGET_ARM_4T)
#define __ARCHITECTURE_STR "arm4t"
#elif defined(__ARM_ARCH_5_) || defined(__ARM_ARCH_5E_)
#define __ARCHITECTURE_STR "arm5"
#elif defined(__ARM_ARCH_6T2_) || defined(__ARM_ARCH_6T2_)
#define __ARCHITECTURE_STR "arm6t2"
#elif defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) || defined(__ARM_ARCH_6K__) || defined(__ARM_ARCH_6Z__) || defined(__ARM_ARCH_6ZK__)
#define __ARCHITECTURE_STR "arm6"
#elif defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7S__)
#define __ARCHITECTURE_STR "arm7"
#elif defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7S__)
#define __ARCHITECTURE_STR "arm7a"
#elif defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7S__)
#define __ARCHITECTURE_STR "arm7r"
#elif defined(__ARM_ARCH_7M__)
#define __ARCHITECTURE_STR "arm7m"
#elif defined(__ARM_ARCH_7S__)
#define __ARCHITECTURE_STR "arm7s"
#elif defined(__aarch64__) || defined(_M_ARM64)
#define __ARCHITECTURE_STR "arm64"
#elif defined(mips) || defined(__mips__) || defined(__mips)
#define __ARCHITECTURE_STR "mips"
#elif defined(__sh__)
#define __ARCHITECTURE_STR "superh"
#elif defined(__powerpc) || defined(__powerpc__) || defined(__powerpc64__) || defined(__POWERPC__) || defined(__ppc__) || defined(__PPC__) || defined(_ARCH_PPC)
#define __ARCHITECTURE_STR "powerpc"
#elif defined(__PPC64__) || defined(__ppc64__) || defined(_ARCH_PPC64)
#define __ARCHITECTURE_STR "powerpc64"
#elif defined(__sparc__) || defined(__sparc)
#define __ARCHITECTURE_STR "sparc"
#elif defined(__m68k__)
#define __ARCHITECTURE_STR "m68k"
#elif defined(__riscv32)
#define __ARCHITECTURE_STR "riscv32"
#elif defined(__riscv64)
#define __ARCHITECTURE_STR "riscv64"
#else
#define __ARCHITECTURE_STR "unknown"
#endif

// include dirent.h for directory operations
#if defined(_WIN32) || defined(_WIN64)
#define __PATH_SEPARATOR '\\'
#else
#define __PATH_SEPARATOR '/'
#endif

struct platform_string_item {
    struct list_item list_header;
    const char*      value;
};

enum platform_filetype {
    PLATFORM_FILETYPE_DIRECTORY,
    PLATFORM_FILETYPE_FILE,
    PLATFORM_FILETYPE_SYMLINK,
    PLATFORM_FILETYPE_UNKNOWN
};

struct platform_file_entry {
    struct list_item       list_header;
    char*                  name;
    enum platform_filetype type;
    char*                  path;
    char*                  sub_path;
};

#define FILTER_FOLDCASE 0x1

/**
 * @brief checks a given filter against the given text
 */
extern int strfilter(const char* filter, const char* text, int flags);

/**
 * @brief Retrieves a list of all files (optionally recursive) in the given path
 * and stores them in the list. 
 * The list of entries will be in the format of list<platform_file_entry>.
 * Call platform_getfiles_destroy() on the list to clean it up and correctly
 * free resources allocated with the call.
 */
extern int utils_getfiles(const char* path, int recursive, struct list* files);
extern int utils_getfiles_destroy(struct list* files);

#endif //!__VAFS_UTILS_H__
