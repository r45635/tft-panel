# Architecture

## What TFT Panel is

A dedicated 7" touch screen for the living room: it shows the **synced lyrics** of what is
playing in Roon, the cover and track, and drives play/pause/skip/volume/zone.

It is a **client of the TextFromTrack Roon Companion** ([r45635/roon.app.textfromtrack.com](https://github.com/r45635/roon.app.textfromtrack.com))
over its LAN HTTP API — the same contract the Apple Watch remote uses.

```
 Roon Core ──(Roon extension API)── TFT Companion (Node, :3888) ──HTTP/JSON over LAN── TFT Panel (ESP32-S3)
                                         │
                                         └─ lyrics: local .lrc / tags / LRCLIB mirror / TextFromTrack
```

- The panel never talks to Roon directly, never to a server of ours, and stores no lyrics.
- The companion is the single source of truth; the panel is stateless apart from its settings
  (Wi-Fi, companion address, chosen zone, brightness) in NVS.
- Name never contains "Roon" (Roon Labs trademark).

## Companion API contract (as of companion v0.5.15)

| Need | Endpoint | Notes |
|---|---|---|
| Zones | `GET /api/roon/zones` | `outputs[].volume` can be `null`; volumes are `db` or `incremental` |
| Now playing | `GET /api/roon/now-playing?zone_id=` | position in whole seconds → extrapolate locally |
| Lyrics | `GET /api/lyrics/current?zone_id=` | `offset_ms` > 0 = shown later → lyric time = position − offset |
| Control | `POST /api/roon/control` | always send `zone_id`; 404 on an idle zone |
| Cover | `GET /api/roon/image/:key?scale=fit&width=&height=` | ask for the exact pixel size; JPEG decoded on device |

Rules learned on the watch client:
- **Always pass `zone_id`. Never call `POST /api/roon/active-zone`** — it is global to the
  companion and would change the zone for every other client.
- The companion guards accept a no-Origin client and an IP or `.local` Host (`hostGuard` in
  `src/utils/requestGuards.js`), so an ESP32 can call it as is. Optional `ACCESS_TOKEN` must be supported.
- No push channel for playback yet → poll (1–2 s) and extrapolate the position between polls.
  A companion SSE/WebSocket endpoint is a later improvement (the ESP32 can hold one, the watch cannot).
- Discovery: the companion announces Bonjour `_textfromtrack._tcp` (TXT `hostname` + port);
  the ESP32 can browse mDNS itself.

## Firmware layers

| Layer | Path | Role |
|---|---|---|
| Board | `src/board/` | one layer per screen behind `board.h`: pins, touch, panel driver (`esp_lcd`), LVGL glue, backlight, colour warmth |
| Layout | `src/layout.h` | screen size and the 800×480 design grid's scaling (`X`, `Y`, `S`) |
| Net | `src/net/` | settings (NVS), Wi-Fi + setup portal, mDNS discovery, companion polling, OTA |
| App | `src/app/` | lyrics model (LRC), fonts, covers, screens (setup, now playing, settings; bring-up via `bringup`) |

**Boards.** `board.h` is the only interface the rest of the firmware sees; each board compiles
its own file, chosen by the environment's `-DBOARD_…` flag: `board_crowpanel7.cpp` (ESP32-S3, RGB
panel fed from bounce buffers, frozen update screen) and `board_waveshare_p4_7b.cpp` (ESP32-P4,
MIPI-DSI panel fed by the 2D-DMA from two internal-RAM strips, image turned 180° on the way). Shared
by both: `restart.cpp` (clean resets), `white_balance.cpp` (warmth table), `gt911.cpp`. Per-board
constants (`pins_*.h`): resolution, `BOARD_NAME`, the release asset name `BOARD_ASSET`,
`HAS_STATIC_SCREEN`, `DEFAULT_WARMTH`, `OTA_DOWNLOAD_FIRST`. Wi-Fi on the P4 goes through the
on-board ESP32-C6 (esp_hosted, transparent to `WiFi.h`); the firmware carries the C6's matching
firmware and installs it at boot when needed (`src/net/coprocessor.cpp`). On the P4 an update is
received whole into PSRAM before it is written: flash writes stall the SDIO link to the C6.

**Scaling.** Layout code is written on the 800×480 grid: `X()` for horizontal positions and widths,
`Y()` for vertical ones, `S()` for things that keep their proportions (fonts, icons, round
buttons, covers, radii). Identity on the CrowPanel; on 1024×600, ×1.28 / ×1.25 / ×1.25. Fonts are
rendered at run time (tiny_ttf), so a scaled size costs nothing; LVGL's symbol fonts are fixed
sizes, so the P4 build adds Montserrat 18/24/36 for them.

Threads: the **net task** (core 0, next to the Wi-Fi stack) owns every network call and publishes
an immutable `net::Snapshot` under a mutex, with a version counter. **LVGL runs only in `loop()`**
(core 1): `ui::tick()` copies the snapshot when the version moves, then extrapolates the position
every frame. Lyrics are a `shared_ptr<const Lyrics>`, so copying a snapshot never copies them.

### Setup flow

1. No Wi-Fi stored → soft AP `TFT-Panel-XXXX` (WPA2, password generated once per device and printed
   on screen) + captive DNS. The screen shows a `WIFI:` QR code and a QR code to `http://192.168.4.1/`.
2. The page (GET = read only) lists the ten strongest scanned networks as buttons that fill the
   name field (a hidden one is typed); `POST /save` validates and stores Wi-Fi, optional companion
   address and token, then the panel restarts.
   Or on the panel: **Set up on this screen** (also Settings > Wi-Fi > Change) lists the networks and
   takes the password on LVGL's keyboard (`src/app/wifi_screen.cpp`); saving restarts the same way.
   Scans run in the net task (`tickScan`, async): on request, when the portal opens, and every 30 s
   while no phone is on the AP; results go out in `Snapshot::networks`.
   The portal takes port 80 from the update page first (it waits for `webupdate::serving()` to drop).
3. Stored Wi-Fi failing for 60 s → the AP comes back next to the station, so a changed password can be fixed.

### Music library (HAS_LIBRARY boards: the Waveshare P4)

`src/net/library.cpp` (net task) talks to the companion with **`POST /api/roon/browse` and
`/browse/load` only**, always with the panel's own `multi_session_key` (its host name), so its
browse stack in Roon is its own: it never moves the desktop app's or the web player's. Never
`GET /search` or `POST /play-item`: they use Roon's shared default session. Every step on an item
carries `zone_or_output_id` = the followed zone, so actions play there.

- Search: hierarchy `search` opened with `pop_all` + `input`; empty categories ("0 …") hidden.
  Browse: hierarchy `browse` from Roon's root.
- Levels: the panel keeps the shown levels and Roon's current level; an action menu leaves Roon one
  level deeper, popped (`pop_levels`) before the next step. An album behind a single-item level is
  opened at once (Back pops both). Pages of 50 (`offset`), "Show more".
- Actions: an `action_list` item opened with the zone lists Roon's `action`s (sometimes one level
  deeper); running one is `POST /browse {item_key, zone_or_output_id}`.
