#include "alloc.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#define ALIGNMENT 16 /**< The alignment of the memory blocks */

static free_block *HEAD = NULL; /**< Pointer to the first element of the free list */

/**
 * Split a free block into two blocks
 *
 * @param block The block to split
 * @param size The size of the first new split block
 * @return A pointer to the first block or NULL if the block cannot be split
 */
void *split(free_block *block, int size) {
    if((block->size < size + sizeof(free_block))) {
        return NULL;
    }

    void *split_pnt = (char *)block + size + sizeof(free_block);
    free_block *new_block = (free_block *) split_pnt;

    new_block->size = block->size - size - sizeof(free_block);
    new_block->next = block->next;

    block->size = size;

    return block;
}

/**
 * Find the previous neighbor of a block
 *
 * @param block The block to find the previous neighbor of
 * @return A pointer to the previous neighbor or NULL if there is none
 */
free_block *find_prev(free_block *block) {
    free_block *curr = HEAD;
    while(curr != NULL) {
        char *next = (char *)curr + curr->size + sizeof(free_block);
        if(next == (char *)block)
            return curr;
        curr = curr->next;
    }
    return NULL;
}

/**
 * Find the next neighbor of a block
 *
 * @param block The block to find the next neighbor of
 * @return A pointer to the next neighbor or NULL if there is none
 */
free_block *find_next(free_block *block) {
    char *block_end = (char*)block + block->size + sizeof(free_block);
    free_block *curr = HEAD;

    while(curr != NULL) {
        if((char *)curr == block_end)
            return curr;
        curr = curr->next;
    }
    return NULL;
}

/**
 * Remove a block from the free list
 *
 * @param block The block to remove
 */
void remove_free_block(free_block *block) {
    free_block *curr = HEAD;
    if(curr == block) {
        HEAD = block->next;
        return;
    }
    while(curr != NULL) {
        if(curr->next == block) {
            curr->next = block->next;
            return;
        }
        curr = curr->next;
    }
}

/**
 * Coalesce neighboring free blocks
 *
 * @param block The block to coalesce
 * @return A pointer to the first block of the coalesced blocks
 */
void *coalesce(free_block *block) {
    if (block == NULL) {
        return NULL;
    }

    free_block *prev = find_prev(block);
    free_block *next = find_next(block);

    // Coalesce with previous block if it is contiguous.
    if (prev != NULL) {
        char *end_of_prev = (char *)prev + prev->size + sizeof(free_block);
        if (end_of_prev == (char *)block) {
            prev->size += block->size + sizeof(free_block);

            // Ensure prev->next is updated to skip over 'block', only if 'block' is directly next to 'prev'.
            if (prev->next == block) {
                prev->next = block->next;
            }
            block = prev; // Update block to point to the new coalesced block.
        }
    }

    // Coalesce with next block if it is contiguous.
    if (next != NULL) {
        char *end_of_block = (char *)block + block->size + sizeof(free_block);
        if (end_of_block == (char *)next) {
            block->size += next->size + sizeof(free_block);

            // Ensure block->next is updated to skip over 'next'.
            block->next = next->next;
        }
    }

    return block;
}

/**
 * Call sbrk to get memory from the OS
 *
 * @param size The amount of memory to allocate
 * @return A pointer to the allocated memory
 */
void *do_alloc(size_t size) {
    void *block = sbrk(size + sizeof(free_block));
    if (block == (void *)-1) {
        return NULL;
    }
    
    free_block *new_block = (free_block *)block;
    new_block->size = size;
    new_block->next = HEAD;
    HEAD = new_block;

    HEAD = (free_block *)coalesce(HEAD);

    return (void *)((char *)new_block + sizeof(free_block));
}

// extra cred: this is the pointer to the last allocated block
static free_block *next_fit_ptr = NULL;



/**
 * Allocates memory for the end user
 *
 * @param size The amount of memory to allocate
 * @return A pointer to the requested block of memory
 */
void *tumalloc(size_t size) {
    // Print debug information
    printf("Allocating %d bytes\n", size);
    printf("Next fit ptr: %p\n", next_fit_ptr);

    // Align the size to the nearest multiple of ALIGNMENT
    size = (size + ALIGNMENT - 1) & ~(ALIGNMENT - 1);

    // Start searching for a free block from the next fit pointer
    free_block *curr = next_fit_ptr ? next_fit_ptr : HEAD;
    free_block *prev = NULL;

    // Traverse the free list to find a block that fits the requested size
    while (curr != NULL) {
        // Check if the current block is large enough
        if (curr->size >= size) {
            // Split the block if it's larger than the requested size
            if (curr->size > size + sizeof(free_block)) {
                split(curr, size);
            }

            // Remove the block from the free list
            if (prev != NULL) {
                prev->next = curr->next;
            } else {
                HEAD = curr->next;
            }

            // Update the next fit pointer
            next_fit_ptr = curr->next ? curr->next : HEAD;

            // Print debug information
            printf("allocated memory: %p\n", (void *)(curr + 1));

            // Return the allocated memory
            return (void *)(curr + 1);
        }

        // Move to the next block in the free list
        prev = curr;
        curr = curr->next;
    }

    // If no free block is found, allocate new memory from the OS
    free_block *new_block = (free_block *)sbrk(size + sizeof(free_block));
    if (new_block == (void *)-1) {
        // Print error message if allocation fails
        printf("Failed to allocate memory\n");
        return NULL;
    }

    // Initialize the new block
    new_block->size = size;
    new_block->next = HEAD;

    // Reset the next fit pointer
    next_fit_ptr = NULL;

    // Print debug information
    printf("allocated memory: %p\n", (void *)(new_block + 1));

    // Return the allocated memory
    return (void *)(new_block + 1);
}

/**
 * Allocates and initializes a list of elements for the end user
 *
 * @param num How many elements to allocate
 * @param size The size of each element
 * @return A pointer to the requested block of initialized memory
 */
void *tucalloc(size_t num, size_t size) {
    // Check for overflow to prevent multiplication from wrapping around
    if (num > SIZE_MAX / size) {
        // If overflow would occur, return NULL to indicate failure
        return NULL;
    }

    // Calculate the total size of the memory to be allocated
    size_t total_size = num * size;

    // Allocate the memory using tumalloc
    void *ptr = tumalloc(total_size);

    // Check if the allocation was successful
    if (ptr != NULL) {
        // Initialize the allocated memory to zero using memset
        memset(ptr, 0, total_size);
    }

    // Return the pointer to the allocated and initialized memory
    return ptr;
}

/**
 * Reallocates a chunk of memory with a bigger size
 *
 * @param ptr A pointer to an already allocated piece of memory
 * @param new_size The new requested size to allocate
 * @return A new pointer containing the contents of ptr, but with the new_size
 */
void *turealloc(void *ptr, size_t new_size) {
    if(!ptr) {
        return tumalloc(new_size);
    }

    free_block *block = (free_block *)ptr - 1;

    if (block->size >= new_size) {
        return ptr;
    }

    void *new_ptr = tumalloc(new_size);
    if(new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        tufree(ptr);
    }

    return new_ptr;
}

/**
 * Removes used chunk of memory and returns it to the free list
 *
 * @param ptr Pointer to the allocated piece of memory
 */
void tufree(void *ptr) {

}
