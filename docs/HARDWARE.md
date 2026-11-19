# Hardware

> **Before flashing any board for the first time**, run `tools/backup-firmware.py --port <port>`:
> it names the board from its chip and flash size — the only way to tell the two ESP32-P4 screens
> apart before flashing — copies the whole flash, and reads it back to check the copy. A running
> image also says so itself: a mismatch between the board it was built for and the hardware under
> it prints **WRONG BOARD** at boot and in the console's `status`, which is what turns a dark
> screen from a mystery into a sentence.

TFT Panel runs on three 7" boards. Each has its own board layer in `src/board/` (one PlatformIO
environment each); everything above it is shared, and the interface is drawn on an 800×480 grid
scaled to the screen (`src/layout.h`).

- [Elecrow CrowPanel ESP32 Display 7.0" (DIS08070H, board V3.0)](#elecrow-crowpanel-esp32-display-70-dis08070h-board-v30) — env `crowpanel7`, ESP32-S3, 4 MB flash
- [Waveshare ESP32-P4-WIFI6-Touch-LCD-7B](#waveshare-esp32-p4-wifi6-touch-lcd-7b) — env `waveshare-p4-7b`, ESP32-P4, 32 MB flash
- [VIEWE ESP32-P4 7" S070](#viewe-esp32-p4-7-s070-jd9165-working-2026-11-14) — env `p4-s070-wba`, ESP32-P4, 16 MB flash
- [Other boards](#other-boards) — not supported; what adding one takes

🪤 The two ESP32-P4 boards differ only by flash size (32 MB Waveshare, 16 MB VIEWE), which is what
`tools/backup-firmware.py` and `board::hardwareVerdict()` use to tell them apart. The browser
installer picks by chip family and cannot.

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
  effect in video mode: the board layer turns each flushed area by 180° and writes it at the
  mirrored position. Touch needs nothing: the GT911 already reports in the viewer's orientation
  (checked at the four corners).
- **Render speed (2026-11-12)**: a lyric slide (`[ui] scroll` in the log, ~420 lines × 1024 per
  frame) ran at 6 frames in 400 ms (~15 fps). Per slide the flush cost 142 ms: warmth table 54,
  upright copy for screenshots 57, CPU reverse 23, 2D-DMA copy 8. Now the PPA turns and copies each
  strip (asynchronous), screenshots turn the framebuffer back only when asked, and the warmth works
  on two pixels at once: 8-9 frames in 400 ms (~20 fps), flush ~1.3 ms per 48-line strip. Rendering
  is the rest (~37 ms per frame: blending, glyphs). Tried and dropped: LVGL's PPA draw unit (needs
  `esp_cache_get_line_size_by_addr`, newer than IDF 5.5.5), two software draw threads (slower,
  ~68 ms per frame), 200-line draw buffers in PSRAM (slower than 48 lines in internal RAM).
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
- **Left as it is: the update screen comes out turquoise** (decided 2026-11-15 - the screen
  changing colour that visibly is a fair way of saying an update is running, and nobody can
  mistake it for a panel doing nothing). During a firmware
  upload from the panel's page the near-black background of the update picture is displayed as a
  light cyan, text and progress bar crisp on top, and it stays that way until something redraws
  the screen. What is established: `board::frameBuffer()` (that is `g_fb`, cache-synced) reads
  **black** - `(8, 4, 0)` at every point sampled - while the panel is visibly cyan, so the pixels
  the firmware produces are right and the fault is below them. Refuted, each by experiment, so
  that nobody spends an evening on them again: the warmth filter (its gains only ever *reduce*
  green and blue, 255/256 and 250/256 at 6 %); the panel drifting on a large uniform dark field
  (the failure screen is one and stays black for minutes); the flash write (a 3.5 MB upload
  refused on its checksum does the whole transfer and writes no flash - the framebuffer stayed
  black); and the screen being drawn once and left alone (shown through `demo` + `show progress`
  and watched, nothing happens). It needs an upload to appear. Not reproduced on the CrowPanel.
  Whoever reopens this: the remaining lead is a build that prints the PPA and DPI state during an
  upload, since everything above the framebuffer has been cleared.
- ESP Web Tools learned the ESP32-P4 in 10.4.0; 10.2 does not list it.
- Building this environment and `crowpanel7` one after the other makes pioarduino redo the
  CrowPanel's framework rebuild (~2 min), whose first pass can fail on a missing `.sconsign` file;
  run it again. `tools/release.sh` does.

## Power use (not measured)

Nobody has put a meter on either panel yet. Both take 5 V over USB-C, and the backlight is most
of the draw.
- Waveshare P4 7B: a user on the Espressif forum measured a P4 board with a 7" 1024×600 panel at
  ~770 mA with the backlight on, ~270 mA with it off (about 3.9 W and 1.4 W at 5 V). No figure
  from Waveshare.
- CrowPanel 7": no published figure; expect the same order (2-4 W with the screen lit).
- When the screen turns off (Settings, "sleep"), the firmware only fades the backlight to 0: Wi-Fi,
  the CPU and the panel keep running, so the panel follows the music and wakes at once. No CPU or
  Wi-Fi power saving is set.
- To measure: a USB-C power meter between the charger and the panel, readings at 100 %, at the
  usual brightness, and with the screen off; record them here.

## Other boards

Not supported. A board with the same chip is not enough: the display controller, its timings, the
pins (backlight, LCD reset, I2C, sometimes the SDIO link to the Wi-Fi chip) and the panel's
orientation all differ, and the firmware would leave the screen dark. The web installer and the
OTA pick firmware by chip family and board name only, so it must never be used on a look-alike.

What adding a board takes: a `pins_<board>.h` and a `board_<board>.cpp` behind `board.h`, an
environment in `platformio.ini`, a `BOARD_ASSET` for its release files, and on the website a way to
choose between boards with the same chip. Then a bring-up **on the real board**: colour bars and
orientation, touch at the four corners, backlight, Wi-Fi to the Internet, an OTA. Nothing is
published for a board that has not run.

### VIEWE ESP32-P4 7" S070, JD9165 (working 2026-11-14)

Sold as **UEP4S070H1024V600C-WB-C** by VIEWE; its own factory firmware calls the board
`ESP32_P4_S070_WBA`. ESP32-P4 rev 3.2, 16 MB flash, 32 MB PSRAM, ESP32-C6 for Wi-Fi, 7" 1024x600
MIPI-DSI behind a **JD9165** driving a UE070WS glass, GT911 touch, serial through a **CH340** (not
the P4's native USB). MAC `80:f1:b2:xx:xx:xx`, env `p4-s070-wba`, asset `tft-panel-p4-s070-wba`.
It is **not** the Guition JC1060P470C below: the two share the panel controller and little else.

It shares `src/board/board_p4_mipi.cpp` with the Waveshare 7B; everything specific to it is a
constant in `src/board/pins_p4_s070_wba.h`, and every value there comes from the vendor's own
board support package (public: `VIEWESMART/7-1024X600-ESP32-P4-C6-TOUCH-DISPLAY`, component
`esp32_p4_s070_wba`), not from a look-alike board.

**Root cause of a day of dark screen: the panel's two supply rails are switched by GPIOs, and
nothing was driving them.** `GPIO 4` (LCM 1.8 V) high, 30 ms, then `GPIO 5` (LCD 3.3 V) high,
before the D-PHY LDO and before anything is sent. The backlight on GPIO 23 is a separate rail,
which is why the screen could glow while the panel was unpowered. The panel **reset is GPIO 22**,
not 0, 5 or 27 as community configurations for similar boards say.

With the rails up, the sequence is the ordinary one: LDO channel 3 at 2500 mV, 2 DSI lanes at
600 Mbps with the driver's default PHY reference, 8-bit DBI command and parameter, reset high
5 ms / low 10 ms / high then 120 ms, the vendor's 59-entry table (it ends with its own sleep-out
and display-on, so the board layer sends neither), then the DPI panel at 50 MHz, h 20/136/160,
v 2/12/20, RGB565, one frame buffer, DMA2D on. Orientation is upright, touch axes are not
mirrored, and the image went up on the first try once the rails were there.

**Three "traps" written here during the bring-up were symptoms of the unpowered panel, not
properties of the board**, and are corrected rather than deleted, because the misreading is the
lesson:

- The DSI command FIFO filling and blocking forever at the twentieth command, inside
  `esp_lcd_panel_io_tx_param`, is what an unpowered panel looks like: nothing drains the queue.
  Powered, all 59 commands go through **before** the video starts, in the normal order
  (`PANEL_INIT_AFTER_DPI_START = false`).
- Reading the panel's ID hung for the same reason. The board layer still never reads it: there is
  nothing to gain, and Espressif's own JD9165 driver does it, which is why a straight port hangs.
- "No image even from the DSI bridge's hardware test pattern" was the clearest proof the panel
  was off, and it was read as a timing problem instead. A probe that bypasses the frame buffer,
  LVGL and the PPA and still shows nothing points at power or reset, not at configuration.

What stays true:

- 🪤 **The PHY reference clock and the lane rate go together.** XTAL with 1000 Mbps made
  `esp_lcd_panel_init` never return (seen before the rails were fixed, not re-tested since). The
  vendor's own choice, the driver default reference at 600 Mbps, works.
- 🪤 **Its CH340 drops bytes on a long transfer**: a 16 MB read died after 284 KB at 460800 and
  again at 230400 in one go. Reading in retryable 512 KB pieces at 230400 works (one retry in 32).
  Flashing at 230400 is reliable. Opening the port resets the board **even with DTR and RTS both
  deasserted** (2026-11-16): the trick that lets a CrowPanel be watched without restarting it does
  not work here, so every console command on this board costs a reboot and about 30 s before it has
  rejoined Wi-Fi, re-paired and found its zone again. A pending-verify OTA window must be left alone.
- 🪤 **16 MB of flash**: the image header has to say 16 MB or the bootloader refuses to probe.
- 🪤 **The C6 Wi-Fi coprocessor has no update path on this board** (found 2026-11-19 while mapping
  where the flash goes). `src/net/coprocessor.cpp` compiles its updater only under
  `BOARD_WAVESHARE_P4_7B`, so on this board `updateIfNeeded()` is the empty stub, and the
  `assets/hosted/esp32c6.bin` blob that `platformio.ini` embeds for this env is never referenced
  and the linker drops it — which is why the VIEWE image is 2,180,480 B while the Waveshare's is
  3,508,320 B, 1,226,976 of them that one blob. Nothing is wrong with the image, but two things
  follow: the embed line in this env is a dead build dependency (it still makes `tools/hosted_fw.py`
  fetch and verify a blob nobody uses), and **a VIEWE whose C6 ships with older firmware than ours
  expects would have no way to be updated** — this one's C6 happens to be compatible, and its boot
  log simply prints no `[c6]` line at all. The Waveshare's factory Wi-Fi chip did fail exactly that
  way, so a different unit is a plausible support case. Neither is fixed: changing the env costs a
  rebuild, and the guard should probably become a board capability rather than a board name.
- 🪤 **Vendor sources beat look-alike sources.** Every published table, timing, reset pin, pixel
  format, lane rate and clock source for boards with the same controller was tried and none of it
  mattered, because the missing piece was not in the display configuration at all. The BSP for the
  exact product code answered it in minutes.

The board's factory image is backed up whole and verified against the chip:
`~/Documents/TFT-Panel-backups/viewe-p4-s070-factory-<mac>.bin` (+ `.sha256`). Restore with
`esptool --baud 230400 write-flash 0 <file>`; the first 4 MB are enough, the demo's assets above
8 MB are never touched by our firmware. The board now runs the panel firmware.

### Guition JC1060P470C (asked about 2026-11-11, still no board)

A user asked for this one on 2026-11-11. It is a different product from the S070 above, with the
same JD9165 controller: 1024x600 MIPI-DSI, GT911, 32 MB PSRAM, 16 MB flash, ESP32-C6 for Wi-Fi.
Community configurations put its backlight on GPIO 23 and its reset on GPIO 0 (2026 batch) or
GPIO 27 (earlier). Nobody on the project has one, and the S070 bring-up above shows that sharing
a controller is not enough to share a configuration.

Still to do for it: its own pin map (the S070's values above are not transferable, as that
bring-up showed), and a board choice on the install page, since the installer picks by chip and
two P4 boards now share one.

