#ifndef __COMMON_H__
#define __COMMON_H__


#include <stdint.h>

typedef struct {
    void *end;
    void *free;
} alloc_state_t;

typedef struct {
    uint32_t data;
} boundary_tags;

#define TAG_SIZE sizeof(boundary_tags)

#endif // __COMMON_H__