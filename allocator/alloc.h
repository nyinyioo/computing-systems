#ifndef __ALLOC_H__
#define __ALLOC_H__

#include <stddef.h>
#include <stdbool.h>

// description:
// - initialize an allocator
// arguments:
// - heap_start: *heap_start is in the heap area you manage but *(heap_start-1) is not;
//   aligned on an 8-byte boundary
// - heap_end: *(heap_end-1) is in the heap area you manage but *heap_end is not;
//   aligned on an 8-byte boundary
// returns:
// - an allocator state pointer that will be passed unchanged to any alloc functions
//   (may be NULL if your allocator is stateless)
// other:
// - may not use any library code that allocates or maps memory
// - may not read or write any files, stdin, stdout, or stderr
void *init(void *heap_start, void *heap_end);

// description:
// - allocate a block of memory
// arguments:
// - heap_handle: the pointer returned by your init()
// - nbytes: a minimum number of bytes to allocate (may be 0)
// returns:
// - pointer p to the allocated block of at least nbytes bytes
//   (p may be NULL if nbytes was 0 or if the heap is full)
// output invariants:
// - p must be either in [heap_start,heap_end) as passed to init, or NULL
// - if p is not NULL, P+nbytes must be in (heap_start,heap_end] as passed to init
// - p must be aligned on an 8-byte boundary
// - [p,p+nbytes) may be overwritten by the caller
// other:
// - may not use any library code that allocates or maps memory
// - may not read or write any files, stdin, stdout, or stderr
void *heap_alloc(void *heap_handle, size_t nbytes);

// description:
// - free a previously allocated a block of memory
// arguments:
// - heap_handle: the pointer returned by your init()
// - p: a pointer previously returned from heap_alloc() and never freed
// other:
// - may not use any library code that allocates or maps memory
// - may not read or write any files, stdin, stdout, or stderr
void heap_free(void *heap_handle, void *p);

// description:
// - extend or shrink allocated block size, moving the block address and contents if necessary
// arguments:
// - heap_handle: the pointer returned by your init()
// - prev: a pointer previously returned from heap_alloc() and never freed,
//         or NULL (in which case realloc behaves like heap_alloc with the same size)
// returns:
// - pointer p to the allocated block of at least nbytes bytes,
//   with a copy of the previous block at prev if prev is not NULL
//   (p may be NULL if nbytes was 0 or if heap is full)
// output invariants:
// - p must be either in [heap_start,heap_end) as passed to init, or NULL
// - if p is not NULL, P+nbytes must be in (heap_start,heap_end] as passed to init
// - if p is NULL, the block at prev remains allocated at its previous sizeand is not altered
// - p must be aligned on an 8-byte boundary
// - [p,p+nbytes) may be overwritten by the caller
// - if prev is not NULL, the full contents of the block at prev
//   must be copied to the beginning of the block at p
//   (or nbytes of the previous contents if shrinking)
// other:
// - may not use any library code that allocates or maps memory
// - may not read or write any files, stdin, stdout, or stderr
void *heap_realloc(void *heap_handle, void *prev, size_t nbytes);

// description:
// - checks the heap for consistency and/or implements other debug functionality
// - for the consistency check, the invariants this checks are up to you,
//   but it should be able to identify heap headers with corrupted fields
// arguments:
// - heap_handle: the pointer returned by your init()
// - op: identifies the operation to perform:
//   - op = 0: heap consistency check
//   - op != 0: up to you
// returns:
// - when op = 0:
//   - 1 if the heap has passed the consistency check
//   - 0 if the heap has failed the consistency check
// - when op != 0: up to you
int debug(void *heap_handle, int op);

#endif // __ALLOC_H__