# Roadmap

## Phase 0 — hardware bring-up (in progress)

- [x] Toolchain: PlatformIO + pioarduino 55.03.312-1, LVGL 9.6.0, builds and flashes (460800 baud)
- [x] PSRAM 8 MB / flash 4 MB detected, RGB panel + bounce buffers created, LVGL running
- [x] PCA9557 found at 0x18, touch reset sequence applied
- [x] GT911 touch: answered once the touch flex was seated (id 911, fw 0x1060, 0x5D)
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
- [x] End-to-end on the real network: phone setup, mDNS, zone, synced lyrics, track change
- [x] Internal RAM kept for Wi-Fi: LVGL and JSON allocate in PSRAM (steady ~120 KB free)
- [x] Lyrics layout bounded (neighbours 1 line, current ≤ 2, "…"), all of General Punctuation in the
      font (U+2010 in "Jean‐Luc" showed as a box), FPS overlay off; checked with `demo` + `screenshot`
- [x] Timing: PositionClock (window ~36 ms on the board) + 250 ms display lead
- [ ] Vincent's eye check on the real LCD after these fixes (the framebuffer is right; the panel itself is unverified)
- [ ] Flash budget: the image is at 85 % of an OTA slot (Wi-Fi + HTTP + web server ≈ 900 KB)

## Phase 2 — lyrics and controls UI (in progress)

- [x] Lyrics stage: current line large, neighbours dimmed, per-track offset + display lead
- [x] Controls bar on tap (auto-hides after 6 s): play/pause, prev/next, volume −/+ (number, dB,
      incremental; hidden when the zone has none), zone picker with "follow the playing zone"
- [x] Cover as a blurred, darkened full-screen backdrop (`backdrop on|off`)
- [x] Smooth scroll between lyric lines (`scroll on|off`) — to check by eye on the LCD
- [x] Cover thumbnail next to the title (one 56 px fetch feeds thumbnail and backdrop)
- [x] Screen dimming / sleep when nothing plays (`sleep <min>|off`, a touch wakes it) — dim, off and
      wake checked on the board with Vincent
- [x] Cover view: big cover + large title/artist + current lyric line, circular swipe between
      views, saved choice, automatic when a track has no lyrics
- [x] Flickering dots on the LCD: gone (fewer redraws, 20-line bounce buffers, 64-byte cache lines) — confirmed by eye

## Phase 2b — product basics (done 2026-11-07, waiting for the first release)

- [x] Final partition table (2 × 1984 KB OTA slots + coredump), firmware version from git tags
- [x] OTA from GitHub releases: daily check, header badge, on-screen confirmation, SHA-256, rollback
- [x] Local upload page with an on-screen code; install + rollback verified on the board
- [x] Release script + browser installer (ESP Web Tools on GitHub Pages) — first release not published yet
- [x] Settings screen, idle clock (SNTP + time zone), volume slider + mute, per-track lyrics offset

## Second screen: Waveshare ESP32-P4 7B (2026-11-11)

- [x] Board layer split per screen (`src/board/board_*.cpp`, `pins_*.h`), shared restart and colour warmth
- [x] Waveshare ESP32-P4-WIFI6-Touch-LCD-7B: MIPI-DSI 1024×600 (EK79007, turned 180°), GT911, backlight,
      Wi-Fi through the ESP32-C6 (factory C6 firmware works) — end to end with the companion
- [x] Interface scaled from the 800×480 grid (`src/layout.h`), fonts and covers included
- [x] Releases carry both boards; web installer picks by chip (ESP Web Tools 10.4.0)
- [x] Over-the-air update on the P4 from a real release (fake-older build, `ota install`) and from the
      local upload page — after two fixes: C6 firmware update at boot, image received whole before writing
- [ ] Vincent's eye check of the P4 screen (colours, warmth, touch comfort)
- [ ] Browser install of the P4 from the website (needs a person at the browser's port picker)
- [x] C6 coprocessor firmware installed at boot from the P4 image (the factory one carried no Internet)
- [ ] Use the P4's spare power later: hardware JPEG decoder for covers, PPA for the rotation, bigger fonts

## Wi-Fi setup (2026-11-11)

- [x] Setup page lists the networks the panel hears (tap to fill the name, signal, lock)
- [x] On-screen Wi-Fi setup: network list + password on a touch keyboard, from the setup screen and Settings
- [x] Fixed: the portal could not bind port 80 when opened with Wi-Fi up (the update page held it)
- [ ] Vincent's finger test of the on-screen flow (pick, type, connect)

## Music library (Waveshare P4, 2026-11-11)

- [x] Search and browse Roon's library from the panel, play / queue / radio on the followed zone
      (own Roon browse session; `docs/ARCHITECTURE.md`)
- [x] Vincent's finger test on the P4 (2026-11-12): search, results and playback work
- [x] Text fields show a visible blinking green cursor while they wait for typing (v0.1.15; the
      theme's thin cursor did not blink when the field was focused by the code)
- [ ] CrowPanel: later if it works well (~207 KB of flash left; lists slower there)

## Other boards (not started)

- **Guition JC1060P470C** (ESP32-P4, JD9165, 16 MB flash) — asked by a user 2026-11-11; about half a day
  of code, but blocked until someone can test on the board. Details: `docs/HARDWARE.md#other-boards`.
- Install page: a board choice for boards that share a chip (needed as soon as a second P4 board is added).

## To discuss (estimated 2026-11-07, not started)

- **Multi-language UI** — ~120 strings, 3–6 KB of flash per language, about a day for English +
  French (UI + setup portal), then mostly translation. Latin Extended-A already covers FR, DE, ES,
  IT, PT, NL, PL, CS, SV… Greek / Cyrillic: +30–50 KB (two font weights), which would also make
  lyrics in those scripts readable (boxes today). CJK: too big for flash, needs the microSD.
- Smaller ideas: a quick small cover before the big one, a French user guide / site, release CI.

## Phase 3 — polish

- Companion push channel (SSE/WebSocket) instead of polling
- Wider scripts (CJK…) from the microSD card (tiny_ttf)
- Double framebuffer + vsync if tearing shows; PCLK tuning
- Enclosure / stand, release binaries (web flasher)
