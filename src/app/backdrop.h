// Full-screen backdrop from the track's cover: a small JPEG, blurred, scaled to cover
// 800x480 and darkened so the lyrics stay readable. Built in the net task, displayed by
// the UI; the buffer lives in PSRAM and is freed with the last shared_ptr.
#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include <memory>

struct Backdrop {
    uint16_t *pixels = nullptr;   // RGB565, w x h
    int w = 0, h = 0;
    lv_image_dsc_t dsc{};         // points at `pixels`
    ~Backdrop();
};

namespace backdrop {

// Decodes `jpeg` (baseline JPEG, any size up to 512x512) and composes the backdrop.
// Returns null with `error` set on failure (progressive JPEG, out of memory…).
std::shared_ptr<const Backdrop> build(const uint8_t *jpeg, size_t len, int outW, int outH, String &error);

}  // namespace backdrop
