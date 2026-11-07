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
| Board | `src/board/` | pins, PCA9557, GT911, RGB panel (`esp_lcd`), LVGL glue, backlight |
| App | `src/app/` | screens (bring-up now; lyrics, controls, settings next) |
| Net (next) | `src/net/` | Wi-Fi, mDNS discovery, companion HTTP client, JSON parsing |

## Key technical choices

- **PlatformIO + pioarduino** (Arduino-ESP32 3.3.12 on ESP-IDF 5.5), pinned in `platformio.ini`.
  The official PlatformIO platform is stuck on Arduino 2.x / IDF 4.4. Native toolchain on the dev
  Mac: Docker on macOS has no USB passthrough, so it cannot flash or read the serial port.
- **`esp_lcd` RGB panel directly, not LovyanGFX**: IDF 5's bounce buffers keep the RGB DMA reading
  internal SRAM, which avoids the image drifting when Wi-Fi or flash access loads PSRAM.
- **LVGL 9.6**, partial rendering into two 24-line internal-RAM strips, copied into a single PSRAM
  framebuffer. Possible tearing on large animations; double framebuffer + vsync swap is the next
  step if lyrics scrolling shows it.
- **Own minimal GT911 / PCA9557 drivers** (~100 lines) instead of Elecrow's bundled libraries.
- PCLK starts at 16 MHz (≈ 33 fps); Elecrow runs 15–24 MHz.
- 4 MB flash: `min_spiffs.csv` = two 1.9 MB OTA slots. Image at phase 0: 752 KB (38 %).
  Fonts are the budget to watch.
