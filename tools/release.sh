#!/usr/bin/env bash
# Publishes a TFT Panel release: tag, build, GitHub release assets, web installer.
#
#   tools/release.sh v0.1.0 "Short release notes"
#
# Assets (what the panels' OTA and the web installer expect):
#   tft-panel-<tag>.bin          app image, installed over the air
#   tft-panel-<tag>.sha256       its SHA-256 ("<hex>  <file>"), checked before the image is committed
#   tft-panel-<tag>-factory.bin  bootloader + partitions + app, flashed at 0x0 by the web installer
# The website (docs + browser installer, tools/site.sh) goes to the gh-pages branch, latest version only.
# See docs/RELEASING.md.
set -euo pipefail

tag="${1:-}"
notes="${2:-}"
[[ "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "usage: $0 vX.Y.Z \"notes\"" >&2; exit 2; }
[[ -n "$notes" ]] || { echo "release notes are required" >&2; exit 2; }

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
pio="${PIO:-$HOME/.local/bin/pio}"
repo="r45635/tft-panel"

[[ -z "$(git status --porcelain)" ]] || { echo "working tree not clean" >&2; exit 1; }
[[ "$(git rev-parse --abbrev-ref HEAD)" == "main" ]] || { echo "release from main only" >&2; exit 1; }
git fetch -q origin main
[[ "$(git rev-parse HEAD)" == "$(git rev-parse origin/main)" ]] || { echo "main is not pushed / up to date" >&2; exit 1; }
if git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then echo "tag $tag already exists" >&2; exit 1; fi

git tag -a "$tag" -m "TFT Panel $tag"
trap 'echo "failed: removing local tag $tag" >&2; git tag -d "$tag" >/dev/null' ERR

"$pio" run
version=$(grep -o '"v[^"]*"' src/generated/version.cpp | tr -d '"')
[[ "$version" == "$tag" ]] || { echo "built version $version is not $tag" >&2; false; }

out="dist/$tag"
rm -rf "$out" && mkdir -p "$out"
cp .pio/build/crowpanel7/firmware.bin "$out/tft-panel-$tag.bin"
cp .pio/build/crowpanel7/firmware.factory.bin "$out/tft-panel-$tag-factory.bin"
(cd "$out" && shasum -a 256 "tft-panel-$tag.bin" > "tft-panel-$tag.sha256")
echo "built:"; ls -l "$out"

git push -q origin "$tag"
trap - ERR
gh release create "$tag" --repo "$repo" --title "TFT Panel $tag" --notes "$notes" \
    "$out/tft-panel-$tag.bin" "$out/tft-panel-$tag.sha256" "$out/tft-panel-$tag-factory.bin"

# Website (docs + browser installer) on gh-pages: tools/site.sh with this release's firmware.
# Four parts, not the merged factory image: the merged one fills the gap over NVS (0x9000) with
# 0xFF, which would wipe the Wi-Fi and settings of a panel that is only being reinstalled.
fw="$out/site-fw"
mkdir -p "$fw"
cp .pio/build/crowpanel7/bootloader.bin "$fw/bootloader.bin"
cp .pio/build/crowpanel7/partitions.bin "$fw/partitions.bin"
cp "$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" "$fw/boot_app0.bin"
cp "$out/tft-panel-$tag.bin" "$fw/"
cat > "$fw/manifest.json" <<MANIFEST
{
  "name": "TFT Panel",
  "version": "$tag",
  "new_install_prompt_erase": true,
  "builds": [
    { "chipFamily": "ESP32-S3", "parts": [
      { "path": "bootloader.bin", "offset": 0 },
      { "path": "partitions.bin", "offset": 32768 },
      { "path": "boot_app0.bin", "offset": 57344 },
      { "path": "tft-panel-$tag.bin", "offset": 65536 }
    ] }
  ]
}
MANIFEST
tools/site.sh "$fw"
echo "released $tag: https://github.com/$repo/releases/tag/$tag"
