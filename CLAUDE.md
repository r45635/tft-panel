# TFT Panel — notes for agents

ESP32-S3 firmware (CrowPanel 7" DIS08070H V3.0) showing synced lyrics + Roon controls, as a
client of the TextFromTrack Roon Companion HTTP API. Public repo, MIT.

Read first: `docs/ARCHITECTURE.md` (API contract, choices), `docs/HARDWARE.md` (pins, pitfalls,
bring-up log), `docs/ROADMAP.md` (current phase).

## Commands

```bash
pio run                                              # build (first build ~6 min, then ~30 s)
pio run -t upload --upload-port /dev/cu.usbserial-*  # flash; 921600 fails on this board, 460800 works
pio device monitor                                   # 115200, exception decoder on
PLATFORMIO_BUILD_FLAGS="-DTOUCH_DIAG" pio run -t upload   # touch reset diagnostic at boot
```

From an agent shell (no TTY), read the serial log with pyserial from PlatformIO's Python:
reset with `rts=True` → `False` (`dtr=False`), then read for a few seconds.
Changing `PLATFORMIO_BUILD_FLAGS` rebuilds the whole framework (~3 min).
Serial console (115200, type `help`): `status`, `portal`, `companion <url>|auto`, `zone <id>|auto`,
`token`, `lead <ms>`, `forget-wifi`, `erase-all`, `bringup` (colour bars + touch tracker), `restart`,
`screenshot` → `tools/screenshot.py <log> out.png` (shows lyrics: keep it out of the repo);
`demo` freezes the screen on invented lyrics — use it for layout checks and doc screenshots.
Fonts: `tools/fonts/build_fonts.sh` rebuilds `assets/fonts/` (never `npx` a converter: run-time tiny_ttf).

## Rules

- Companion API: always send `zone_id`; **never** `POST /api/roon/active-zone` (global).
- Never put Wi-Fi credentials, tokens or a companion address in the repo — NVS only (setup portal or console).
- Never "Roon" in the product name. Never commit screenshots showing real lyrics (copyright).
- Pin versions (platform, libraries). Keep `src/board/` free of app logic.
- English for code, comments, docs and commits. No assistant attribution in commits.
- After a hardware finding, update the bring-up log / pitfalls in `docs/HARDWARE.md`.
