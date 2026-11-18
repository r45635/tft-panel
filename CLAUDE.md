# TFT Panel — notes for agents

ESP32 firmware showing synced lyrics + Roon controls on three 7" screens — Elecrow CrowPanel 7"
DIS08070H V3.0 (ESP32-S3, env `crowpanel7`, the default), Waveshare ESP32-P4-WIFI6-Touch-LCD-7B
(env `waveshare-p4-7b`) and VIEWE ESP32-P4 7" UEP4S070H1024V600C-WB-C (env `p4-s070-wba`). It reads
what is playing either as its own extension of the music server (direct mode) or as a client of the
TextFromTrack Roon Companion HTTP API (companion mode); lyrics come from a companion, any
LRCLIB-compatible server, or nowhere. Public repo, MIT.
The two P4 boards share `src/board/board_p4_mipi.cpp`; they differ by their `pins_*.h`, flash size and
console (VIEWE: CH340 on UART0; Waveshare: the P4's native USB).

Read first: `docs/ARCHITECTURE.md` (API contract, choices), `docs/HARDWARE.md` (pins, pitfalls,
bring-up log), `docs/ROADMAP.md` (current phase), `docs/HARDWARE_TEST.md` (what the maintainer runs
on each board; append the steps only real hardware can prove).

## Commands

```bash
tools/backup-firmware.py --port /dev/cu.usbserial-*  # identify the board, dump its flash, verify the copy
                                                     # (read baud is below the flash baud: 230400 on both CH340s;
                                                     #  the VIEWE reads in retried 512 KB pieces)
python3 -I tools/test_backup_firmware.py             # host test of that tool, esptool mocked (no board)
pio run                                              # CrowPanel build (first build ~6 min + framework rebuild ~2 min, then ~30 s)
pio run -t upload --upload-port /dev/cu.usbserial-*  # CrowPanel flash; 921600 fails on this board, 460800 works
pio run -e waveshare-p4-7b -t upload --upload-port /dev/cu.usbmodem*   # Waveshare P4 (native USB, 921600 fine)
pio run -e p4-s070-wba -t upload --upload-port /dev/cu.usbserial-*     # VIEWE P4 (CH340, 230400)
pio device monitor                                   # 115200, exception decoder on
PLATFORMIO_BUILD_FLAGS="-DTOUCH_DIAG" pio run -t upload   # touch reset diagnostic at boot
pio test -e native                                   # host tests (test/); a plain `pio test` runs none, by design
sim/shoot.sh [OUT_DIR] [SCREEN,...]                  # simulator PNGs at 800x480 and 1024x600 (default sim/out/)
pio run -e sim && .pio/build/sim/program --window    # click through the screens (or -e sim-1024)
```
Simulator setup (sim/README.md): PlatformIO 6.2.0, a C++17 compiler, SDL2 and zlib
(`apt install build-essential libsdl2-dev zlib1g-dev`; macOS `brew install sdl2`, sdl2-compat works too).
It shows the showcase data only; warmth, backlight, touch on glass and frame rate need a board.

From an agent shell (no TTY), read the serial log with pyserial from PlatformIO's Python:
CrowPanel: to **watch a running panel**, open with `dtr=False, rts=False` — both deasserted, the CH340
leaves EN and IO0 alone and nothing restarts; commands typed on it answer normally. Only to capture the
**boot** log do you reset it: `rts=True` → `False` (`dtr=False`), then read for a few seconds. Waveshare P4
(USB-Serial/JTAG): open with **`dtr=True, rts=False`** — other states reset it on open, and a logger
that reopens after each reset loops forever. VIEWE (CH340 on UART0): opening the port resets it even
with DTR and RTS deasserted (docs/HARDWARE.md), so leave its port alone during a pending-verify OTA window.
🪤 `pio run -e <other env>` can wipe the whole `.pio/build/` (`project.checksum` is rewritten), so an
image built a minute earlier is simply gone — and `curl -F firmware=@…` then uploads 0 bytes and says
nothing useful. Copy the `.bin` somewhere else before building another environment.
🪤 Building `crowpanel7` after `waveshare-p4-7b` redoes the CrowPanel's framework rebuild (~2 min) and
that first run can fail on a missing `.sconsign312.tmp`: run it again (release.sh retries).
Changing `PLATFORMIO_BUILD_FLAGS` rebuilds the whole framework (~3 min).
`custom_sdkconfig` (64-byte cache lines) makes pioarduino rebuild the IDF libraries into the
**global** `~/.platformio/packages/framework-arduinoespressif32-libs`. 🪤 With pioarduino 55.03.312-1 its
second stage runs PlatformIO 6.2.0.post1 from `~/.platformio/penv`, which the platform rejects
("needs >=6.2.0") and **uninstalls the platform**. Fix: reinstall it
(`pio pkg install -e crowpanel7 -p <platform URL from platformio.ini>`), then `pio run` again —
the rebuilt libraries are kept and the build goes through.
Serial console (115200, type `help`): `status`, `portal`, `wifi <ssid> [password]`, `companion <url>|auto`,
`source companion|direct` (restarts), `server <host:port>|auto` (restarts), `lyrics auto|<url>|none`, `lyrics-token <t>|none`,
`forget-pairing` (restarts), `weather <town>|none`, `news <rss url>|none`, `zone <id>|auto`, `token`, `lead <ms>`,
`lib search <text>|browse|find artist|album [name]|open <n>|back|more|actions <n>|show|ui [close]` (`lib action <n>` plays — not while someone listens), `backdrop on|off`, `scroll on|off`, `sleep <min>|off`, `view lyrics|cover|clock`, `ota check|install`, `settings [close|qr]`, `tz <POSIX>|default`, `warmth <0-100>`, `colours`, `backlight <0-100>`, `forget-wifi`, `erase-all`, `bringup` (colour bars + touch tracker), `restart`,
`shots on` then `GET http://<panel>/screenshot` returns the framebuffer over Wi-Fi in ~4 s: 8-byte
`TFTS` header (magic, uint16 w, uint16 h) then raw RGB565, and it reads `g_fb` itself — the one way to
tell "drawn wrong" from "displayed wrong". Over serial instead: `screenshot` →
`tools/screenshot.py <log> out.png` (minutes at 115200). Both show real lyrics: keep them out of the repo;
`demo` freezes the screen on invented lyrics — use it for layout checks and doc screenshots
(the real cover thumbnail and backdrop stay on screen: cover art is copyrighted too);
`demo pos <ms>|manual on|off|slide <%>|off`, `show <screen>|none` and `shots on|off` (`GET /screenshot`
over Wi-Fi) drive it.
`touchdiag` replays the touch reset variants live and attaches the touch if the GT911 answers.
`backdrop off` before `screenshot` makes it fast (a dithered backdrop does not compress: ~3 min at 115200).
Docs/promo media: `tools/showcase.py --port … --host <panel IP>` (console `demo` showcase: invented
song, lyrics, zones, generated cover; frames over Wi-Fi with `shots on`, ~4 s each) → `docs/images/`;
`uv run --with pillow==11.0.0 tools/promo.py` (device frame, hero/square/banner/grid) and
`tools/animate.sh` (MP4 + GIF) → `docs/media/`. Only showcase pictures may be committed — one
deliberate exception: `docs/media/in-use.jpg` and its 1280×640 crop `social.jpg` (the repo's social
preview) are a real photo of the panel at home, kept because the lyrics and cover on its screen are
blurred until unreadable. Keep them; any new real photo needs the same blur before it is committed.
Website (GitHub Pages, gh-pages): `tools/site.sh` republishes landing + guide (+ installer, firmware kept);
`tools/release.sh` calls it with the new firmware.
Fonts: `tools/fonts/build_fonts.sh` rebuilds `assets/fonts/` (never `npx` a converter: run-time tiny_ttf).

## Rules

- Companion API: always send `zone_id`; **never** `POST /api/roon/active-zone` (global).
- Never put Wi-Fi credentials, tokens or a companion address in the repo — NVS only (setup portal or console).
- Never "Roon" in the product name. Never commit screenshots showing real lyrics (copyright).
- Pin versions (platform, libraries). Keep `src/board/` free of app logic.
- English for code, comments, docs and commits. No assistant attribution in commits.
- After a hardware finding, update the bring-up log / pitfalls in `docs/HARDWARE.md`.
