# TFT Panel — notes for agents

ESP32-S3 firmware (CrowPanel 7" DIS08070H V3.0) showing synced lyrics + Roon controls, as a
client of the TextFromTrack Roon Companion HTTP API. Public repo, MIT.

Read first: `docs/ARCHITECTURE.md` (API contract, choices), `docs/HARDWARE.md` (pins, pitfalls,
bring-up log), `docs/ROADMAP.md` (current phase).

## Commands

```bash
pio run                                              # build (first build ~6 min + framework rebuild ~2 min, then ~30 s)
pio run -t upload --upload-port /dev/cu.usbserial-*  # flash; 921600 fails on this board, 460800 works
pio device monitor                                   # 115200, exception decoder on
PLATFORMIO_BUILD_FLAGS="-DTOUCH_DIAG" pio run -t upload   # touch reset diagnostic at boot
```

From an agent shell (no TTY), read the serial log with pyserial from PlatformIO's Python:
reset with `rts=True` → `False` (`dtr=False`), then read for a few seconds.
Changing `PLATFORMIO_BUILD_FLAGS` rebuilds the whole framework (~3 min).
`custom_sdkconfig` (64-byte cache lines) makes pioarduino rebuild the IDF libraries into the
**global** `~/.platformio/packages/framework-arduinoespressif32-libs`. 🪤 With pioarduino 55.03.312-1 its
second stage runs PlatformIO 6.2.0.post1 from `~/.platformio/penv`, which the platform rejects
("needs >=6.2.0") and **uninstalls the platform**. Fix: reinstall it
(`pio pkg install -e crowpanel7 -p <platform URL from platformio.ini>`), then `pio run` again —
the rebuilt libraries are kept and the build goes through.
Serial console (115200, type `help`): `status`, `portal`, `companion <url>|auto`, `zone <id>|auto`,
`token`, `lead <ms>`, `backdrop on|off`, `scroll on|off`, `sleep <min>|off`, `view lyrics|cover`, `ota check|install`, `settings [close]`, `tz <POSIX>|default`, `warmth <0-100>`, `colours`, `backlight <0-100>`, `forget-wifi`, `erase-all`, `bringup` (colour bars + touch tracker), `restart`,
`screenshot` → `tools/screenshot.py <log> out.png` (shows lyrics: keep it out of the repo);
`demo` freezes the screen on invented lyrics — use it for layout checks and doc screenshots
(the real cover thumbnail and backdrop stay on screen: cover art is copyrighted too).
`touchdiag` replays the touch reset variants live and attaches the touch if the GT911 answers.
`backdrop off` before `screenshot` makes it fast (a dithered backdrop does not compress: ~3 min at 115200).
Docs/promo media: `tools/showcase.py --port … --host <panel IP>` (console `demo` showcase: invented
song, lyrics, zones, generated cover; frames over Wi-Fi with `shots on`, ~4 s each) → `docs/images/`;
`uv run --with pillow==11.0.0 tools/promo.py` (device frame, hero/square/banner/grid) and
`tools/animate.sh` (MP4 + GIF) → `docs/media/`. Only showcase pictures may be committed.
Fonts: `tools/fonts/build_fonts.sh` rebuilds `assets/fonts/` (never `npx` a converter: run-time tiny_ttf).

## Rules

- Companion API: always send `zone_id`; **never** `POST /api/roon/active-zone` (global).
- Never put Wi-Fi credentials, tokens or a companion address in the repo — NVS only (setup portal or console).
- Never "Roon" in the product name. Never commit screenshots showing real lyrics (copyright).
- Pin versions (platform, libraries). Keep `src/board/` free of app logic.
- English for code, comments, docs and commits. No assistant attribution in commits.
- After a hardware finding, update the bring-up log / pitfalls in `docs/HARDWARE.md`.
