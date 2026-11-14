// Music library on the panel (boards with HAS_LIBRARY): search, browse Roon's library, play on
// the followed zone. Opened from the controls bar. The net task does the browsing (net/library).
#pragma once

#include <lvgl.h>

#include "../net/net.h"

namespace libui {

void build(lv_obj_t *scr);
void open();
void openFind(bool album, const String &name, const String &artist);   // cover view: artist/album tapped
void close();
bool isOpen();
void refresh(const net::Snapshot &s);   // new page, thumbnails, messages, followed zone
void tick(uint32_t now);                // hides the message after a while

// Showcase: an invented album (tracks with the demo cover); `actions` opens the action sheet.
void showcase(const std::shared_ptr<const Picture> &thumb, bool actions);

}  // namespace libui
