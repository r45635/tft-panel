# Roadmap

## Phase 0 — hardware bring-up (in progress)

- [x] Toolchain: PlatformIO + pioarduino 55.03.312-1, LVGL 9.6.0, builds and flashes (460800 baud)
- [x] PSRAM 8 MB / flash 4 MB detected, RGB panel + bounce buffers created, LVGL running
- [x] PCA9557 found at 0x18, touch reset sequence applied
- [ ] **GT911 not answering** — TP-RESET reads low when released: check the touch flex (docs/HARDWARE.md)
- [ ] Visual check: colour bars in R/G/B order, gradient smooth, no drift or flicker
- [ ] Touch mapping (dot follows the finger, axes not mirrored), backlight slider (PWM 5 kHz)

## Phase 1 — connected client

- Wi-Fi provisioning from the touch screen (on-screen keyboard), credentials in NVS only
- Companion discovery via mDNS `_textfromtrack._tcp`, manual address fallback, optional `ACCESS_TOKEN`
- HTTP client + JSON (zones, now-playing, lyrics), polling with local position extrapolation
- Generated fonts (`lv_font_conv`): Latin-1 + Latin Extended-A at 2–3 sizes, open licence font

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
