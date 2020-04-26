#include <stdlib.h>
#include <stdio.h>
#include "heaplib.h"
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

#define ADD_BYTES(base_addr, num_bytes) (((char *)(base_addr)) + (num_bytes))

typedef struct _block_info_t {
    unsigned int block_size;
} block_info_t;

typedef struct _heap_header_t {
    unsigned int heap_size;
    unsigned int padding;
    unsigned long first_free;
} heap_header_t;

/* See the .h for the advertised behavior of this library function.
 * These comments describe the implementation, not the interface.
 *
 * Checks if size >= MIN_HEAP_SIZE.
 * Initializes the heap size as the argument heap_size.
 * Inserts information on block size and status bit in the first and end blocks.
 */
int hl_init(void *heap, unsigned int heap_size) {
    if (heap_size < MIN_HEAP_SIZE) {
        return FAILURE;
    }

    int padding = 8 - (((long) heap) + 4) % 8;
    heap_header_t * header = (heap_header_t *) heap;
    header->heap_size = (heap_size - padding - 16) / 8 * 8;
    header->padding = padding;
    header->first_free = (long) ADD_BYTES(heap, padding + 16);

    block_info_t * first = (block_info_t *) ADD_BYTES(heap, padding + 16);
    first->block_size = header->heap_size;
    block_info_t * end = (block_info_t *) ADD_BYTES(first, ((int) first->block_size) - 4);
    end->block_size = header->heap_size;

    return SUCCESS;
}

/* See the .h for the advertised behavior of this library function.
 * These comments describe the implementation, not the interface.
 *
 * Looks through the blocks to see if there's one that's free.
 * If it's found with the block_size, initialize the block and return the location of the block to the user.
 * If not found, return NULL.
 */
void *hl_alloc(void *heap, unsigned int block_size) {
    heap_header_t * header = (heap_header_t *) heap;
    unsigned int heap_size = header->heap_size;
    unsigned int padding = header->padding;
    block_info_t * empty = (block_info_t *) header->first_free;
    heap = ADD_BYTES(heap, padding + 16);
    char *end = ADD_BYTES(heap, heap_size);
    if (block_size % 8 != 0) {
        block_size += 8 - block_size % 8;
    }

    while ((((char*)empty) < end) && (empty->block_size % 2 != 0 || (((int) empty->block_size) - 8) < ((int) block_size))) {
        empty = (block_info_t *) ADD_BYTES(empty, empty->block_size/2*2);
    }
    if (((char*)empty) < end) {
        unsigned int next_size = empty->block_size - block_size - 8;
        if (next_size < 16) {
            block_info_t * footer = (block_info_t *) ADD_BYTES(empty, empty->block_size - 4);
            empty->block_size += 1;
            footer->block_size += 1;
            if (((long) empty) == header->first_free) {
                block_info_t * next_empty = (block_info_t *) ADD_BYTES(empty, empty->block_size - 1);
                while ((((char*)next_empty) < end) && (next_empty->block_size % 2 != 0)) {
                    next_empty = (block_info_t *) ADD_BYTES(next_empty, next_empty->block_size - 1);
                }
                header->first_free = (long) next_empty;
            }
        }
        else {
            empty->block_size = block_size + 9;
            block_info_t * footer = (block_info_t *) ADD_BYTES(empty, empty->block_size - 5);
            footer->block_size = empty->block_size;
            block_info_t * next = (block_info_t *) ADD_BYTES(empty, empty->block_size - 1);
            next->block_size = next_size;
            block_info_t * next_footer = (block_info_t *) ADD_BYTES(next, next->block_size - 4);
            next_footer->block_size = next_size;
            if (((long) empty) == header->first_free) {
                header->first_free = (long) next;
            }
        }
        return ADD_BYTES(empty, 4);
    }

    return NULL;
}

/* See the .h for the advertised behavior of this library function.
 * These comments describe the implementation, not the interface.
 *
 * If block == 0, act as a NOP.
 * Change the header and footer's valid bit to 0 to release the block.
 * Merge the next block if possible and update the information on the block size accordingly.
 */
void hl_release(void *heap, void *block) {
    if (block == 0) {
        return;
    }

    heap_header_t * heap_header = (heap_header_t *) heap;
    block_info_t * first_free = (block_info_t *) heap_header->first_free;
    block_info_t * first = (block_info_t *) ADD_BYTES(heap, heap_header->padding + 16);
    block_info_t * last = (block_info_t *) ADD_BYTES(first, heap_header->heap_size);

    block_info_t * header = (block_info_t *) ADD_BYTES(block, -4);
    header->block_size -= 1;
    block_info_t * footer = (block_info_t *) ADD_BYTES(header, header->block_size - 4);
    footer->block_size -= 1;

    if (((block_info_t *) ADD_BYTES(footer, 4)) < last) {
        block_info_t * next_header = (block_info_t *) ADD_BYTES(footer, 4);
        if (next_header->block_size % 2 == 0) {
            block_info_t * next_footer = (block_info_t *) ADD_BYTES(next_header, ((int) next_header->block_size) - 4);
            unsigned int new_block_size = next_header->block_size + header->block_size;
            next_footer->block_size = new_block_size;
            header->block_size = new_block_size;
        }
    }

    if (header < first_free) {
        heap_header->first_free = (long) header;
    }
}

/* See the .h for the advertised behavior of this library function.
 * These comments describe the implementation, not the interface.
 *
 * If block == 0, call hl_alloc to behave like regular alloc.
 * If resizing to a smaller size, act as a NOP.
 * If resizing to a bigger size, call alloc to find a new location for new_size.
 * Change the information on the new block accordingly and free the original block.  
 */
void *hl_resize(void *heap, void *block, unsigned int new_size) {
    if (block == 0) {
        return hl_alloc(heap, new_size);
    }

    block_info_t * header = (block_info_t *) ADD_BYTES(block, -4);
    if (new_size % 8 != 0) {
        new_size += 8 - new_size % 8;
    }
    if ((((int) header->block_size) - 9) >= ((int) new_size)) {
        return block;
    }

    block_info_t * new_header = hl_alloc(heap, new_size);
    if (new_header == 0) {
        return NULL;
    }
    memmove(new_header, block, ((int) header->block_size) - 8);
    hl_release(heap, block);
    return new_header;
}
