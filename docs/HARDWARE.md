# Hardware — Elecrow CrowPanel ESP32 Display 7.0" (DIS08070H, board V3.0)

Authoritative sources, in this order:
1. What the board does on the bench (bring-up logs below).
2. Elecrow's working V3.0 example code and the V3.0 schematic (2024-04-20) in
   [Elecrow-RD/CrowPanel-7.0-HMI-ESP32-Display-800x480](https://github.com/Elecrow-RD/CrowPanel-7.0-HMI-ESP32-Display-800x480).
3. Elecrow's prose "Hardware Driver Guide" (`Eagle_SCH&PCB/V3.0/readme.md`) — useful, but wrong
   in places (see Pitfalls).

Pin map in code: [`src/board/board_pins.h`](../src/board/board_pins.h).

## Summary

| Block | Part | Interface / pins |
|---|---|---|
| MCU | ESP32-S3-WROOM-1-N4R8 (rev v0.2) | 4 MB quad flash, 8 MB octal PSRAM |
| LCD | 800×480 TN, EK9716BD3 + EK73002ACGB | RGB565 parallel: B0–B4 15,7,6,5,4 · G0–G5 9,46,3,8,16,1 · R0–R4 14,21,47,48,45 · DE 41 · VSYNC 40 · HSYNC 39 · PCLK 0 |
| LCD timings | | H 40/48/40 · V 1/31/13 · syncs active low · PCLK falling edge · 15–24 MHz |
| Backlight | MT9201 boost, EN on GPIO2 | active high, LEDC PWM |
| I2C | 4.7 kΩ pull-ups | SDA 19, SCL 20 |
| Expander | PCA9557 @ 0x18 | IO0 TP-RESET · IO1 TP-INT · IO2 LCD-RESET · IO3 LCD-POWER |
| Touch | GT911 on the touch-panel flex (P2 or P4) | I2C 0x5D (INT low at reset) / 0x14 |
| SD | microSD | SPI CS 10, MOSI 11, SCK 12, MISO 13 |
| Audio | NS4168 I2S amp | DOUT 17, BCLK 42, LRCLK 18 |
| USB | CH340C → UART0 | auto-reset via DTR/RTS; no native USB on the connector |

## Bring-up log

- **2026-11-07 (Mac Studio, `/dev/cu.usbserial-8310`)** — chip ESP32-S3 rev v0.2, PSRAM 8 MB,
  flash 4 MB detected. Flash at 921600 baud fails ("Unable to verify flash chip connection");
  **460800 works**. RGB panel created with bounce buffers, LVGL 9.6 runs. I2C scan finds
  **only 0x18** (PCA9557). **GT911 absent** at 0x5D and 0x14 after every reset variant, at 400 and 100 kHz.
  PCA9557 input register reads **IO0 (TP-RESET) = 0 even when released** — see Pitfalls.
  Open: visual check of the image (colour order, drift) and the touch flex.
- **2026-11-07, later** — `[diag]` re-run: with IO1 (TP-INT) switched to input it reads back
  whatever was last driven (1 after high, 0 after low), as does IO0: **both touch lines float**, as
  if nothing were on the flex side. Elecrow's current V3.0 sketch (`example/…/V3.0/…/crowpanel-esp32-7.0.ino`,
  2026-06) does **no** reset at all (PCA9557 sequence commented out, `TOUCH_GT911_RST -1`); our
  "all inputs (released)" step covers that variant too — still no GT911. Next: physical check of the
  touch FPC. Phase 1 firmware (Wi-Fi AP + setup QR) runs on the board meanwhile.
- **2026-11-07, phase 1 on the bench** — provisioned from an iPhone through the setup AP
  (`POST /save` reached; the first attempt left no trace before request logging existed). Joined
  Wi-Fi at -36..-46 dBm, found the companion by mDNS, followed the playing zone, loaded synced lyrics,
  followed a track change. Two OOM aborts before the PSRAM allocator fix (see Pitfalls), none after.
- **2026-11-07, touch, software side closed** — console `touchdiag` (panel and backlight on, as the
  factory firmware does) replays Elecrow's exact factory sequence (their PCA9557 library: same 0x18,
  same registers, same 20 ms / 100 ms), a 500 ms reset, and the expander fully released, each at
  400 and 100 kHz, probing 3 s each: **no GT911**. With every expander pin released for 3 s the input
  register reads **0x00**: no expander net has a pull-up on the main board. TP-RESET (IO0) is
  open-drain, so its high level can only come from the touch side; it reads 0 → the GT911 is either
  not connected or held in reset. Elecrow forum users with the same board see 0x5D in a plain scan
  (their issues were higher up, e.g. 400 kHz in LovyanGFX), we never do. Remaining test: Elecrow's V3.0
  factory demo (`factory_firmare/hmi7.0-factory-firmware-3.0`, NVS at the same 0x9000) with a finger.
