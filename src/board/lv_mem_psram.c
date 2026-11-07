// LVGL allocator (LV_USE_STDLIB_MALLOC = LV_STDLIB_CUSTOM): everything LVGL allocates
// — objects, label texts, tiny_ttf glyph caches — goes to PSRAM.
//
// Why: with the C allocator, blocks under 4 KB (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL)
// land in internal RAM. A 44 px glyph is ~1.3 KB, so the lyrics font cache alone ate
// the ~70 KB the Wi-Fi stack needs, and the panel aborted in phy/mDNS allocations.
// The draw buffers are allocated explicitly in internal RAM by board.cpp.
#include <lvgl.h>

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#include <esp_heap_caps.h>

#define PSRAM_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes) {
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool) { LV_UNUSED(pool); }

void *lv_malloc_core(size_t size) { return heap_caps_malloc(size, PSRAM_CAPS); }

void *lv_realloc_core(void *p, size_t new_size) { return heap_caps_realloc(p, new_size, PSRAM_CAPS); }

void lv_free_core(void *p) { heap_caps_free(p); }

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p) {
    multi_heap_info_t info;
    heap_caps_get_info(&info, PSRAM_CAPS);
    mon_p->total_size = info.total_free_bytes + info.total_allocated_bytes;
    mon_p->free_size = info.total_free_bytes;
    mon_p->free_biggest_size = info.largest_free_block;
    mon_p->used_cnt = info.allocated_blocks;
    mon_p->free_cnt = info.free_blocks;
    mon_p->max_used = info.total_allocated_bytes;   // approximation: no peak tracking
    mon_p->used_pct = mon_p->total_size ? 100 - (100 * info.total_free_bytes) / mon_p->total_size : 0;
    mon_p->frag_pct = info.total_free_bytes
                          ? 100 - (100 * info.largest_free_block) / info.total_free_bytes
                          : 0;
}

lv_result_t lv_mem_test_core(void) { return heap_caps_check_integrity(PSRAM_CAPS, true) ? LV_RESULT_OK : LV_RESULT_INVALID; }

#endif
