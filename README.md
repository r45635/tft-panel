# TFT Panel

**A 7" touch screen that shows the synced lyrics of what you play in Roon — and lets you control it.**

![TFT Panel](docs/media/hero.png)

TFT Panel is open firmware for 7" ESP32 touch screens — the **Elecrow CrowPanel 7.0"** and the
**Waveshare ESP32-P4 7B** (see [Hardware](#hardware)). It is a companion screen for the
[TextFromTrack Roon Companion](https://github.com/r45635/roon.app.textfromtrack.com): the companion
watches your Roon Core and finds the lyrics; the panel displays them line by line, with the cover,
and gives you play/pause, skip, volume and zone selection at a touch.

> 🚧 **Early development** — synced lyrics, cover view, touch controls and over-the-air updates work
> on the developer's panel; expect rough edges. See [docs/ROADMAP.md](docs/ROADMAP.md).

- Talks only to **your own** companion on your LAN — no cloud, no account, no lyrics stored on the device.
- Same open HTTP API as the companion's web player.
- MIT licence ([details](#licence)).

*Roon is a trademark of Roon Labs. TFT Panel is an independent project, not affiliated with Roon Labs, Elecrow or Waveshare.*

<p align="center"><img src="docs/media/lyrics.gif" width="640" alt="Lyrics scrolling, then the cover view"></p>

*Pictures from the panel's showcase mode: the song, lyrics, zones and cover are invented.*

## Hardware

| | Elecrow CrowPanel 7.0" | Waveshare ESP32-P4 7B |
|---|---|---|
| Model | ESP32 Display 7.0", SKU **DIS08070H**, board **V3.0** | **ESP32-P4-WIFI6-Touch-LCD-7B** |
| Chip | ESP32-S3 (4 MB flash, 8 MB PSRAM) | ESP32-P4 (32 MB flash, 32 MB PSRAM) + ESP32-C6 for Wi-Fi |
| Screen | 800×480 RGB, GT911 touch | 1024×600 MIPI-DSI, GT911 touch |
| Build | `pio run -e crowpanel7` | `pio run -e waveshare-p4-7b` |

The interface is drawn on an 800×480 grid and scaled to the screen, so both look the same.
Details, pin maps and pitfalls: [docs/HARDWARE.md](docs/HARDWARE.md).

## Install

**First time — from your browser, no tools to install.** Open the
[web installer](https://r45635.github.io/tft-panel/install.html) in Chrome or Edge on a computer, connect the
panel's USB-C port (on the Waveshare, the one marked *USB 1.1*), click *Install* and pick the serial
port; the installer picks the firmware for the screen it finds. On Windows the CrowPanel may need the
[CH340 driver](https://www.wch-ic.com/downloads/CH341SER_EXE.html).

**After that — over the air.** The panel checks for new releases by itself; when one is out, *Update*
appears at the top of the screen. Tap it and confirm: the panel downloads it, checks its checksum and
restarts. If the new version does not start properly, it goes back to the previous one by itself.

## Build and flash

Requires [PlatformIO](https://platformio.org/) (VS Code extension or `pio` CLI). The toolchain is
pinned in `platformio.ini` (pioarduino = Arduino-ESP32 3.3 on ESP-IDF 5.5).

```bash
pio run -e crowpanel7 -t upload           # CrowPanel: flash over USB-C (CH340, 460800 baud)
pio run -e waveshare-p4-7b -t upload      # Waveshare: flash over USB-C "USB 1.1" (native USB)
pio device monitor                        # serial log and console, 115200
```

## First start

With no Wi-Fi configured, the panel opens its own network `TFT-Panel-XXXX` and shows two QR codes:
scan the first to join it, the second to open the setup page, then pick your Wi-Fi. The companion is
found automatically on your network (Bonjour); its address and access token can also be entered there.

## Documentation

- [User guide](docs/USER_GUIDE.md) — install, Wi-Fi setup, screens, gestures, settings, updates
- [Architecture](docs/ARCHITECTURE.md) — how the panel talks to the companion, firmware layers, technical choices
- [Hardware](docs/HARDWARE.md) — pin map, bring-up log, pitfalls
- [Roadmap](docs/ROADMAP.md)
- [Releasing](docs/RELEASING.md) — how a version is published (GitHub release + web installer)
- [Media kit](https://r45635.github.io/tft-panel/media-kit.html) — logos, pictures, video and texts for coverage

## Licence

TFT Panel is released under the [MIT licence](LICENSE) — code, documentation and media. The
firmware also contains third-party components under their own licences (Arduino core for ESP32:
LGPL-2.1; ESP-IDF: Apache-2.0; LVGL, ArduinoJson: MIT; Noto Sans: OFL 1.1): see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
