#!/usr/bin/env python3
"""TFT Panel logo: the TextFromTrack mark (the same icon as the companion app
and the panel's header), plus the "TFT Panel" wordmark.

    uv run --with pillow==11.0.0 tools/brand.py      # writes assets/brand/

Writes icon.svg (vector, the companion's electron/assets/icon.svg), icon-{1024,512,180,32}.png,
wordmark-on-dark.png and wordmark-on-light.png (mark + "TFT Panel" in the project's Noto Sans 600).
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tft_icon import render  # noqa: E402  (the mark's geometry lives in one place)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets/brand")
FONT = os.path.join(ROOT, "assets/fonts/NotoSans-600.ttf")

SVG = """<svg width="512" height="512" viewBox="0 0 96 96" xmlns="http://www.w3.org/2000/svg">
  <!-- TextFromTrack mark: the same drawing as the companion app's icon -->
  <rect x="0" y="0" width="96" height="96" rx="22" fill="#15161A"/>
  <path d="M22 26 L22 70 L30 70" stroke="#F6F4EE" stroke-width="6" stroke-linecap="square" stroke-linejoin="miter" fill="none"/>
  <path d="M74 26 L74 70 L66 70 M74 26 L66 26" stroke="#F6F4EE" stroke-width="6" stroke-linecap="square" stroke-linejoin="miter" fill="none"/>
  <rect x="36" y="34" width="24" height="6" fill="#F6F4EE"/>
  <rect x="45" y="34" width="6" height="20" fill="#F6F4EE"/>
  <g fill="#6db56d">
    <rect x="36" y="58" width="3" height="6"/>
    <rect x="42" y="54" width="3" height="14"/>
    <rect x="48" y="50" width="3" height="22"/>
    <rect x="54" y="56" width="3" height="10"/>
    <rect x="60" y="60" width="3" height="3"/>
  </g>
</svg>
"""


def wordmark(text_colour):
    h = 200
    ic = render(h, ss=8)
    f = ImageFont.truetype(FONT, 104)
    tw = int(ImageDraw.Draw(Image.new("RGB", (1, 1))).textlength("TFT Panel", font=f))
    img = Image.new("RGBA", (h + 36 + tw + 40, h), (0, 0, 0, 0))
    img.paste(ic, (0, 0), ic)
    ImageDraw.Draw(img).text((h + 36, h / 2), "TFT Panel", font=f, fill=text_colour, anchor="lm")
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    open(os.path.join(OUT, "icon.svg"), "w").write(SVG)
    for n in (1024, 512, 180, 32):
        render(n, ss=max(2, 4096 // n)).save(os.path.join(OUT, f"icon-{n}.png"))
    wordmark((245, 242, 238, 255)).save(os.path.join(OUT, "wordmark-on-dark.png"))
    wordmark((24, 20, 30, 255)).save(os.path.join(OUT, "wordmark-on-light.png"))
    print("written to", OUT)


if __name__ == "__main__":
    main()
