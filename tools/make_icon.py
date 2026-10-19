#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Draw the Homebrew Channel icon (128x48): the title in the app's Terminus over a little patch of night sky.

  make_icon.py OUT.png
"""
import os
import random
import sys

from PIL import Image, ImageDraw

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
    lettering = [(W // 2 - 32, 1, W // 2 + 32, 15), (W // 2 - 60, 16, W // 2 + 60, 40), (W // 2 - 40, 41, W // 2 + 40, 47)]
    placed = 0
    while placed < 22:
        x, y = rng.randrange(W), rng.randrange(H)
        if any(x0 <= x <= x1 and y0 <= y <= y1 for x0, y0, x1, y1 in lettering):
            continue
        level = rng.randrange(70, 220)
        stamp(im, text_mask(small, rng.choice('..+*'), 1), x, y, (level, level, min(255, level + 30)))
        placed += 1
    stamp(im, text_mask(small, 'A S C I I', 1), W // 2, 8, (255, 204, 120))
    stamp(im, text_mask(large, 'ODYSSEY', 2), W // 2, 28, (255, 240, 222))
    stamp(im, text_mask(small, '~~~~~~~~~~~~', 1), W // 2, 44, (255, 106, 138))
    im.save(out)


if __name__ == '__main__':
    main()
