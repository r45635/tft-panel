#!/usr/bin/env python3
"""TFT Panel logo: a screen with lyric lines (one bright, two dim) and a warm sunset glow.

    uv run --with pillow==11.0.0 tools/brand.py      # writes assets/brand/

Writes icon.svg (vector), icon-{1024,512,180,32}.png, wordmark-on-dark.png and
wordmark-on-light.png (icon + "TFT Panel" in the project's Noto Sans 600).
"""
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets/brand")
FONT = os.path.join(ROOT, "assets/fonts/NotoSans-600.ttf")

# Geometry on a 100 x 100 grid (shared by the SVG and the PNG renders).
BODY = (6, 16, 94, 84, 12)          # x0, y0, x1, y1, radius: the panel
SCREEN = (13, 23, 87, 77, 6)
LINES = [  # x0, x1, y, thickness, colour
    (26, 74, 36, 5, "#8a7f96"),
    (20, 80, 50, 8, "#fff4e6"),
    (30, 70, 64, 5, "#8a7f96"),
]
SVG = """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100" width="512" height="512">
  <defs>
    <linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#2a1640"/><stop offset="1" stop-color="#7a2e3a"/>
    </linearGradient>
  </defs>
  <rect x="6" y="16" width="88" height="68" rx="12" fill="#0e0e10" stroke="#3a3a40" stroke-width="1.2"/>
  <rect x="13" y="23" width="74" height="54" rx="6" fill="url(#sky)"/>
  <circle cx="50" cy="77" r="22" fill="#f2864a" opacity="0.35"/>
{lines}
</svg>
"""


def svg():
    lines = "\n".join(
        f'  <line x1="{x0}" y1="{y}" x2="{x1}" y2="{y}" stroke="{c}" stroke-width="{t}" stroke-linecap="round"/>'
        for x0, x1, y, t, c in LINES)
    return SVG.format(lines=lines)


def hexrgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


def icon(size):
    s = 4 * size / 100   # supersampled
    big = Image.new("RGBA", (4 * size, 4 * size), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    x0, y0, x1, y1, r = BODY
    d.rounded_rectangle((x0 * s, y0 * s, x1 * s, y1 * s), radius=r * s, fill=(14, 14, 16, 255),
                        outline=(58, 58, 64, 255), width=max(1, int(1.2 * s)))
    x0, y0, x1, y1, r = SCREEN
    sky = Image.new("RGBA", big.size)
    sd = ImageDraw.Draw(sky)
    for y in range(int(y0 * s), int(y1 * s)):
        t = (y - y0 * s) / ((y1 - y0) * s)
        c = tuple(int(a + (b - a) * t) for a, b in zip((42, 22, 64), (122, 46, 58)))
        sd.line((x0 * s, y, x1 * s, y), fill=c + (255,))
    glow = Image.new("RGBA", big.size, (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse(((50 - 22) * s, (77 - 22) * s, (50 + 22) * s, (77 + 22) * s),
                                 fill=(242, 134, 74, 90))
    sky = Image.alpha_composite(sky, glow)
    mask = Image.new("L", big.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((x0 * s, y0 * s, x1 * s, y1 * s), radius=r * s, fill=255)
    big.paste(sky, (0, 0), mask)
    for lx0, lx1, ly, t, c in LINES:
        rgb = hexrgb(c)
        d.line((lx0 * s, ly * s, lx1 * s, ly * s), fill=rgb + (255,), width=int(t * s))
        for x in (lx0, lx1):   # round caps
            d.ellipse(((x - t / 2) * s, (ly - t / 2) * s, (x + t / 2) * s, (ly + t / 2) * s), fill=rgb + (255,))
    return big.resize((size, size), Image.LANCZOS)


def wordmark(text_colour, bg):
    h = 200
    ic = icon(h)
    f = ImageFont.truetype(FONT, 104)
    tw = int(ImageDraw.Draw(Image.new("RGB", (1, 1))).textlength("TFT Panel", font=f))
    img = Image.new("RGBA", (h + 30 + tw + 40, h), bg)
    img.paste(ic, (0, 0), ic)
    ImageDraw.Draw(img).text((h + 30, h / 2), "TFT Panel", font=f, fill=text_colour, anchor="lm")
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    open(os.path.join(OUT, "icon.svg"), "w").write(svg())
    for n in (1024, 512, 180, 32):
        icon(n).save(os.path.join(OUT, f"icon-{n}.png"))
    wordmark((245, 242, 238, 255), (0, 0, 0, 0)).save(os.path.join(OUT, "wordmark-on-dark.png"))
    wordmark((24, 20, 30, 255), (0, 0, 0, 0)).save(os.path.join(OUT, "wordmark-on-light.png"))
    print("written to", OUT)


if __name__ == "__main__":
    main()
