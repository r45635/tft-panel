// The on-screen keyboard (LVGL's), styled for the panel: Wi-Fi password, library search.
#pragma once

#include <lvgl.h>

namespace keyboard {

// A full-width keyboard of `height` at the bottom of `parent`, typing into `textarea`.
// Its OK key sends LV_EVENT_READY and its keyboard key LV_EVENT_CANCEL to the keyboard.
lv_obj_t *create(lv_obj_t *parent, lv_obj_t *textarea, int32_t height);

}  // namespace keyboard
