#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Draw the Homebrew Channel icon (128x48): the title in the app's Terminus over a little patch of night sky, the
name in the rainbow palette's bands with a soft glow, and a ringed planet in the corner.

  make_icon.py OUT.png
"""
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter

from gen_font import FONTS, load_bdf

W, H = 128, 48


def text_mask(glyphs, text, scale):
    # Glyphs side by side, trimmed to their ink and blown up by whole pixels so the font stays crisp.
    cw, ch = next(iter(glyphs.values())).size
    strip = Image.new('L', (cw * len(text), ch), 0)
    for i, c in enumerate(text):
        if ord(c) in glyphs:
            strip.paste(glyphs[ord(c)], (i * cw, 0))
    strip = strip.crop(strip.getbbox())
    return strip.resize((strip.width * scale, strip.height * scale), Image.NEAREST)


def stamp(im, mask, center_x, center_y, color):
    im.paste(Image.new('RGB', mask.size, color), (center_x - mask.width // 2, center_y - mask.height // 2), mask)


# The rainbow palette's stops, in the diagonal bands it paints on screen.
SPECTRUM = [(255, 59, 78), (255, 138, 31), (255, 224, 58), (76, 240, 106), (43, 214, 255), (80, 112, 255), (180, 85, 255)]


def rainbow(width, height):
    out = Image.new('RGB', (width, height))
    for y in range(height):
        for x in range(width):
            u = (x / width * 0.86 + 0.25 * y / height) % 1 * (len(SPECTRUM) - 1)
            a, b, t = SPECTRUM[int(u)], SPECTRUM[min(int(u) + 1, len(SPECTRUM) - 1)], u - int(u)
            out.putpixel((x, y), tuple(int(a[k] + (b[k] - a[k]) * t) for k in range(3)))
    return out


def glow(im, mask, x, y, color, radius):
    # A blurred copy of the lettering laid down first, so it lights the sky around it.
    halo = Image.new('L', (mask.width + radius * 4, mask.height + radius * 4), 0)
    halo.paste(mask, (radius * 2, radius * 2))
    halo = halo.filter(ImageFilter.GaussianBlur(radius)).point(lambda v: v * 0.55)
    im.paste(Image.new('RGB', halo.size, color), (x - radius * 2, y - radius * 2), halo)


def planet(draw, cx, cy, r):
    # A small ringed planet: a shaded ball with its ring passing behind it and then in front.
    draw.ellipse((cx - r - 7, cy - 2, cx + r + 7, cy + 3), outline=(255, 204, 120))
    for y in range(-r, r + 1):
        for x in range(-r, r + 1):
            if x * x + y * y <= r * r:
                lit = max(0.25, 1 - ((x + r * 0.5) ** 2 + (y + r * 0.5) ** 2) / (2.2 * r * r))
                draw.point((cx + x, cy + y), fill=(int(120 * lit + 40), int(80 * lit + 30), int(200 * lit + 40)))
    draw.arc((cx - r - 7, cy - 2, cx + r + 7, cy + 3), 0, 180, fill=(255, 224, 160))


def main():
    out = sys.argv[1]
    _, small = load_bdf(os.path.join(FONTS, 'terminus', 'ter-u12b.bdf'))
    _, large = load_bdf(os.path.join(FONTS, 'terminus', 'ter-u16b.bdf'))
    im = Image.new('RGB', (W, H))
    draw = ImageDraw.Draw(im)
    for y in range(H):
        t = y / H
        draw.line([(0, y), (W, y)], fill=(int(6 + 14 * t), int(7 + 9 * t), int(16 + 30 * t)))
    rng = random.Random(7)
    # Stars keep to the sky around the lettering: the label, the title and the thread under it.
    lettering = [(W // 2 - 32, 1, W // 2 + 32, 15), (W // 2 - 60, 16, W // 2 + 60, 40), (W // 2 - 40, 41, W // 2 + 40, 47),
                 (W - 26, 1, W - 1, 17)]
    placed = 0
    while placed < 22:
        x, y = rng.randrange(W), rng.randrange(H)
        if any(x0 <= x <= x1 and y0 <= y <= y1 for x0, y0, x1, y1 in lettering):
            continue
        level = rng.randrange(70, 220)
        stamp(im, text_mask(small, rng.choice('..+*'), 1), x, y, (level, level, min(255, level + 30)))
        placed += 1
    planet(draw, W - 12, 9, 5)
    stamp(im, text_mask(small, 'A S C I I', 1), W // 2, 8, (255, 204, 120))
    title = text_mask(large, 'ODYSSEY', 2)
    x, y = W // 2 - title.width // 2, 28 - title.height // 2
    glow(im, title, x, y, (150, 110, 255), 3)
    im.paste(rainbow(title.width, title.height), (x, y), title)
    stamp(im, text_mask(small, '~~~~~~~~~~~~', 1), W // 2, 44, (255, 106, 138))
    im.save(out)


if __name__ == '__main__':
    main()
