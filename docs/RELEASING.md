# Releasing

A release is a git tag `vX.Y.Z` on `main` with three assets on its GitHub release, plus the web
installer on GitHub Pages. Panels in the field update from the **latest** (non-pre-release)
GitHub release, so publishing one ships it to everyone: test it on a panel first.

```bash
tools/release.sh v0.2.0 "What changed, for users"
```

The script refuses a dirty tree, a branch other than `main`, an unpushed `main` or an existing
tag. It then:

1. tags, builds (`pio run`), and checks that the built version is exactly the tag;
2. writes `dist/<tag>/`:
   - `tft-panel-<tag>.bin` — app image, what the panels install over the air;
   - `tft-panel-<tag>.sha256` — `<hex>  <file>`; the panel compares it before committing the image;
   - `tft-panel-<tag>-factory.bin` — bootloader + partition table + app at 0x0 (PlatformIO's
     `firmware.factory.bin`), for the web installer;
3. pushes the tag and creates the GitHub release with these assets;
4. force-pushes an orphan `gh-pages` commit with `index.html` (from `tools/web-installer/`),
   `manifest.json` and the factory image — latest version only, so binaries never pile up in git.

GitHub Pages serves `gh-pages` at <https://r45635.github.io/tft-panel/>. ESP Web Tools needs the
image on the same origin (release assets have no CORS headers), hence the copy there. It flashes
at 115200 baud, which the board's CH340 handles (921600 fails on this board).

## What a panel does with it

- 30 s after boot and every 24 h it reads `releases/latest`. A tag greater than its own
  `FW_VERSION` (a dev build `vX.Y.Z-n-gsha` counts as `vX.Y.Z`) with both assets shows the badge.
- Install: SHA-256 of the stream must match the `.sha256` asset, else nothing is committed.
- First boot of the new image: confirmed once Wi-Fi is up and the companion answers; if not
  within 3 minutes, the bootloader rolls back to the previous image.

## Rules

- The partition table (`partitions.csv`) is final: an OTA cannot change it.
- No credentials, companion address or lyrics in a release (settings live in NVS only).
- Build on the Mac with the pinned toolchain. CI is not set up yet: the pioarduino hybrid build
  needs the platform-reinstall workaround described in `CLAUDE.md`.
