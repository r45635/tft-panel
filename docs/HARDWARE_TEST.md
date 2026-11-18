# Hardware test

What a maintainer runs on each board to validate a change. Much of the firmware can be checked
without a board, but colours, touch, Wi-Fi, timing on a real song, render speed and updates can
only be proved on the screen itself. Run the core steps
on every board, then the section of each change being released, and paste the results block
(end of this page) into the pull request.

Expected numbers below come from the bring-up logs, with their date and source. They are what one
board did once, not guarantees: a different value is a finding worth reporting, not a failure by
itself.

## The three boards

| | CrowPanel 7" V3.0 | Waveshare P4 7B | VIEWE P4 7" S070 |
|---|---|---|---|
| Env | `crowpanel7` | `waveshare-p4-7b` | `p4-s070-wba` |
| Port | `/dev/cu.usbserial-*` (CH340) | `/dev/cu.usbmodem*` (native USB) | `/dev/cu.usbserial-*` (CH340) |
| Flash speed | 460800 | 921600 | 230400 |
| Screen (`res=` at boot) | 800x480 | 1024x600 | 1024x600 |
| Board name at boot | `Elecrow CrowPanel 7" V3.0` | `Waveshare ESP32-P4 7B` | `VIEWE ESP32-P4 7in` |
| Update screen | frozen picture, live bar | live | live |

The console is 115200 baud on all three (`pio device monitor -e <env>`, type `help`). Open the
Waveshare's with `--dtr 1 --rts 0`, or it restarts. Opening the VIEWE's restarts it (see step 12).

## Core steps (every board)

1. **Backup**, first time on a board only: `tools/backup-firmware.py --port <port>`. It names the
   board from its chip and flash size and verifies the copy. A board it does not name is not one
   of the three: stop there. 🪤 It reads the VIEWE's 16 MB in one go at 230400, which once died
   after 284 KB on that board (docs/HARDWARE.md, VIEWE section): if it stops early, read in 512 KB
   pieces with `esptool read-flash`, and restore at 230400, not the 460800 its `--restore` prints.
2. **Flash**: `pio run -e <env> -t upload --upload-port <port>`. The boot log shows
   `[tft-panel] boot, firmware <version>, board <board name>`, then
   `[tft-panel] psram=… touch=1 … res=<screen>`. `status` never prints `WRONG BOARD`. P4 boards
   also print `[c6] Wi-Fi chip firmware …, up to date`; on a factory Wi-Fi chip they first print
   `[c6] updating the Wi-Fi chip …` (~40 s, Waveshare, 2026-11-11, docs/HARDWARE.md).
3. **Colour bars**: console `bringup`. Bars read R, G, B, W, K from left to right, the gradient
   below them has no bands, the title is upright. `restart` leaves the screen.
4. **Touch at the four corners**, still on `bringup`: the dot sits under the finger in each corner,
   and the on-screen `touch: x=… y=…` reads near `0,0` top left and near the screen size bottom
   right. No axis mirrored.
5. **Wi-Fi to the Internet**: join from the setup screen's QR codes (or `wifi <ssid> <password>`).
   `status` shows an `ip=` and a `dns=`. `ota check` prints `[ota] latest release …, running …`,
   which needs DNS and TLS to GitHub: a LAN-only link fails here (the Waveshare's factory Wi-Fi
   chip did exactly that, docs/HARDWARE.md).
6. **Companion mode**, with the TextFromTrack companion running: `source companion` (restarts).
   `status` shows `source=companion`, the zone playing, `lyrics status=… synced=1` on a song with
   synced lyrics.
7. **Direct mode**: `source direct` (restarts), then enable *TFT Panel* in Roon → Settings →
   Extensions. `status` shows `source=direct` and the zone playing; the panel's controls act on
   that zone. Go back with `source companion`.
8. **Lyrics timing**: on a song with synced lyrics, each line lights up as the singer starts it.
   `status` prints `lead=250` unless it was changed (the default, src/net/settings.h) and a
   `clock window`: ~36 ms recorded in companion mode (CrowPanel, docs/ROADMAP.md phase 1), ~7 ms
   for the pushed position in direct mode (2026-11-14, docs/ROADMAP.md). If lines are early or
   late, find the `lead <ms>` that fixes it and report the value.
9. **Render speed**: after a restart, with `scroll on` (the default), let three lyric lines
   slide. Only those first three print a line:
   `[ui] scroll: <steps> steps in <ms> ms, <n> refreshes <ms> ms, <n> flushes <ms> ms`. A slide
   lasts 400 ms (`SCROLL_MS`, src/app/ui.cpp). Reference:
   - Waveshare: 8-9 frames in 400 ms (~20 fps), flush ~1.3 ms per 48-line strip (2026-11-12,
     docs/HARDWARE.md);
   - CrowPanel: ~88 ms per full-stage frame with the backdrop, ~70 ms without (measured before the
     64-byte cache lines, docs/HARDWARE.md), so about 4-5 steps per slide (derived, not measured);
   - VIEWE: no reference yet; the first figure becomes it.

   By eye: no flickering dots around the lyrics, no image shift.
10. **Update**: build the image under test (`pio run -e <env>`), compute
    `shasum -a 256 .pio/build/<env>/firmware.bin`, and upload it from
    `http://tft-panel-xxxx.local/update` with the 6-digit code and the sha256. Keep the console
    open from before the upload: the CH340 boards' port survives the restart; the Waveshare's
    disappears, reopen it with `--dtr 1 --rts 0`. Expect
    `[ota] first boot of <version>: rollback in 3 min unless it holds Wi-Fi for 90 s`, then
    `[ota] <version> confirmed working, rollback cancelled`. Display and touch work afterwards.
    Known, not a finding: on the Waveshare the update screen's background turns turquoise during an
    upload (left as it is on 2026-11-15, docs/HARDWARE.md).
11. **Rollback**, right after step 10: put `#define OTA_TEST_NEVER_HEALTHY 1` at the top of
    `src/net/ota.cpp` (the environment variable does not reach it, docs/RELEASING.md), build with
    a version you can tell apart, `TFT_FW_VERSION=v0.0.0-rollback pio run -e <env>`, upload it as in
    step 10, then remove the line. After 90 s the log claims `…the Wi-Fi link has held for 90 s: the
    image works`: expected, the test build ignores it. At 3 minutes: `[ota] new firmware never got
    healthy: restarting into the previous one`, and the boot line shows step 10's version again,
    without a `first boot` line.
12. **VIEWE only, console without a reset**: with the panel running, open the port with DTR and RTS
    both deasserted (`--dtr 0 --rts 0`, which spares the CrowPanel). Report whether it restarted.
    Until this is known, never open the VIEWE's port during the three minutes after an update:
    open it before the upload and keep it open.

## Changes in this release

Each change that needs the board adds its steps under its heading when it lands (docs/ROADMAP.md).

### Scrub on the progress bar

### First run finds the companion or the server

### French interface

### Signed updates

### Queue and zone grouping

### Render speed: P4 draw unit, S3 DMA flush

## Results block

Copy once per board, fill in, paste into the pull request.

```
Board: <CrowPanel | Waveshare | VIEWE>   firmware: <version from the boot line>
[ ] 1 backup        [ ] 2 flash         [ ] 3 colour bars   [ ] 4 touch corners
[ ] 5 Internet      [ ] 6 companion     [ ] 7 direct        [ ] 8 timing, lead = ___ ms
[ ] 9 render        [ ] 10 update       [ ] 11 rollback     [ ] 12 VIEWE port: reset? ___
[ui] scroll lines:
<paste the three lines>
Change sections run: <names>
Notes:
```
