#!/usr/bin/env bash
# Builds and publishes the website on GitHub Pages (branch gh-pages, one orphan commit):
#   index.html    landing page (tools/site/index.html)
#   guide.html    docs/USER_GUIDE.md rendered (Python-Markdown, pinned), with docs/images + docs/media
#   install.html  the browser installer (ESP Web Tools) + manifest.json and the firmware parts
#
#   tools/site.sh             docs only: keeps the firmware currently on gh-pages
#   tools/site.sh <fw-dir>    with new firmware parts (manifest.json + *.bin), from tools/release.sh
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"
fw="${1:-}"
site="$(mktemp -d)"
trap 'rm -rf "$site"' EXIT

if [[ -n "$fw" ]]; then
    cp "$fw"/manifest.json "$fw"/*.bin "$site/"
else
    git fetch -q origin gh-pages
    git archive origin/gh-pages manifest.json $(git ls-tree --name-only origin/gh-pages | grep '\.bin$') |
        tar -x -C "$site"
fi
version=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['version'])" "$site/manifest.json")

sed "s/{{VERSION}}/$version/g" tools/site/install.html > "$site/install.html"
sed "s/{{VERSION}}/$version/g" tools/site/index.html > "$site/index.html"
cp -R docs/images docs/media "$site/"
uv run --quiet --no-project --with markdown==3.7 python tools/site/render_guide.py "$site/guide.html"
touch "$site/.nojekyll"
(
    cd "$site"
    git init -q -b gh-pages
    git add -A
    git -c user.name="$(git -C "$root" config user.name)" -c user.email="$(git -C "$root" config user.email)" \
        commit -q -m "Website: firmware $version, docs $(git -C "$root" rev-parse --short HEAD)"
    git push -q --force "$(git -C "$root" remote get-url origin)" gh-pages
)
echo "published https://r45635.github.io/tft-panel/ (firmware $version)"
