// Pin map and panel constants of the board this firmware is built for (one per PlatformIO env).
#pragma once

#if defined(BOARD_WAVESHARE_P4_7B)
#include "pins_waveshare_p4_7b.h"
#elif defined(BOARD_CROWPANEL7)
#include "pins_crowpanel7.h"
#else
#error "No board selected: build with -DBOARD_CROWPANEL7 or -DBOARD_WAVESHARE_P4_7B (see platformio.ini)"
#endif
