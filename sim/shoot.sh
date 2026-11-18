#!/usr/bin/env bash
# Builds the simulator at both screen sizes and writes one PNG per screen into OUT_DIR.
# usage: sim/shoot.sh [OUT_DIR] [SCREEN,...]     (default: sim/out, the five main screens)
set -euo pipefail
cd "$(dirname "$0")/.."
out=${1:-sim/out}
shots=${2:-lyrics,cover,clock,library,settings}
pio=${PIO:-pio}
mkdir -p "$out"
for env in sim sim-1024; do
    "$pio" run -s -e "$env"
    ".pio/build/$env/program" --out "$out" --shots "$shots"
done
