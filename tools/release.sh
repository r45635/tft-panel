#!/usr/bin/env bash
# Publishes a TFT Panel release: tag, build, GitHub release assets, web installer.
#
#   tools/release.sh v0.1.0 "Short release notes"
#
# Assets, per board (what the panels' OTA and the web installer expect); <base> is the board's
# BOARD_ASSET: tft-panel (CrowPanel, the first board kept the plain name), tft-panel-waveshare-p4-7b.
#   <base>-<tag>.bin          app image, installed over the air
#   <base>-<tag>.sha256       its SHA-256 ("<hex>  <file>"), checked before the image is committed
#   <base>-<tag>-factory.bin  bootloader + partitions + app, for a manual flash at the bootloader offset
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

# env  asset-base  chip-family  bootloader-offset
boards=(
    "crowpanel7 tft-panel ESP32-S3 0"
    "waveshare-p4-7b tft-panel-waveshare-p4-7b ESP32-P4 8192"
)
out="dist/$tag"
fw="$out/site-fw"   # the web installer's files (tools/site.sh)
rm -rf "$out" && mkdir -p "$fw"
assets=()
builds=""
# Each board's files are copied right after its build: a pioarduino framework rebuild for the
# other environment can wipe .pio/build. Alternating environments makes it redo the CrowPanel's
# (custom_sdkconfig), whose first pass can fail on a missing .sconsign file: one retry.
for b in "${boards[@]}"; do
    read -r env base chip boot <<<"$b"
    "$pio" run -e "$env" || "$pio" run -e "$env"
    version=$(grep -o '"v[^"]*"' src/generated/version.cpp | tr -d '"')
    [[ "$version" == "$tag" ]] || { echo "built version $version is not $tag" >&2; false; }
    cp ".pio/build/$env/firmware.bin" "$out/$base-$tag.bin"
    cp ".pio/build/$env/firmware.factory.bin" "$out/$base-$tag-factory.bin"
    (cd "$out" && shasum -a 256 "$base-$tag.bin" > "$base-$tag.sha256")
    assets+=("$out/$base-$tag.bin" "$out/$base-$tag.sha256" "$out/$base-$tag-factory.bin")
    # Web installer: four parts, not the merged factory image, which fills the gap over NVS
    # (0x9000) with 0xFF and would wipe the Wi-Fi and settings of a panel being reinstalled.
    cp ".pio/build/$env/bootloader.bin" "$fw/$env-bootloader.bin"
    cp ".pio/build/$env/partitions.bin" "$fw/$env-partitions.bin"
    cp "$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin" "$fw/$env-boot_app0.bin"
    cp "$out/$base-$tag.bin" "$fw/"
    [[ -z "$builds" ]] || builds+=","
    builds+="
    { \"chipFamily\": \"$chip\", \"parts\": [
      { \"path\": \"$env-bootloader.bin\", \"offset\": $boot },
      { \"path\": \"$env-partitions.bin\", \"offset\": 32768 },
      { \"path\": \"$env-boot_app0.bin\", \"offset\": 57344 },
      { \"path\": \"$base-$tag.bin\", \"offset\": 65536 }
    ] }"
done
# ESP Web Tools picks the build matching the chip it finds on the port.
cat > "$fw/manifest.json" <<MANIFEST
{
  "name": "TFT Panel",
  "version": "$tag",
  "new_install_prompt_erase": true,
  "builds": [$builds
  ]
}
MANIFEST
python3 -c "import json,sys; json.load(open(sys.argv[1]))" "$fw/manifest.json"
echo "built:"; ls -l "$out"

git push -q origin "$tag"
trap - ERR
gh release create "$tag" --repo "$repo" --title "TFT Panel $tag" --notes "$notes" "${assets[@]}"

# Website (docs + browser installer) on gh-pages: tools/site.sh with this release's firmware.
tools/site.sh "$fw"
echo "released $tag: https://github.com/$repo/releases/tag/$tag"
