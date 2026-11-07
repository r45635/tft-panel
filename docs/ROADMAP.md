# Roadmap

## Phase 0 — hardware bring-up (in progress)

- [x] Toolchain: PlatformIO + pioarduino 55.03.312-1, LVGL 9.6.0, builds and flashes (460800 baud)
- [x] PSRAM 8 MB / flash 4 MB detected, RGB panel + bounce buffers created, LVGL running
- [x] PCA9557 found at 0x18, touch reset sequence applied
- [ ] **GT911 not answering** — TP-RESET and TP-INT behave like an open net: check the touch flex (docs/HARDWARE.md)
- [ ] Visual check: colour bars in R/G/B order, gradient smooth, no drift or flicker
- [ ] Touch mapping (dot follows the finger, axes not mirrored), backlight slider (PWM 5 kHz)

## Phase 1 — connected client (in progress)

- [x] Settings in NVS only (`src/net/settings.*`): Wi-Fi, companion address, access token, pinned zone
- [x] Wi-Fi setup without touch: WPA2 soft AP `TFT-Panel-XXXX` (password random once per device, kept
      in NVS) + captive page; the setup screen shows two QR codes (join the AP, open the page)
- [x] Companion discovery via mDNS `_textfromtrack._tcp`, manual address fallback, optional `ACCESS_TOKEN`
- [x] HTTP client + JSON (zones, now-playing, lyrics) in a core-0 task, 500 ms polling, position
      extrapolated locally and clamped to Roon's whole-second window
- [x] Lyrics fonts with accents: Noto Sans Latin + Latin Extended-A, rendered at run time (tiny_ttf)
- [x] Serial console for development (`help`)
- [ ] End-to-end check on the real network (needs the Wi-Fi set once from a phone)
- [ ] Flash budget: the image is at 85 % of an OTA slot (Wi-Fi + HTTP + web server ≈ 900 KB)

## Phase 2 — lyrics and controls UI

- Lyrics stage: current line large, neighbours dimmed, smooth scroll, per-track offset respected
- Now playing bar: cover (JPEG at exact size), title/artist, progress
- Controls: play/pause, prev/next, volume (db / incremental / null), zone picker
- Screen dimming / sleep when nothing plays

## Phase 3 — polish

- OTA updates (from GitHub releases or the companion)
- Companion push channel (SSE/WebSocket) instead of polling
- Wider scripts (CJK…) from the microSD card (tiny_ttf)
- Double framebuffer + vsync if tearing shows; PCLK tuning
- Enclosure / stand, release binaries (web flasher)
