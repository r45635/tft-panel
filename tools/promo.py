#!/usr/bin/env python3
"""Promotional pictures from showcase screenshots (tools/showcase.py): the 800x480 screens set
in a device frame on a dark background, with the project's Noto Sans for the text.

    uv run --with pillow==11.0.0 tools/promo.py --screens docs/images --out docs/media
    uv run --with pillow==11.0.0 tools/promo.py ... --frames out/frames   # + framed animation frames

Writes hero.png (1600x900), square.png (1080x1080), banner.png (1500x500), grid.png (README),
and, with --frames, framed/fNNN.png for tools/animate.sh.
"""
import argparse
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_BOLD = os.path.join(ROOT, "assets/fonts/NotoSans-600.ttf")
FONT_REG = os.path.join(ROOT, "assets/fonts/NotoSans-400.ttf")
TAGLINE = "Synced lyrics and touch controls for your music, on a 7-inch screen."
SUB = "Open firmware for 7\" ESP32 touch screens. Works with your own TextFromTrack companion for Roon."


def font(path, size):
    return ImageFont.truetype(path, size)


def background(w, h):
    """Dark violet-to-black gradient with a warm glow, like the showcase cover."""
    bg = Image.new("RGB", (w, h))
    px = bg.load()
    for y in range(h):
        t = y / max(1, h - 1)
        c = (int(22 + 10 * (1 - t)), int(16 + 4 * (1 - t)), int(34 + 20 * (1 - t)))
        for x in range(w):
            px[x, y] = c
    glow = Image.new("RGB", (w, h), (0, 0, 0))
    d = ImageDraw.Draw(glow)
    r = int(min(w, h) * 0.45)
    d.ellipse((w * 0.62 - r, h * 0.55 - r, w * 0.62 + r, h * 0.55 + r), fill=(120, 50, 40))
    glow = glow.filter(ImageFilter.GaussianBlur(r * 0.6))
    return ImageChops.add(bg, glow)


def device(screen, scale=1.0):
    """The panel: black bezel with rounded corners, a thin rim, a soft shadow (RGBA)."""
    sw, sh = int(screen.width * scale), int(screen.height * scale)
    bez = int(34 * scale)
    w, h = sw + 2 * bez, sh + 2 * bez
    pad = int(60 * scale)
    out = Image.new("RGBA", (w + 2 * pad, h + 2 * pad), (0, 0, 0, 0))
    shadow = Image.new("RGBA", out.size, (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle((pad, pad + int(18 * scale), pad + w, pad + h + int(18 * scale)),
                                             radius=int(30 * scale), fill=(0, 0, 0, 170))
    out = Image.alpha_composite(out, shadow.filter(ImageFilter.GaussianBlur(int(28 * scale))))
    body = Image.new("RGBA", out.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(body)
    d.rounded_rectangle((pad, pad, pad + w, pad + h), radius=int(30 * scale), fill=(14, 14, 16, 255),
                        outline=(58, 58, 64, 255), width=max(1, int(2 * scale)))
    out = Image.alpha_composite(out, body)
    out.paste(screen.resize((sw, sh), Image.LANCZOS), (pad + bez, pad + bez))
    return out


def paste(canvas, img, x, y):
    canvas.paste(img, (int(x), int(y)), img)


def text_block(canvas, x, y, width, title, tagline, sub, scale=1.0):
    d = ImageDraw.Draw(canvas)
    d.text((x, y), title, font=font(FONT_BOLD, int(72 * scale)), fill=(245, 242, 238))
    y += int(100 * scale)
    for line in wrap(d, tagline, font(FONT_REG, int(34 * scale)), width):
        d.text((x, y), line, font=font(FONT_REG, int(34 * scale)), fill=(236, 214, 190))
        y += int(46 * scale)
    y += int(18 * scale)
    for line in wrap(d, sub, font(FONT_REG, int(24 * scale)), width):
        d.text((x, y), line, font=font(FONT_REG, int(24 * scale)), fill=(170, 166, 176))
        y += int(34 * scale)


def wrap(d, text, f, width):
    words, lines, cur = text.split(), [], ""
    for w in words:
        t = (cur + " " + w).strip()
        if d.textlength(t, font=f) > width and cur:
            lines.append(cur)
            cur = w
        else:
            cur = t
    if cur:
        lines.append(cur)
    return lines


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--screens", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--frames")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    s = lambda n: Image.open(os.path.join(a.screens, n + ".png")).convert("RGB")  # noqa: E731

    hero = background(1600, 900)
    paste(hero, device(s("lyrics"), 0.95), 10, 150)
    text_block(hero, 960, 270, 580, "TFT Panel", TAGLINE, SUB)
    hero.save(os.path.join(a.out, "hero.png"))

    sq = background(1080, 1080)
    d = ImageDraw.Draw(sq)
    d.text((540, 110), "TFT Panel", font=font(FONT_BOLD, 76), fill=(245, 242, 238), anchor="mm")
    for i, line in enumerate(wrap(d, TAGLINE, font(FONT_REG, 34), 900)):
        d.text((540, 190 + 46 * i), line, font=font(FONT_REG, 34), fill=(236, 214, 190), anchor="mm")
    dev = device(s("cover-view"), 1.0)
    paste(sq, dev, (1080 - dev.width) / 2, 300)
    sq.save(os.path.join(a.out, "square.png"))

    ban = background(1500, 500)
    d = ImageDraw.Draw(ban)
    d.text((70, 170), "TFT Panel", font=font(FONT_BOLD, 64), fill=(245, 242, 238))
    for i, line in enumerate(wrap(d, TAGLINE, font(FONT_REG, 28), 440)):
        d.text((70, 260 + 38 * i), line, font=font(FONT_REG, 28), fill=(236, 214, 190))
    for i, n in enumerate(["lyrics", "cover-view"]):
        dev = device(s(n), 0.48)
        paste(ban, dev, 540 + i * 460, (500 - dev.height) / 2 + 10)
    ban.save(os.path.join(a.out, "banner.png"))

    grid = background(1700, 1080)
    for i, n in enumerate(["lyrics", "cover-view", "controls", "settings"]):
        dev = device(s(n), 0.9)
        paste(grid, dev, 20 + (i % 2) * 830, 10 + (i // 2) * 520)
    grid.save(os.path.join(a.out, "grid.png"))

    if a.frames:
        fo = os.path.join(a.out, "framed")
        os.makedirs(fo, exist_ok=True)
        bg = background(1000, 680)
        for n in sorted(os.listdir(a.frames)):
            if n.endswith(".png"):
                c = bg.copy()
                paste(c, device(Image.open(os.path.join(a.frames, n)).convert("RGB"), 1.0), 4, 8)
                c.save(os.path.join(fo, n))
    print("written to", a.out)


if __name__ == "__main__":
    main()
