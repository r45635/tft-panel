#!/usr/bin/env bash
# Lyric-scroll animation (MP4 + GIF) from framed showcase frames (tools/promo.py --frames).
#   tools/animate.sh <framed-dir> <out-basename>     → <out>.mp4, <out>.gif
# Frame order from tools/showcase.py: still, 11 slide steps, still, 11 slide steps, cover view.
# Slide steps last 40 ms each (the panel's real slide is ~400 ms), stills ~1.5 s.
set -euo pipefail
dir="$(cd "$1" && pwd)"; out="$2"   # absolute: the concat list lives in a temp dir
list="$(mktemp)"
i=0
for f in $(ls "$dir"/f*.png | sort); do
    case $i in
        0|12) d=1.6 ;;     # still before each line change
        11|23) d=1.2 ;;    # end of a slide, line settled
        24) d=2.8 ;;       # cover view
        *) d=0.04 ;;
    esac
    printf "file '%s'\nduration %s\n" "$f" "$d" >> "$list"
    last="$f"; i=$((i + 1))
done
printf "file '%s'\n" "$last" >> "$list"   # concat demuxer: the last duration needs a trailing entry
ffmpeg -loglevel error -y -f concat -safe 0 -i "$list" -vf "fps=25,format=yuv420p" -c:v libx264 -crf 20 \
    -movflags +faststart "$out.mp4"
ffmpeg -loglevel error -y -f concat -safe 0 -i "$list" \
    -vf "fps=25,scale=640:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128[p];[b][p]paletteuse=dither=bayer:bayer_scale=4" \
    "$out.gif"
rm -f "$list"
ls -l "$out.mp4" "$out.gif"
