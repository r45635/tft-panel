// Track cover, from one small JPEG: a thumbnail next to the title, and a full-screen
// backdrop (blurred, scaled to cover 800x480, darkened so the lyrics stay readable).
// Built in the net task, displayed by the UI; buffers live in PSRAM and are freed with
// the last shared_ptr.
#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include <memory>

struct Picture {
    uint16_t *pixels = nullptr;   // RGB565, w x h
    int w = 0, h = 0;
    lv_image_dsc_t dsc{};         // points at `pixels`
    ~Picture();
};

namespace cover {

struct Cover {
    std::shared_ptr<const Picture> thumb;      // fits in thumbBox x thumbBox, aspect kept
    std::shared_ptr<const Picture> backdrop;   // null unless asked for
};

// Decodes `jpeg` (baseline JPEG, up to 512x512) and builds the pictures.
// Returns false with `error` set on failure (progressive JPEG, out of memory…).
bool build(const uint8_t *jpeg, size_t len, int thumbBox, bool withBackdrop, int outW, int outH,
           Cover &out, String &error);

}  // namespace cover
