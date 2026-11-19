# Roadmap

## Phase 0 — hardware bring-up (done)

- [x] Toolchain: PlatformIO + pioarduino 55.03.312-1, LVGL 9.6.0, builds and flashes (460800 baud)
- [x] PSRAM 8 MB / flash 4 MB detected, RGB panel + bounce buffers created, LVGL running
- [x] PCA9557 found at 0x18, touch reset sequence applied
- [x] GT911 touch: answered once the touch flex was seated (id 911, fw 0x1060, 0x5D)
- [x] Visual check: colour bars in R/G/B order, gradient smooth, no drift or flicker
- [x] Touch mapping (dot follows the finger, axes not mirrored), backlight slider (PWM 5 kHz)

## Phase 1 — connected client (done)

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
- [x] Vincent's eye check on the real LCD (white balance picked on the colour test card)
- [x] Flash budget: the CrowPanel image was 1,889,435 B of its 2,031,616 B OTA slot on 2026-11-14
      (93.0 %); with direct mode it became 95.3 %, and at **v0.1.19 it is 1,982,880 B — 97.6 %,
      47 KiB left**, which is now the binding constraint of this repo. The P4 boards have 6 MB
      slots and sit near 35 % (VIEWE) and 56 % (Waveshare, which also carries the C6 firmware).

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
- [x] Release script + browser installer (ESP Web Tools on GitHub Pages); releases published since
      v0.1.0, **v0.1.19** on 2026-11-15 carries all three boards
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
- [x] PPA turns the image 180° (was the CPU), no permanent screenshot copy, faster warmth: lyric
      slides ~15 → ~20 fps (`docs/HARDWARE.md`, render speed)
- [ ] Hardware JPEG decoder: measured 2026-11-12, not worth it yet — a 480 px cover decodes in
      147 ms of ~750 ms (HTTP ~170, resampling big cover + thumbnail + full-screen backdrop ~430);
      library thumbnails are 70 px, bound by the companion's HTTP answer
- [ ] Bigger fonts on the P4 if Vincent wants them (the UI is scaled 1.25× from the 800×480 grid)

## Wi-Fi setup (2026-11-11)

- [x] Setup page lists the networks the panel hears (tap to fill the name, signal, lock)
- [x] On-screen Wi-Fi setup: network list + password on a touch keyboard, from the setup screen and Settings
- [x] Fixed: the portal could not bind port 80 when opened with Wi-Fi up (the update page held it)
- [ ] Vincent's finger test of the on-screen flow (pick, type, connect)

## Music library (Waveshare P4 2026-11-11, CrowPanel 2026-11-13)

- [x] Search and browse Roon's library from the panel, play / queue / radio on the followed zone
      (own Roon browse session; `docs/ARCHITECTURE.md`)
