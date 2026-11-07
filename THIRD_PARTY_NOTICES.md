# Third-party notices

TFT Panel's own code, documentation and media are under the [MIT licence](LICENSE). The
firmware image also contains, or is built with, the following components under their own
licences.

| Component | Use in TFT Panel | Licence |
|---|---|---|
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) 3.3 | framework (Wi-Fi, HTTP, web server, Update) | LGPL-2.1-or-later |
| [ESP-IDF](https://github.com/espressif/esp-idf) 5.5 | underlying SDK (drivers, FreeRTOS, mbedTLS, …) | Apache-2.0 (components under their own licences, listed in ESP-IDF) |
| [LVGL](https://lvgl.io) 9.6 | user interface | MIT |
| — stb_truetype (in LVGL `tiny_ttf`) | run-time font rendering | MIT or public domain |
| — TJpgDec (in LVGL) | cover JPEG decoding | ChaN's free licence (BSD-style, see below) |
| — QR Code generator (in LVGL) | setup QR codes | MIT |
| [ArduinoJson](https://arduinojson.org) 7.4 | companion API | MIT |
| [Noto Sans](https://notofonts.github.io) (subset, `assets/fonts/`) | text font, embedded | SIL Open Font License 1.1 ([OFL.txt](assets/fonts/OFL.txt)) |
| [ESP Web Tools](https://github.com/esphome/esp-web-tools) 10.2 | browser installer on the website (loaded from unpkg, not in the firmware) | Apache-2.0 |
| [Python-Markdown](https://python-markdown.github.io) 3.7, [Pillow](https://python-pillow.org) 11.0 | build tools only (website, promo pictures) | BSD-3-Clause, MIT-CMU |

**LGPL and the firmware binaries.** The Arduino core is linked statically into the firmware.
As the LGPL requires, everything needed to rebuild the firmware with a modified version of
the core is public: this repository's complete source, the pinned toolchain in
`platformio.ini`, and the build steps in the [README](README.md).

**TJpgDec** — Copyright (C) 2021, ChaN, all right reserved. "The TJpgDec module is a free
software and there is NO WARRANTY. No restriction on use. You can use, modify and redistribute it
for personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY. Redistributions of
source code must retain the above copyright notice."

**Trademarks.** Roon is a trademark of Roon Labs; Elecrow and CrowPanel are trademarks of
Elecrow. TFT Panel is an independent project, not affiliated with or endorsed by them.