- **2026-11-07, touch fixed** — Vincent reseated the touch flex: GT911 answers at 0x5D from boot,
  `touchdiag` finds it at once. Our reset sequence was right all along.

## Pitfalls

- ✅ **Root cause (2026-11-07): the touch flex was not seated** in its latch connector (top centre, back of
  the board, thin black flex — not the wide orange LCD flex J2). After reseating: PCA9557 input reads
  **0xFF** (IO0 high, pulled up from the flex), GT911 at **0x5D** at boot, id `911`, fw `0x1060`, 800×480.
  Diagnostic rule: **IO0 = 0 while released ⇒ check the touch flex first.**
- If the flex is seated and the GT911 still never answers: give TP-RESET a high level — 10 kΩ from
  the TP-RESET net to 3V3, or fit **R40 (0 Ω)** to tie it to EN. With R40 fitted, the firmware must
  stop pulling IO0 low (it would reset the ESP32).
- 🪤 **PCA9557 IO0 is open-drain.** Writing it high only releases the line. The V3.0 schematic has
  **no pull-up on TP-RESET on the main board** (net: PCA9557 IO0, touch FPC P2.1/P4.1, R40 0R/NC to
  EN_RESET). The high level must come from the touch flex. If IO0 reads 0 while released, the GT911
  is held in reset or not connected: check that the touch flex is seated in P2/P4 before blaming code.
- 🪤 Elecrow's guide says IO2/IO3 "remain inputs"; their **working code** (`Out.setMode(IO_OUTPUT)`)
  turns **all** expander pins into outputs latched high. We copy the code, not the prose.
- 🪤 The PCA9557 library header claims address 0x41; the board uses **0x18**.
- 🪤 GPIO0 is both BOOT strapping and LCD PCLK: never hold BOOT while the display runs.
- 🪤 **Flickering dots around often-refreshed labels** (seen by eye, absent from framebuffer captures):
  the RGB bounce buffers are refilled from the PSRAM framebuffer while LVGL's flush writes into it.
  Mitigation: only touch LVGL objects whose content changed (`setText`/`setTextColour`/`setHidden` in
  `ui.cpp`; set_text with the same text still invalidates) and 20-line bounce buffers (were 10).
  Next levers if dots remain: lower PCLK (16 → 14 MHz), double framebuffer.
- 🪤 **Internal RAM is the scarce resource, not PSRAM.** Blocks under 4 KB
  (`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`) go to internal RAM even with 7 MB of PSRAM free. With
  LVGL on the C allocator, the tiny_ttf glyph cache (~1.3 KB per 44 px glyph) ate the ~70 KB the
  Wi-Fi stack needs: `abort()` in `esp_phy_enable` / mDNS a few minutes into a song, and every HTTP
  call reported "connection refused" (HTTPClient's name for any failed connect). Fix: LVGL allocates
  in PSRAM (`src/board/lv_mem_psram.c`), ArduinoJson too, JSON parsed from the socket. Internal RAM
  free while playing went from 0 to a steady ~120 KB; the net task warns below 20 KB.
- 🪤 LVGL's built-in Montserrat fonts are **ASCII only** — no French accents. Text goes through the
  embedded Noto Sans (tiny_ttf, `src/app/fonts.cpp`); Montserrat only supplies the LVGL symbols.
- 🪤 LVGL 9.6 deprecates `lv_obj_add_flag/remove_flag` for common flags (runtime warning): use
  `lv_obj_set_hidden/clickable/scrollable`.
- 🪤 **Full-stage redraws are PSRAM-bound**: ~88 ms per 760×340 frame with the backdrop (render
  ~68 ms: backdrop read + text; flush ~20 ms); ~70 ms without it (measured with 32-byte lines). The RGB scan-out reads the
  framebuffer continuously from the same PSRAM. Profile with the `[ui] scroll:` log line (first 3
  slides per boot: steps, refresh and flush time).
- Old Elecrow comment `TOUCH_GT911_RST 38` is a leftover; GPIO38 is the J4/J7 header pin.

## Touch diagnostic

`PLATFORMIO_BUILD_FLAGS="-DTOUCH_DIAG" pio run -t upload` runs several reset variants at boot and
prints the PCA9557 registers plus an I2C scan after each (`[diag]` lines on the serial port).