- [x] Vincent's finger test on the P4 (2026-11-12): search, results and playback work
- [x] Text fields show a visible blinking green cursor while they wait for typing (v0.1.15; the
      theme's thin cursor did not blink when the field was focused by the code)
- [x] CrowPanel too (v0.1.17, +29 KB of flash, 91 %): library text and lists moved to PSRAM so its
      internal RAM stays flat while browsing (300 albums + thumbnails: 61 KB free throughout)
- [x] Cover view: the artist and album lines open that artist or that album in the library
      (v0.1.18; search + exact match, the results shown when the name is not one thing)
- [ ] Vincent's finger test on the CrowPanel, including the two new tappable lines

## VIEWE ESP32-P4 7" (third board, 2026-11-14)

- [x] UEP4S070H1024V600C-WB-C (JD9165, GT911, C6 Wi-Fi, CH340 serial, 16 MB flash) running the
      firmware: image, touch, Wi-Fi, portal, library, OTA
- [x] The P4 display layer is shared (`src/board/board_p4_mipi.cpp`) and the board is one pin header
      (`src/board/pins_p4_s070_wba.h`), every value taken from the vendor's own BSP
- [x] Root cause of a day of dark screen written down: two GPIO-switched panel supply rails nobody
      drove, reset on GPIO 22 (`docs/HARDWARE.md`); three earlier "pitfalls" were its symptoms
- [ ] Vincent's eye check (colours, warmth, orientation, touch comfort)
- [x] Published since v0.1.19: `tools/release.sh` builds and ships its three files, so a VIEWE
      updates over the air and from its own page like the others
- [x] Web installer carries it too (2026-11-15): the page **asks which screen is connected** and
      hands it that board's own `manifest-<env>.json`, instead of ESP Web Tools picking by chip —
      which cannot work once two boards share one, and would have left a screen dark
- [x] The published v0.1.19 notes say so too (corrected 2026-11-17): they had told people a VIEWE
      must be flashed from a computer first "because the browser installer chooses by chip", true
      when they were written and wrong a few hours later — and that paragraph is what the
      r/roonlabs post of 2026-11-15 sends people to

## Direct to the music server (2026-11-14)

- [x] Protocol library `r45635/tft-core-client` (discovery, framing, WebSocket, registry, zones,
      browse) with 159 host checks, four bugs caught before any hardware ran
- [x] `Source` seam in the firmware: the companion and the direct source behind one interface, the
      network task source-independent (`docs/ARCHITECTURE.md`)
- [x] Direct source on the VIEWE P4: pairing kept in NVS, zones and now playing pushed about once a
      second, covers over plain HTTP, library browse, volume and transport, healthy mark for OTA
- [x] Position clock told apart a polled reading from a pushed one (uncertainty 821 ms → 7 ms)
- [x] No lyrics come from the music server itself (checked, 2026-11-14), so lyrics are their own
      question: a companion found on the network, an LRCLIB-compatible server, or none
- [x] Flash cost on the ESP32-S3 measured: +46.4 KiB, image at 95.3 % of the slot — one image can
      carry both modes
- [x] The CrowPanel runs it too (2026-11-15): paired, zones, now playing, covers, library and the
      update check, with 51 KB of internal RAM free (43 KB low water) and no movement while 200
      albums and their thumbnails load — the list lives in PSRAM. Both thresholds met, so
      `-DSOURCE_ROON` is in the `crowpanel7` env as well and one image carries both sources
- [x] The library is public (MIT, v0.1.0) and **all three envs** carry the direct source
      (2026-11-15): every board can reach the music server by itself, and the companion is the
      option it is — the lyrics one. Direct is where this is going
- [ ] Vincent's run in direct mode over a few days
- [x] Lyrics in direct mode answered without a second Roon extension (2026-11-15): the companion
      gained `ROON_ENABLED=false` — a lyrics server that never pairs with Roon — and LRCLIB's own
      `GET /api/get`, so a panel given an address reaches it too (Docker Desktop has no host
      networking, so its announcement never leaves the container). Verified: a CrowPanel in direct
      mode took 41 synced lines from a companion paired with nothing
- [x] Fixed the intermittent "no lyrics": a Mac announces every interface, and the panel took
      whichever IPv4 came first — a self-assigned `169.254.x.x` half the time. Both discovery paths
      now walk the whole list and refuse link-local (`docs/ARCHITECTURE.md`)

## The clock face, and what has to be typed (2026-11-14)

- [x] The clock is a third swipe view as well as the idle screen, with the weather and a news
      ticker (hand-animated: LVGL caps a scrolling label at 10 s whatever the length)
- [x] The weather town is disambiguated (country, region or county after the name, most populated
      match) and says what it settled on or why it failed
- [x] One page per panel at `http://tft-panel-xxxx.local` for every typed setting, announced over
      mDNS with the panel's name, board, MAC and version
- [x] Code once, then a month-long cookie that the page, the Wi-Fi reset and `erase-all` revoke;
      refusals keep what was typed and say what was wrong; a save names what moved; an unsent field
      is left alone; a firmware upload still asks for a fresh code
- [x] Vincent's eye on the page from a phone (2026-11-15). Two things came out of it: the address
      QR covered the code the page then asks for, so it closes itself when the code appears
      (console `settings qr` replays it); and a feed that gives no headlines now says why on the
      page, as the weather already did
- [x] News checked end to end (2026-11-15): BBC and CBC, 1.7 s a feed, 8 headlines, the ticker
      readable. 🪤 cbc.ca never answered until the panel stopped calling itself `ESP32HTTPClient`
      (`docs/ARCHITECTURE.md`)
- [ ] Vincent's finger test of the clock view

## A remote, not a lyrics screen (positioning, 2026-11-15)

The direct mode made the panel usable with nothing else running, which changes what it is: a remote
nobody has to unlock, that happens to show lyrics — a bigger audience than lyrics alone. One repo,
one firmware, two modes; the README, the site landing and the guide's opening were rewritten to lead
with that, and lyrics became a section. What is still missing to be a credible remote:

- [ ] The queue: what is next, reorder, clear (`subscribe_queue`; the companion already exposes it,
      `tft-core-client` does not)
- [ ] Zone grouping / ungrouping (the protocol has it, neither client uses it yet)
- [x] Scrub on the progress bar: written in PR #2 (batch 2) and tried by finger on the VIEWE
      2026-11-17 — tap and drag both behave; it reaches main with that batch
- [ ] Output standby and convenience switch
- [ ] First run should not ask "companion or direct": the panel can look for both and say what it
      found, so a newcomer never has to understand the project's history to hear music

🪤 None of this fits the CrowPanel: it is at **97.6 % of its OTA slot, 47 KiB left**. These are P4
screens; the S3 stays the panel it already is — and the next thing added to it has to pay for
itself in bytes.

## Updates you can watch and trust, and a panel that looks alive (2026-11-15)

Found by using the thing: the Waveshare was stuck on a release older than direct mode, and nothing
would install.

- [x] **An image on trial proves itself** (90 s of Wi-Fi) instead of waiting for a server to answer.
      Until then a panel whose companion had gone away installed every update and rolled it back
      three minutes later, for ever — reporting success each time. Two silent rollbacks before
      anyone noticed
- [x] **A firmware sent from the panel's page shows on the panel**: the same full-screen picture as
      a download, counting up, the code leaving the screen as the first byte arrives. It drew
      nothing at all before — a frozen-looking panel at the one moment nobody must unplug it
- [x] **Every refusal takes that picture away again** and says why, in a sentence, while the browser
      keeps the hex. A bad checksum used to leave the panel on "Updating…" until it was unplugged
- [x] **Five bars of the mark dance while music plays**: two panels side by side, both playing, both
      looked idle — the only sign was the word "Playing" in 16 px grey in a corner
- [x] One task owns mDNS: the duplicate registration that failed at every boot is gone
- [x] The panel says where it comes from — its About row and both its pages link the project, and
      the upload page names the file this board takes and where the digests live
- [x] **v0.1.19 published**, three boards, and installed over the air from the public release on a
      panel to prove the whole path
- [ ] The update screen comes out turquoise on the Waveshare during an upload. The framebuffer
      reads black, so it is below the app; four explanations were disproved by experiment
      (`docs/HARDWARE.md`). **Kept**: it shows plainly that something is happening

## First outside contribution: PR #2 (2026-11-16)

Seek, a first run that picks its own source, French, queue + zone grouping, ECDSA-signed updates, a
GDMA render A/B, host tests and an SDL simulator of the real `src/app` screens — 4,669 lines in one
pull request, to be taken as **seven batches** instead: tooling and tests first, signed updates
last, each merged once its checklist has run on a board. The order and the reasons are the review
comment on the PR.

What a board still has to say, and nothing else is blocking:

- [x] Seek by finger on the VIEWE (2026-11-17): tap and drag on the progress bar both behave
- [x] Seek against the offset popup (2026-11-18, VIEWE): the drag seeks, the popup does not steal
      it. 🪤 The same drag on the CrowPanel and the Waveshare swipes between views instead — not a
      regression and not a conflict: seek lives in batch 2, so `main` has no `makeSeekable()` at
      all and those two boards were running firmware without the feature. Comparing a gesture
      across boards means putting the same build on them first
- [ ] Tap a queue row to play it
- [ ] Group and ungroup two real zones
- [ ] A signed install end to end — before any private key is compiled into a release, because a
      panel that ships with one stops accepting unsigned updates for good
- [ ] The render A/B on the CrowPanel: `flush cpu|dma|dma2`, watching for tearing and for a torn
      strip after a backdrop change; `cpu` stays the default until a measurement says otherwise

Two design questions from the review, both decided on 2026-11-18:

- **Batch 3, the first run: the grace delay becomes symmetric.** `choose()` let a music server win
  immediately while a companion waited `GRACE_MS`, which is backwards — direct mode cannot play
  anything until someone enables the extension in Roon, so the panel was picking the source that
  needs a human and making the user wait on the one that would have played. Both sources now wait
  the same grace. Preferring the companion and moving to direct once the pairing is approved would
  be better still; the symmetric delay is the cheap half of it, and at least it is not backwards.
- **Batch 5, queue and grouping: through `tft-core-client` first.** Both features go through the
  companion only, so they would land dark in direct mode — which is what a new panel chooses and
  what both of the maintainer's panels run. `subscribe_queue` and grouping go into the library, and
  then both modes gain them. The detour is accepted rather than merging a companion-only version.

### Batch 1, tooling and tests: PR #3 (2026-11-18)

The first batch arrived as its own pull request: host tests, an SDL simulator of the real
`src/app` screens, the VIEWE backup read in pieces, and `docs/HARDWARE_TEST.md` — the checklist a
maintainer runs on each board. Both macOS fixes the review asked for are in it (the signature
suite in an environment of its own; `-std=gnu++17` on the simulator), and CI builds all three
boards.

Re-checked here, not taken on trust: 11 host suites green on this Mac, the simulator renders all
ten screens at both sizes, and `settings.png` comes out pixel-identical to the committed
showcase picture.

- [x] `docs/HARDWARE_TEST.md` run end to end on the Waveshare (2026-11-18): backup, flash,
      Internet, companion, direct, update and rollback all pass; results on the PR
- [ ] The four steps a console cannot do: colour bars, touch at the four corners, lyrics timing
      on a real song, render speed from the three `[ui] scroll:` lines
- [ ] `docs/images/lyrics.png` is stale: the simulator's own render differs from it in 530
      pixels, all inside the status corner (x 722-775, y 11-33), i.e. the picture predates a
      change to that corner. The renderer is right and the picture is old — regenerate it with
      `tools/showcase.py` at the next release, and the comparison becomes a real regression test
- [x] The OTA self-proof message printed once instead of on every loop (2026-11-18): under the
      rollback test build it filled the log with 8,376 identical lines in the three minutes that
      matter, which is exactly when someone is reading it

## Other boards (not started)

- **Guition JC1060P470C** (ESP32-P4, JD9165, 16 MB flash) — asked by a user 2026-11-11; about half a day
  of code, but blocked until someone can test on the board. Details: `docs/HARDWARE.md#other-boards`.
- Install page: the board choice exists since 2026-11-15, so a new board is one more entry and one
  more manifest — no longer blocked on the chip being unique.

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
- Enclosure / stand