- A 502 (stale `item_key`) reopens the hierarchy. Thumbnails (`/api/roon/image/<key>`, the list's
  image for tracks) load one by one while the screen is open, LRU cache of 150 in PSRAM.
- Published as `Snapshot::library` (copy-on-write); `src/app/library_screen.cpp` draws it; UI
  commands go through `net::library(op, index, text)`. Console: `lib search <text> | browse |
  open <n> | back | more | actions <n> | show | ui` (`lib action <n>` plays: not while someone listens).

### Following the playback

- Zone: the pinned one (`zone <id>` on the console) or, by default, keep the current zone while it
  plays, else the first playing zone. Never `POST /active-zone`.
- `now-playing` every 300 ms. Roon gives whole seconds; `src/net/position_clock.h` turns each sample
  into a window for the offset between the local clock and the true position (`[seek, seek + 1 s)`,
  widened by half the round trip) and intersects the windows: it converges on the instant each
  second ticks over (simulated: ~10 ms mean error, ~40 ms worst; a plain "seek + clamp" lagged
  ~300 ms). An empty intersection = a seek: start over.
- Each line is shown `lead` ms before its timestamp (default 250, `lead <ms>` on the console), then
  the per-track `offset_ms` from the companion applies.
- Lyrics fetched on each new track (the call waits while the companion resolves them: 20 s
  timeout); "not found" is retried up to 3 times, once a minute.
