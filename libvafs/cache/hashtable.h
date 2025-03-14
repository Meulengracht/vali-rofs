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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * - Open addressed hashtable implementation using round robin for balancing.
 */

#ifndef __VAFS_HASHTABLE_H__
#define __VAFS_HASHTABLE_H__

#include <stdint.h>
#include <stddef.h>

#define HASHTABLE_LOADFACTOR_GROW   75 // Equals 75 percent load
#define HASHTABLE_LOADFACTOR_SHRINK 20 // Equals 20 percent load
#define HASHTABLE_MINIMUM_CAPACITY  16

// HashFn must return a 64-bit hash for the element given. The hash does
// not need to be unique, but if two identical hashes should occur, then 
// CmpFn will be invoked on the two objects that share hash
typedef uint64_t (*hashtable_hashfn)(const void* element);

// CmpFn is used to compare the equality of two objects that share the same
// hash returned by HashFn. This function is expected to return zero if the objects
// are identical, and thus should replace each other. Otherwise should return
// non-zero to identicate the objects are not identical.
typedef int (*hashtable_cmpfn)(const void* lh, const void* rh);

// EnumFn is the signature of a enumerate callback.
typedef void (*hashtable_enumfn)(int index, const void* element, void* userContext);

typedef struct hashtable {
    size_t capacity;
    size_t element_count;
    size_t grow_count;
    size_t shrink_count;
    size_t element_size;
    void*  swap;
    void*  elements;

    hashtable_hashfn hash;
    hashtable_cmpfn  cmp;
} hashtable_t;

/**
 * Constructs a new hashtable that can be used to store and retrieve elements. The hashtable is constructed
 * in such a way that variable sized elements are supported, and the allows for inline keys in the element.
 * @param hashtable       The hashtable pointer that will be initialized.
 * @param requestCapacity The initial capacity of the hashtable, will automatically be set to HASHTABLE_MINIMUM_CAPACITY if less.
 * @param elementSize     The size of the elements that will be stored in the hashtable.
 * @param hashFunction    The hash function that will be used to hash the element data.
 * @param cmpFunction     The function that will be invoked when comparing the keys of two elements.
 * @return                Status of the hashtable construction.
 */
int vafs_hashtable_construct(hashtable_t* hashtable, size_t requestCapacity, size_t elementSize, hashtable_hashfn hashFunction, hashtable_cmpfn cmpFunction);

/**
 * Destroys the hashtable and frees up any resources previously allocated. The structure itself is not freed.
 * @param hashtable The hashtable to cleanup.
 */
void vafs_hashtable_destroy(hashtable_t* hashtable);

/**
 * Inserts or replaces the element with the calculated hash. 
 * @param hashtable The hashtable the element should be inserted into.
 * @param element   The element that should be inserted into the hashtable.
 * @return          The replaced element is returned, or NULL if element was inserted.
 */
void* vafs_hashtable_set(hashtable_t* hashtable, const void* element);

/**
 * Retrieves the element with the corresponding key.
 * @param hashtable The hashtable to use for the lookup.
 * @param key       The key to retrieve an element for.
 * @return          A pointer to the object.
 */
void* vafs_hashtable_get(hashtable_t* hashtable, const void* key);

/**
 * Removes the element from the hashtable with the given key.
 * @param hashtable The hashtable to remove the element from.
 * @param key       Key of the element to lookup.
 */
void* vafs_hashtable_remove(hashtable_t* hashtable, const void* key);

/**
 * Enumerates all elements in the hashtable.
 * @param hashtable    The hashtable to enumerate elements in.
 * @param enumFunction Callback function to invoke on each element.
 * @param context      A user-provided callback context.
 */
void vafs_hashtable_enumerate(hashtable_t* hashtable, hashtable_enumfn enumFunction, void* context);

#endif //!__VAFS_HASHTABLE_H__
