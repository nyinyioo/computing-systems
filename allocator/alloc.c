#include <string.h>
#include "alloc.h"
#include "common.h"

/* 
    Implements a simple allocator that manages a heap. 

    Boundary Tags: 
        - Each block of memory has a header and footer tag that store the size of the freeblock and the allocation status (0 = free or 1 = allocated).
        - Aligned on 8-byte boundaries, the size is stored in bits [31:3] and the allocation status is stored in bits [2:0].

        +-------------------------------+--------+
        | Size (bits 31:3)              |  |  |  |
        +-------------------------------+--------+
                                          2  1  0 
                                                |-- allocation status (0 = free, 1 = allocated)
           
    Free List:
        The allocator maintains uses a first-fit strategy to find suitable blocks for allocation.
        The allocator traverses the heap to find free blocks using implicit free list structure.

    Coalescing: 
        When a block is freed, the allocator checks the adjacent blocks (previous and next) to see if they are also free. 
        If so, it merges them into a single larger free block to reduce fragmentation.


    The allocator provides the following functions:
        - `init`: initializes the allocator and sets up the initial free block covering the entire heap.
        - `heap_alloc`: allocates a block of at least `nbytes` bytes and returns a pointer to the allocated block.
        - `heap_free`: deallocates a previously allocated block of memory.
        - `heap_realloc`: resizes a previously allocated block to at least N bytes.
*/

// helpers ------------------------------------------------------------------------------------------
int printf(const char *format, ...);


// helper to set allocation status in a header/footer tag.
static void set_tag(boundary_tags *tag, size_t size, uint32_t alloc){ 
    // cast size to uint32_t: size_t is 64 bits, tag->data is 32 bits
    // zero out lower 3 bits and mask alloc to fit in lower 3 bits
    tag->data = ((uint32_t)size <<3) | (alloc & 0x7);
}

// helper to extract the size from a boundary tag.
static size_t tag_size (boundary_tags *t) { 
    // shift right by 3 bits ~ size [31:3] 
    return t->data >> 3; 
}

// helper to extract the allocation status from a boundary tag.
static uint32_t tag_alloc(boundary_tags *t) { 
    // mask lower 3 bits to get allocation status
    return t->data & 0x7; 
}

// return the footer tag given a block's header pointer and block size.
static boundary_tags *get_footer(boundary_tags *header, size_t block_size) {
    // move byte by byte char ( 1byte) and then cast to boundary_tags pointer (4 bytes)
    return (boundary_tags *)((char *)header + block_size + TAG_SIZE);
}

// return the header tag given a block's footer pointer and block size.
// need to check ptr arithmetic 
static boundary_tags *get_header(boundary_tags *footer, size_t block_size) {
    // move byte by byte char ( 1byte) and then cast to boundary_tags pointer (4 bytes)
    return (boundary_tags *)((char *)footer - block_size - TAG_SIZE);
}

// alloc functions ----------------------------------------------------------------------------------

// initialize the allocator and returns a ptr to start of the free block.
// allocator struct | padding | header tag | free block | footer tag 
void *init(void *heap_start, void *heap_end) {

    // set up allocator struct at the start of the heap
    alloc_state_t *s = (alloc_state_t *)heap_start;
    s->end = heap_end;  // set end ptr 

    // base is after the allocator struct, 8 byte aligned
    char *base = (char *)heap_start + sizeof(alloc_state_t);

    // type cast to uintptr_t for pointer arithmetic and alignment
    uintptr_t b = (uintptr_t)base;
    
    // Note: (x + N-1) & ~(N-1) rounds x up to the next multiple of N
    // after the allocator struct and the header tag
    // aligned is first block space 8 byte aligned
    uintptr_t aligned = (b + TAG_SIZE + 7) & ~(uintptr_t)7;

    // header of the first block
    char *first_block = (char *)(aligned - TAG_SIZE);
    
    // free list starts at the first block
    s->free = first_block;

    // calculate bytes avaliable from first block to the end of the heap
    size_t heap_size = (char *)heap_end - first_block;

    // space check: at least one block with header and footer tags
    if (heap_size < 2 * TAG_SIZE + 8) return NULL;

    // calculate the payload size for the first block, ensuring 8 byte alignment
    size_t block_size = heap_size - 2 * TAG_SIZE;
    block_size &= ~(size_t)7;

    // space check: at least one block with header and footer tags
    if (block_size < 8) return NULL;

    // update end ptr to the end of the heap
    s->end = first_block + 2 * TAG_SIZE + block_size;   

    // set the header tag for the initial free block
    boundary_tags *header = (boundary_tags *)first_block;

    set_tag(header, block_size, 0); // header tag
    set_tag(get_footer(header, block_size), block_size, 0); // footer tag

    // return allocator state 
    return s;
}