- Commands (control, volume, zone) go through a FreeRTOS queue to the net task, so a tap never
  blocks the UI: `POST /api/roon/control {action, zone_id}`, `POST /api/roon/volume {output_id,
  how: relative_step, value: ±1}` on every output of the zone. A failure shows in red for 5 s.
- Cover (`src/app/cover.*`, built by its own task as soon as the track changes, in parallel with
  the lyrics; a request superseded by a newer track is dropped): one `GET /api/roon/image/<key>?w=384&h=384`
  (20–90 KB; the companion fits it, so a non-square cover comes back e.g. 384×350), decoded by
  TJpgDec into the cover view's picture. An area-average copy ≤ 56 px gives the thumbnail left of
  the title and, unless `backdrop off`, the backdrop: 2 box-blur passes, bilinear scale to cover
  800×480, darkened to ~35 %, ordered dither to RGB565. ~1–1.5 s in the net task; ~300 KB + 768 KB
  PSRAM. If the big JPEG cannot be decoded (progressive), the 56 px one still gives thumbnail and backdrop.
- Two views, switched by a horizontal swipe that wraps around (left = next, right = previous),
  instantly — a full-screen frame costs ~130 ms, an animated wipe would get 3–4 frames. Lyrics view;
  cover view (384 px cover, title 44 px, artist 30 px, album, current lyric line, progress). Two dots
  at the top show the page. A track without lyrics shows the cover view; as soon as lyrics are
  found (a retry or the next track) the saved view comes back. A swipe on a track with lyrics is
  saved in NVS (`view lyrics|cover` on the console); on a track without lyrics it only holds for
  that track while it has none, so the panel never stays stuck on covers. No track → lyrics view
  (it carries the status message). `lv_indev_wait_release` after a swipe: the finger's
  release never clicks the screen or a button it started on.
- Header: the TextFromTrack mark (same drawing and colours as the companion's app icon and web
  top bar; `tools/tft_icon.py` → `src/app/tft_icon.c`, 24 px RGB565A8) before the zone name.
- Long texts (title, artist, status, zone button, zone list, idle-clock track) bounce-scroll
  slowly (LVGL `LONG_MODE_SCROLL`, ~35 px/s) instead of ending in "…". In the cover view a text
  that fits its 2 lines wraps, a longer one goes on one scrolling line. Lyric lines are not
  scrolled (a moving line while singing would distract).
- Lyrics slide up when the next line comes (320 ms ease-out, `scroll on|off`): the rows sit in a
  box translated inside a fixed, clipping stage; the incoming last line fades in. Seeks, track
  changes and going back jump.
- Screen sleep (UI, not board): without playback for 1 min the backlight fades to 12 %, after
  `sleep <min>` (default 10, `off` = never) it goes off — never during setup, the zone picker or
  `demo`, and never off when there is no touch to wake it. A touch wakes it; the touch that wakes
  a dark screen is swallowed at the input device (`lv_indev_stop_processing`) so it presses nothing.
- Idle clock: SNTP (`pool.ntp.org`) starts once Wi-Fi is up; the time zone is a POSIX TZ string
  applied by the UI (`settingsui::applyTz`). While the screen is dimmed and nothing plays, a
  128 px clock with the date (and the paused track) covers the screen, redrawn once a minute.
- Settings screen (`src/app/settings_screen.*`, gear button in the controls bar): brightness,
  screen-off delay, lyrics lead, backdrop / scroll / clock switches, time zone, zone, forget
  Wi-Fi, firmware version + check/install, about (IP, companion, free RAM). Every change is saved
  to NVS at once; the screen talks back to the main UI through `settingsui::Hooks`.
- Firmware updates (`src/net/ota.*`, net task): 30 s after boot and every 24 h, HTTPS GET of
  GitHub's `releases/latest` (built-in CA bundle, TLS in PSRAM). A newer `vX.Y.Z` tag with
  assets `tft-panel-<tag>.bin` + `.sha256` shows a header badge; a tap opens a confirmation,
  then the image streams into the other app slot while it is hashed (up to 3 attempts on a
  transport error, the update screen frozen in internal RAM meanwhile), and only a matching
  SHA-256 is committed. The new image boots "pending verify": it is confirmed once Wi-Fi is up
  and the companion answered, otherwise after 3 min the bootloader goes back to the previous
  one. Console: `ota check|install`.
