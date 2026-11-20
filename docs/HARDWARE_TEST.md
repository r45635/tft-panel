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
console of any of them with `--dtr 1 --rts 0`: that is the one open that leaves all three running.
Every other combination resets the Waveshare, and both lines deasserted resets the two CH340 boards
too — measured 2026-11-19, correcting what this file said about the VIEWE (docs/HARDWARE.md).

## Core steps (every board)

1. **Backup**, first time on a board only: `tools/backup-firmware.py --port <port>`. It names the
   board from its chip and flash size and verifies the copy. A board it does not name is not one
   of the three: stop there. Both CH340 boards are read in 512 KB pieces, up to 3 attempts each,
   because their adapters drop bytes on a long read: a single 4 MB read of the CrowPanel died at
   ~28 % (2026-11-18), and in pieces two of the eight needed a retry. The backup restarts the board, so never run it in the
   3 minutes after an update (the update would roll back). `--restore <file>` prints the command
   at the board's own flash speed, read from the file's name.
2. **Flash**: `pio run -e <env> -t upload --upload-port <port>`. The boot log shows
   `[tft-panel] boot, firmware <version>, board <board name>`, then
   `[tft-panel] psram=… touch=1 … res=<screen>`. `status` never prints `WRONG BOARD`. P4 boards
   also print `[c6] Wi-Fi chip firmware …, up to date`; on a factory Wi-Fi chip they first print
   `[c6] updating the Wi-Fi chip …` (~40 s, Waveshare, 2026-11-11, docs/HARDWARE.md).
3. **Colour bars**: console `bringup`. Bars read R, G, B, W, K from left to right, the gradient
   below them has no bands, the title is upright. `restart` leaves the screen. 🪤 This step cannot
   be done remotely: `bringup` prints nothing on the console and draws *below* the application, so
   `GET /screenshot` keeps returning the normal interface while the bars are on the glass. Someone
   working over Wi-Fi gets no signal at all, not even that the command ran.
4. **Touch at the four corners**, still on `bringup`: the dot sits under the finger in each corner,
   and the on-screen `touch: x=… y=…` reads near `0,0` top left and near the screen size bottom
   right. No axis mirrored. 🪤 Worth knowing what this step is and is not for: a mirrored axis
   cannot survive normal use of the panel — buttons would not answer under the finger and the seek
   bar could not be dragged — so on a board already in daily use, that part is proved. What the
   step actually adds is the calibration at the edges, where a few pixels of error never show up in
   use. Don't hold a release open on it for a board that has been in service for days.
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
   🪤 `clock window` is **not a board characteristic and a single reading means nothing**. It is
   the width of `PositionClock`'s uncertainty window (`uncertaintyMs()`): each sample bounds the
   offset, successive samples intersect, so the window only narrows — and any track change, seek
   or stall empties the intersection and starts it over, printing `-1` until the first sample.
   Read at an arbitrary moment it mostly says how long the clock has been converging. On one
   CrowPanel in direct mode, two minutes apart: `-1`, `-1`, 1, 15, 30 ms, and 120 ms earlier the
   same evening. So let one track play for a while and read it repeatedly; report the settled
   value, not the first number the console gives you, and do not compare one board's reading with
   another's. (docs/ROADMAP.md quotes ~36 ms for companion mode and ~7 ms for direct; the 7 ms came
   from the tft-core-client proof of concept on a bare board, not from the panel firmware.)
   🪤 If lines are early or late, first play **several different songs**. A consistent offset across
   all of them is the panel's, and `lead <ms>` is the fix: report the value. An offset on one song
   is in its lyrics file, and `lead` is the wrong lever — it is a single global correction, so
   tuning it for one track writes that track's error into every other. The per-track offset
   (companion, `/api/lyrics/offset`) is what that case needs.
9. **Render speed**: after a restart, with `scroll on` (the default), let three lyric lines
   slide. Only those first three print a line:
   `[ui] scroll: <steps> steps in <ms> ms, <n> refreshes <ms> ms, <n> flushes <ms> ms`. A slide
   lasts 400 ms (`SCROLL_MS`, src/app/ui.cpp). Reference:
   🪤 **The backdrop decides these numbers, so say which state you measured in.** Shortly after a
   restart, before the cover and its blurred backdrop are drawn, every board reports ~21 steps with
   refreshes around 100 ms and a flush near free; once the backdrop is up the step count falls and
   the refresh cost rises by three to four times. Only the second is the state a panel spends its
   life in, so measure there: let the cover appear first.
   All three measured 2026-11-18 on a real song in direct mode, backdrop up, replacing figures that
   were derived rather than measured:
   - CrowPanel: 12-16 steps in 413-445 ms, 49-56 flushes in 11-15 ms (~0.25 ms each). The old entry
     said 4-5 steps per slide, derived from a 2026-11-12 full-frame timing and wrong by three times;
   - Waveshare: 8-9 steps in 402-441 ms, 91-103 flushes in 78-93 ms (~0.9 ms each). The 2026-11-12
     reference said ~1.3 ms per 48-line strip, so flushing got faster, not slower;
   - VIEWE: 8-9 steps in 451-453 ms, 104-107 flushes in 93 ms (~0.9 ms each) — the same as the
     Waveshare, which is what two boards sharing the P4 display path should give.

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

    Open the console before the upload and keep it open through the three minutes after. Opened
    with `--dtr 1 --rts 0` it leaves any of the three boards running (docs/HARDWARE.md) — the VIEWE
    was wrongly believed to reset on every open until 2026-11-19 — but no one has yet opened a port
    during a pending-verify window, so don't make this step the first time.

## Changes in this release

Each change that needs the board adds a heading here with its steps when it lands (docs/ROADMAP.md).

## Results block

Copy once per board, fill in, paste into the pull request.

```
Board: <CrowPanel | Waveshare | VIEWE>   firmware: <version from the boot line>
[ ] 1 backup        [ ] 2 flash         [ ] 3 colour bars   [ ] 4 touch corners
[ ] 5 Internet      [ ] 6 companion     [ ] 7 direct        [ ] 8 timing, lead = ___ ms
[ ] 9 render        [ ] 10 update       [ ] 11 rollback
[ui] scroll lines:
<paste the three lines>
Change sections run: <names>
Notes:
```