// allocate a block of at least N bytes and return a pointer to the allocated block.
void *heap_alloc(void *alloc_state, size_t nbytes) {

    // if allocate 0 bytes, return NULL
    if (nbytes == 0) {return NULL;}
    alloc_state_t *s = (alloc_state_t *)alloc_state;

    // calculate the total block size needed, 
    // including header and footer tags, 
    // and ensuring 8 byte alignment for the freeblock.
    size_t payload = (nbytes + 7) & ~(size_t)7;
    size_t needed  = payload + 2 * TAG_SIZE;

    // traverse the heap to find a suitable free block
    char *ptr = (char *)s->free;
    char *end = (char *)s->end;

    // first-fit: traverse the heap until we reach the end or find a suitable free block 
    while (ptr + 2 * TAG_SIZE <= end) {

        // get the header of the current block
        boundary_tags *header = (boundary_tags *)ptr;

        // get the payload size and total block size of the current block
        size_t blk_payload = tag_size(header);
        size_t blk_total   = blk_payload + 2 * TAG_SIZE;

        // case 1: if the block is free and large enough, allocate it
        if (!tag_alloc(header) && blk_total >= needed) {
            size_t leftover = blk_total - needed;

            // case 1: block splitting logic
            if (leftover >= 2 * TAG_SIZE + 8) {
                
                set_tag(header, payload, 1);
                set_tag(get_footer(header, payload), payload, 1);

                boundary_tags *nheader  = (boundary_tags *)(ptr + needed);
                size_t         new_payload = leftover - 2 * TAG_SIZE;
                set_tag(nheader, new_payload, 0);
                set_tag(get_footer(nheader, new_payload), new_payload, 0);
            } 
            

            // case 2: if block is not large enough to split, allocate the entire block
            else {
                set_tag(header, blk_payload, 1);
                set_tag(get_footer(header, blk_payload), blk_payload, 1);
            }
            // return newly allocated block's head
            return ptr + TAG_SIZE;  
        }
        // advance pointer to the next block
        ptr += blk_total;
    }

    // case 3: no block large enough 
    return NULL;  
}


// deallocate a previously allocated block of memory and coalesces adjacent free blocks.
void heap_free(void *alloc_state, void *p) {

    // if p is NULL, return immediately
    if (!p) return;

    // get the allocator state and heap boundaries
    alloc_state_t *s = (alloc_state_t *)alloc_state;
    char *heap_start = (char *)s->free;
    char *heap_end   = (char *)s->end;

    // get the header of the block from the payload pointer p
    boundary_tags *header = (boundary_tags *)((char *)p - TAG_SIZE);

    // get the payload size from the header
    size_t payload = tag_size(header);

    // set the alloc status to indicate free ~ (0) 
    // in both the header and footer tags
    set_tag(header, payload, 0);
    set_tag(get_footer(header, payload), payload, 0);

    // coalescing logic------------------------------------------------------------------ 

    // check the next block's header tag
    char *next_header_addr = (char *)get_footer(header, payload) + TAG_SIZE;

    // if next block exists
    if (next_header_addr + TAG_SIZE <= heap_end) {
        boundary_tags *next_header = (boundary_tags *)next_header_addr;

        // and if next block is free
        if (!tag_alloc(next_header)){

            // get the size of the next block
            size_t next_payload = tag_size(next_header);

            // calculate the size of merged block 
            // current_size + next_size + 2* TAG_SIZE
            size_t merged_payload = payload + 2 * TAG_SIZE + next_payload;

            // set new header and footer tags for the merged block
            set_tag(header, merged_payload, 0);
            set_tag(get_footer(header, merged_payload), merged_payload, 0);

            // update payload to the merged payload size 
            payload = merged_payload;
        }
    }
    // check the previous block's footer tag 
    char *prev_footer_addr = (char *)header - TAG_SIZE;

    // if prev block exists
    if (prev_footer_addr >= heap_start) {
        boundary_tags *prev_footer = (boundary_tags *)prev_footer_addr;

        // and if prev block is free
        if (!tag_alloc(prev_footer)){

            // get the size of the previous block
            size_t prev_payload = tag_size(prev_footer);

            // get the header of the previous block
            boundary_tags *prev_header = get_header(prev_footer, prev_payload);

            // calculate the size of merged block 
            // prev_size + current_size + 2* TAG_SIZE
            size_t merged_payload = prev_payload + 2 * TAG_SIZE + payload;

            // set new header and footer tags for the merged block
            set_tag(prev_header, merged_payload, 0);
            set_tag(get_footer(prev_header, merged_payload), merged_payload, 0);

            // update payload to the merged payload size
            payload = merged_payload;
        }
    }
}


// resize a previously allocated block to at least N bytes
void *heap_realloc(void *alloc_state, void *prev, size_t nbytes) {

    // if not prev pointer, return block
    if (!prev) return heap_alloc(alloc_state, nbytes);

    // ptr to the new block
    void *p = heap_alloc(alloc_state, nbytes);

    // if allocation failed, return NULL
    if (!p) return NULL;

    // copy the old data to the new block
    boundary_tags *header = (boundary_tags *)((char *)prev - TAG_SIZE);

    size_t old_payload = tag_size(header);
    size_t new_payload = (nbytes + 7) & ~(size_t)7;
    size_t copy_bytes  = old_payload < new_payload ? old_payload : new_payload;

    // copy the data from the old block to the new block
    memmove(p, prev, copy_bytes);

    // free the old block
    heap_free(alloc_state, prev);

    // return the pointer to the new block
    return p;
}