- Local upload page (`src/net/web_update.*`): `http://tft-panel-xxxx.local/update` in station mode
  (not while the setup portal owns port 80). Opening it shows a 6-digit code on the panel for
  5 min; the upload must carry it (`?code=`), else 403 before anything is written. Same
  pending-verify rollback as a GitHub update.
- Every failure becomes the status line (401 → token needed, 503 → Roon Core not connected, transport
  errors by name). A discovered companion is dropped after 6 failures in a row and looked up again.

## Key technical choices

- **PlatformIO + pioarduino** (Arduino-ESP32 3.3.12 on ESP-IDF 5.5), pinned in `platformio.ini`.
  The official PlatformIO platform is stuck on Arduino 2.x / IDF 4.4. Native toolchain on the dev
  Mac: Docker on macOS has no USB passthrough, so it cannot flash or read the serial port.
- **`esp_lcd` RGB panel directly, not LovyanGFX**: IDF 5's bounce buffers keep the RGB DMA reading
  internal SRAM, which avoids the image drifting when Wi-Fi or flash access loads PSRAM.
- **LVGL 9.6**, partial rendering into one 48-line internal-RAM strip, copied into a single PSRAM
  framebuffer. The flush is a synchronous CPU copy, so a second strip bought nothing; one strip of
  twice the height halves the passes over the text. Double framebuffer + vsync swap remains the
  step if tearing shows.
- **Framework rebuilt with 64-byte data cache lines** (`custom_sdkconfig` in `platformio.ini`,
  pioarduino hybrid compile): twice the PSRAM burst per cache miss. Measured on a lyrics slide
  (760×340 px over the backdrop): 110 → 88 ms per frame, flush 30 → 20 ms. The rest is PSRAM
  bandwidth shared with the RGB scan-out (~25 MB/s): ~11 fps for full-stage animations.
- **Partition table is final** (`partitions.csv`): two 1984 KB app slots for OTA with rollback,
  64 KB coredump, no filesystem. OTA can never change it; a new layout means a USB flash for
  every user, so it was fixed before the first release.
- **Firmware version** from `git describe` (`tools/version.py` writes `src/generated/version.cpp`,
  not in git; only that file recompiles). Releases are tags `vX.Y.Z`; an untagged build is
  `v0.0.0-g<sha>`.
- **TLS buffers in PSRAM** (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`) and the common CA subset, for the
  HTTPS update check without eating the internal RAM Wi-Fi needs.
- **Own minimal GT911 / PCA9557 drivers** (~100 lines) instead of Elecrow's bundled libraries.
- PCLK starts at 16 MHz (≈ 33 fps); Elecrow runs 15–24 MHz.
- **Fonts: Noto Sans TTF rendered at run time by LVGL `tiny_ttf`**, not `lv_font_conv` bitmaps.
  Two weights subset to Latin + Latin Extended-A + typographic punctuation weigh 23 KB each, against
  ~220 KB of bitmaps for four fixed sizes, and any size stays possible. Glyphs are cached (256 per
  font). Built by `tools/fonts/build_fonts.sh` (pinned google/fonts commit and fonttools), embedded
  with `board_build.embed_files`. Montserrat stays as the fallback for LVGL symbols. CJK can later come
  from the microSD card the same way.
- **ArduinoJson 7.4.2** for the companion's JSON.
- **Memory**: internal RAM is kept for the Wi-Fi/lwIP stack and the two LVGL draw strips. LVGL
  (objects, texts, glyph caches) uses a PSRAM allocator (`LV_STDLIB_CUSTOM`,
  `src/board/lv_mem_psram.c`); ArduinoJson documents use a PSRAM allocator and parse straight from the
  socket (HTTP/1.0, no body `String`).
- 4 MB flash: `min_spiffs.csv` = two 1.9 MB OTA slots. Image: 752 KB at phase 0, **1.66 MB (85 %)
  with Wi-Fi, HTTP, web server and mDNS**. Next cuts if needed: log level, the web server (Improv
  serial instead), or a single-slot layout with OTA from the companion.
