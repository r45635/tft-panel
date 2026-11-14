// Pin map and panel constants of the board this firmware is built for (one per PlatformIO env).
#pragma once

#if defined(BOARD_WAVESHARE_P4_7B)
#include "pins_waveshare_p4_7b.h"
#elif defined(BOARD_P4_S070_WBA)
#include "pins_p4_s070_wba.h"
#elif defined(BOARD_CROWPANEL7)
#include "pins_crowpanel7.h"
#else
#error "No board selected: build with one of -DBOARD_CROWPANEL7, -DBOARD_WAVESHARE_P4_7B, -DBOARD_P4_S070_WBA (see platformio.ini)"
#endif
