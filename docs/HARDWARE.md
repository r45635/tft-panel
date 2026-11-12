# Hardware

TFT Panel runs on two 7" boards. Each has its own board layer in `src/board/` (one PlatformIO
environment each); everything above it is shared, and the interface is drawn on an 800×480 grid
scaled to the screen (`src/layout.h`).

- [Elecrow CrowPanel ESP32 Display 7.0" (DIS08070H, board V3.0)](#elecrow-crowpanel-esp32-display-70-dis08070h-board-v30) — env `crowpanel7`
- [Waveshare ESP32-P4-WIFI6-Touch-LCD-7B](#waveshare-esp32-p4-wifi6-touch-lcd-7b) — env `waveshare-p4-7b`

## Elecrow CrowPanel ESP32 Display 7.0" (DIS08070H, board V3.0)

Authoritative sources, in this order:
1. What the board does on the bench (bring-up logs below).
2. Elecrow's working V3.0 example code and the V3.0 schematic (2024-04-20) in
   [Elecrow-RD/CrowPanel-7.0-HMI-ESP32-Display-800x480](https://github.com/Elecrow-RD/CrowPanel-7.0-HMI-ESP32-Display-800x480).
3. Elecrow's prose "Hardware Driver Guide" (`Eagle_SCH&PCB/V3.0/readme.md`) — useful, but wrong
   in places (see Pitfalls).

Pin map in code: [`src/board/pins_crowpanel7.h`](../src/board/pins_crowpanel7.h).

### Summary

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

### Bring-up log

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

### Pitfalls

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
  **Confirmed gone by eye (2026-11-07)**, lyric slides included, with the 64-byte cache lines and the
  single 48-line draw strip (shorter flushes). Levers if they ever come back: lower PCLK (16 → 14 MHz),
  double framebuffer.
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
- 🪤 **A software restart leaves the display broken** (found 2026-11-07 on the first OTA): `esp_restart()`
  is a CPU-only reset (`rst:0xc RTC_SW_CPU_RST`); LCD_CAM / GDMA / I2C keep their state, so the image
  came up partly off-frame and touch was dead, while the framebuffer was perfect and the boot log
  normal. An EN reset fixed it. `board::cleanResetIfNeeded()` (first call in `setup()`) turns every
  CPU-only reset (software restart, OTA, rollback, panic, watchdogs) into one full system reset
  (`esp_rom_software_reset_system`, `rst:0x3`), with an RTC_NOINIT guard against loops.
  🪤 Not while the running image is still "pending verify": the bootloader counts that reset as a
  failed boot and rolls the update back (v0.1.2 could never install over the air because of it).
  Then the reset waits until the app confirms the image. Intentional restarts all go through
  `board::restart()` (a marked system reset), so updates from v0.1.3 on boot clean at once.
- 🪤 **Image shifted / jumping after flash writes** (2026-11-07): the RGB LCD interrupt was not
  IRAM-safe, so every flash write (NVS settings, Wi-Fi calibration, OTA) stalled the bounce-buffer
  refill; the driver's VSYNC resync only catches a lag of more than two bounce buffers, so a
  20-line shift could stay. Fix: `CONFIG_SPIRAM_XIP_FROM_PSRAM` (code + rodata copied to PSRAM at
  boot, ~1.8 MB) + `CONFIG_LCD_RGB_ISR_IRAM_SAFE`: 20 NVS writes in 20 s, image steady, rendering
  not slower. Works with the v0.1.x bootloader (OTA never replaces it).
- 🪤 **Jerky image while an update is written** even with XIP from PSRAM: flash and PSRAM share the
  MSPI bus, so a sector erase stalls PSRAM reads for milliseconds. During an install the panel now
  shows a static screen generated in the bounce-buffer callback from internal RAM only (1-bit text
  mask + live progress bar; `board::staticScreen*`): Vincent saw it steady. The driver runs in
  `no_fb` mode; we own the framebuffer and copy from it in `on_bounce_empty` otherwise.
- The panel's white is cool (bluish), seen by Vincent with neutral UI colours. `board::setWarmth()`
  scales green/blue in the flush through a 4 KB G+B table (~5 ms per full-stage frame, O2 + IRAM).
  On the colour test card (Settings, console `colours`) Vincent picked 76 % (blue -34 %, green -9 %):
  default 75 %. Screenshots read the corrected framebuffer, so they look yellowish (`warmth 0` for
  documentation captures).
- 🪤 **Covers with red and blue swapped** (until v0.1.6): LVGL's bundled TJpgDec writes B, G, R (LVGL's
  RGB888 memory order). `cover.cpp` swaps to R, G, B in its output callback. Check against the
  companion's own JPEG (`/api/roon/image/<key>`) with `warmth 0`, not by eye on the bluish panel.
- Old Elecrow comment `TOUCH_GT911_RST 38` is a leftover; GPIO38 is the J4/J7 header pin.

### Touch diagnostic

`PLATFORMIO_BUILD_FLAGS="-DTOUCH_DIAG" pio run -t upload` runs several reset variants at boot and
prints the PCA9557 registers plus an I2C scan after each (`[diag]` lines on the serial port).

## Waveshare ESP32-P4-WIFI6-Touch-LCD-7B

Sources: what the board does on the bench (below), and Waveshare's BSP
`waveshare/esp32_p4_wifi6_touch_lcd_7b` 3.0.1 (Apache-2.0, Espressif component registry) with the
`espressif/esp_lcd_ek79007` driver 2.0.2 (Apache-2.0) for the panel's init sequence.
Pin map in code: [`src/board/pins_waveshare_p4_7b.h`](../src/board/pins_waveshare_p4_7b.h).

### Summary

| Block | Part | Interface / pins |
|---|---|---|
| MCU | ESP32-P4 rev v3.2, 400 MHz dual core | 32 MB flash (QIO), 32 MB PSRAM (own bus, 250 MHz) |
| Wi-Fi | ESP32-C6 coprocessor, esp_hosted over SDIO | CLK 18, CMD 19, D0–D3 14–17, C6 reset 54 (Arduino's defaults) |
| LCD | 7" 1024×600, EK79007, **mounted upside down** | MIPI-DSI 2 lanes, 1000 Mbps/lane, DPI 52 MHz, RGB565; D-PHY powered by LDO channel 3 at 2.5 V; reset GPIO33 |
| LCD timings | | H 10/160/160 (sync/back/front) · V 1/23/12 |
| Backlight | | GPIO32, LEDC PWM 5 kHz, **inverted** (active low) |
| I2C | | SDA 7, SCL 8: GT911 0x5D, ES8311 codec 0x18, ES7210 mic ADC 0x40 |
| Touch | GT911 (id "911", config 1024×600) | RST and INT not wired: address fixed at 0x5D |
| USB | two USB-C: *USB 1.1* = the P4's USB-Serial/JTAG (console, flash), *USB to UART* = UART0 (GPIO37/38) | plus a USB-A host port |

### Bring-up log

- **2026-11-11 (Mac Studio, `/dev/cu.usbmodem83101`)** — factory firmware: Waveshare's
  ESP-Brookesia phone demo (`esp32-p4-lcd-7b-brookesia`, IDF 5.5.5). Back up the full 32 MB before
  the first write (`esptool read-flash 0 ALL factory.bin`, then `verify-flash`): `esptool
  write-flash 0 factory.bin` puts the demo back. Done and verified on this board (kept outside the repo).
- A bare spike (DSI bus + DBI commands + DPI panel, GT911 polling, `WiFi.scanNetworks`) worked first
  time: colour bars, touch, 11 networks — through the **factory C6 firmware**, which does not answer
  esp_hosted's version request (Arduino logs "Slave firmware version: 0.0.0" and an update URL).
  It carries the local network (STA, setup portal, mDNS, HTTP to the companion) but **no UDP reply
  and no Internet**: DNS failed (`-54`), TCP to a public IP failed, UDP to the router's port 53 got
  no answer from any source port. Installing the C6 firmware matching the host (esp_hosted 2.12.13,
  1.2 MB, ~40 s over SDIO from the P4) fixed all of it.
- The image was upside down (colour bars reversed). The EK79007's MADCTL flip (0x36) has no
  effect in video mode: the board layer turns each flushed area by 180° (a rectangle turned 180°
  is its pixels in reverse order) and draws it at the mirrored position. Touch needs nothing: the
  GT911 already reports in the viewer's orientation (checked at the four corners).
- TFT Panel itself: Wi-Fi setup from the phone, companion, zones, synced lyrics, covers
  (480 px JPEG decoded in ~1 s), screenshots, `restart` (full system reset) all work.

### Pitfalls

- **The factory C6 firmware is too old for Arduino 3.3's esp_hosted**: LAN works, Internet and
  DNS do not (see the bring-up log). The P4 firmware embeds the matching C6 image
  (`tools/hosted_fw.py` fetches and checks it at build time) and installs it at boot when the C6
  is older (`src/net/coprocessor.cpp`, two attempts per version, then gives up and boots normally).
  When the platform is updated, Arduino logs the new expected version and URL at boot: update
  `tools/hosted_fw.py`.
- **A flash write kills the SDIO link to the C6** while it is busy: a streamed OTA died at 240 KB
  ("Response not received for Req_GetWifiMode" forever after). Updates are received whole into PSRAM,
  checked, then written (`OTA_DOWNLOAD_FIRST`), for GitHub updates and the local upload page alike.
  v0.1.11 still streamed: a P4 on v0.1.11 cannot update itself, it needs the web installer once.
- **Opening the USB console resets the P4** when DTR/RTS toggle (reset reason `CHIP_USB_UART_RESET`);
  a logger that reopens the port after each reset then loops. Open it with **DTR high, RTS low**.
- The console is the native USB (`ARDUINO_USB_CDC_ON_BOOT=1`): nothing on the *USB to UART* port
  after the ROM boot messages.
- `esp_read_mac(ESP_MAC_WIFI_STA)` fails on the P4 (Wi-Fi lives in the C6): names (AP SSID,
  mDNS host) come from the base MAC, `esp_efuse_mac_get_default`, which is the same value on the S3.
- The panel's white is neutral, unlike the CrowPanel's bluish one: colour warmth defaults to 0 here
  (75 % on the CrowPanel), per board (`DEFAULT_WARMTH`).
- Flash writes do not stall the display (PSRAM has its own bus): no frozen update screen
  (`HAS_STATIC_SCREEN = false`), the progress bar stays live.
- ESP Web Tools learned the ESP32-P4 in 10.4.0; 10.2 does not list it.
- Building this environment and `crowpanel7` one after the other makes pioarduino redo the
  CrowPanel's framework rebuild (~2 min), whose first pass can fail on a missing `.sconsign` file;
  run it again. `tools/release.sh` does.
