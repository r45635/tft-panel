#!/usr/bin/env bash
# Rebuilds the embedded fonts in assets/fonts/ from Noto Sans (SIL Open Font License 1.1).
#
# LVGL's built-in Montserrat is ASCII only; lyrics need accents, Latin Extended-A
# (Polish, Czech, Turkish, œ…) and typographic punctuation (’ “ ” – — …).
# The firmware renders these TTFs at run time with LVGL's tiny_ttf, at any size.
#
# Requirements: gh (GitHub CLI), uv (runs a pinned fonttools).
set -euo pipefail

GOOGLE_FONTS_SHA=8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5   # google/fonts, ofl/notosans
FONTTOOLS=fonttools==4.55.0

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/assets/fonts"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

echo "Downloading Noto Sans (google/fonts@${GOOGLE_FONTS_SHA:0:7})"
for f in 'NotoSans[wdth,wght].ttf' OFL.txt; do
    gh api "repos/google/fonts/contents/ofl/notosans/$f?ref=$GOOGLE_FONTS_SHA" \
        -H 'Accept: application/vnd.github.raw' > "$WORK/$f"
done
test -s "$WORK/NotoSans[wdth,wght].ttf" || { echo "font download failed" >&2; exit 1; }

# Basic Latin, Latin-1 Supplement, Latin Extended-A, dashes, quotes, bullet, ellipsis, euro.
UNICODES='U+0020-007E,U+00A0-017F,U+2013-2014,U+2018-2019,U+201C-201D,U+2022,U+2026,U+20AC'

mkdir -p "$OUT"
for w in 400 600; do
    echo "  NotoSans-$w.ttf"
    uvx --quiet --from "$FONTTOOLS" fonttools varLib.instancer \
        "$WORK/NotoSans[wdth,wght].ttf" wght=$w wdth=100 -o "$WORK/inst-$w.ttf"
    # stb_truetype reads glyf/cmap/hmtx (+ legacy kern); drop hinting and layout tables.
    uvx --quiet --from "$FONTTOOLS" pyftsubset "$WORK/inst-$w.ttf" \
        --unicodes="$UNICODES" --no-hinting --layout-features='' --drop-tables+=GSUB,GPOS,GDEF \
        --name-IDs='*' --output-file="$OUT/NotoSans-$w.ttf"
done
cp "$WORK/OFL.txt" "$OUT/OFL.txt"
ls -l "$OUT"
