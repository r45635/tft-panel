# Simulator

The panel's real screens (`src/app`, unchanged) on a desktop, at 800 x 480 (CrowPanel) and
1024 x 600 (Waveshare 7B). It runs them in their showcase mode: the invented song, lyrics, zones and
cover of `src/app/showcase.cpp`. No network, no copyrighted lyrics, so every picture can be shared.

```bash
sim/shoot.sh                                   # both sizes, five screens, into sim/out/
sim/shoot.sh /tmp/shots lyrics,volume,zones    # another folder, other screens
```

A screen name is anything the console's `show` command takes: `lyrics`, `cover`, `clockview`,
`controls`, `settings`, `colours`, `volume`, `offset`, `zones`, `clock`, `update`, `confirm`,
`progress`, `setup`, `searching`, `library`, `library-actions`, `wifi`, `wifi-password`, `code`.
Files are named `<screen>-<width>x<height>.png`. An unknown name fails the run (exit code 1),
so does an empty list.

To click through the screens yourself, open a window. The mouse stands in for the touch screen:

```bash
pio run -e sim && .pio/build/sim/program --window          # or -e sim-1024
```

## Setup

PlatformIO Core (`pip install platformio==6.2.0`, or `uv tool install platformio==6.2.0`),
a C++17 compiler, SDL2 and zlib. `PIO=/path/to/pio` if `pio` is not on the `PATH`.

- Debian / Ubuntu: `apt install build-essential libsdl2-dev zlib1g-dev`
- macOS: `brew install sdl2`. Not checked yet: the build has only run on Linux so far.

The build asks `sdl2-config` where SDL2 is (`sim/sdl_flags.py`).

First build ~20 s (LVGL), then a few seconds. After `tools/fonts/build_fonts.sh`, delete
`.pio/build/sim*`: the build does not see that the font files changed. A run takes well under a second.

## How it works

| Where | What |
|---|---|
| `[env:sim]`, `[env:sim-1024]` in `platformio.ini` | Native builds of `src/app`, one per screen size: `src/layout.h` scales at compile time from the board's pin header. |
| `sim/lv_conf_sim.h` | The firmware's `include/lv_conf.h`, plus the C heap and the SDL driver. |
| `sim/src/main.cpp` | Display (headless buffer or SDL window), the board functions the UI calls, the clock, PNG output. |
| `sim/src/fake_services.cpp` | Settings in memory, and network, update and web page calls that do nothing. |
| `sim/src/wall_clock.c` | `time()` fixed at the showcase's date and time. |
| `sim/src/fonts_embed.c` | The Noto Sans files under the names `board_build.embed_files` gives them. |
| `sim/shim/` | The few ESP-IDF headers the UI includes. `host/shim/Arduino.h` (shared with the host tests) stands in for Arduino. |

Headless, each screen is drawn by a fresh process, so a picture never depends on the screens
drawn before it. Time is the simulator's own: each screen gets 1.5 s of 16 ms frames before it is
saved, so animations have settled, and the wall clock stands still at the showcase's Saturday
7 November, 21:47 (`sim/src/wall_clock.c`). The same build writes the same pictures whenever and
wherever it runs. In the window, frames follow real time; the wall clock stays still there too.

What it does not show: colour warmth and backlight (applied in the board code, after LVGL draws), touch
behaviour on glass, frame rate. Those need a board (`docs/HARDWARE.md`).
