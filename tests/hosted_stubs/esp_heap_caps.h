#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
#define MALLOC_CAP_DMA 8
#ifdef __cplusplus
extern "C" {
#endif
void *heap_caps_aligned_alloc(size_t align, size_t size, unsigned caps);
#ifdef __cplusplus
}
#endif
