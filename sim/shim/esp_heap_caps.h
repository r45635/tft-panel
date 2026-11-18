// Host stand-in: one heap, so PSRAM and internal allocations are both plain malloc.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_DMA (1 << 3)

inline void *heap_caps_malloc(size_t n, uint32_t) { return malloc(n); }
inline void *heap_caps_calloc(size_t n, size_t size, uint32_t) { return calloc(n, size); }
inline void heap_caps_free(void *p) { free(p); }
// Fixed figures, so the settings screen's memory line is the same in every picture.
inline size_t heap_caps_get_free_size(uint32_t caps) { return caps & MALLOC_CAP_SPIRAM ? 7 << 20 : 120 << 10; }
inline size_t heap_caps_get_minimum_free_size(uint32_t caps) { return heap_caps_get_free_size(caps); }
