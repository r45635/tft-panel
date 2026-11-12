#include "keyboard.h"

#include "../layout.h"
#include "fonts.h"

namespace keyboard {

lv_obj_t *create(lv_obj_t *parent, lv_obj_t *textarea, int32_t height) {
    using layout::S;
    lv_obj_t *kb = lv_keyboard_create(parent);
    lv_obj_set_size(kb, layout::SCREEN_W, height);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, textarea);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x0A0A0B), 0);
    lv_obj_set_style_pad_all(kb, S(6), 0);
    lv_obj_set_style_pad_gap(kb, S(6), 0);
    lv_obj_set_style_text_font(kb, fonts::ui20, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, lv_color_hex(0xF2F2F2), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x2A2A2E), LV_PART_ITEMS);
    // Mode, delete, arrows and OK keys are "checked" buttons: LVGL's theme draws them light.
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x3A3A40), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(kb, lv_color_hex(0xF2F2F2), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x6DB56D), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_radius(kb, S(8), LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    return kb;
}

}  // namespace keyboard
