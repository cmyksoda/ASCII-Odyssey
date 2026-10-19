#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Draw the Homebrew Channel icon (128x48): the title over a little patch of night sky.

  make_icon.py OUT.png
"""
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(HERE, '..', 'vendor', 'fonts')
S = 4
W, H = 128, 48


def main():
    out = sys.argv[1]
    im = Image.new('RGB', (W * S, H * S))
    draw = ImageDraw.Draw(im)
    for y in range(H * S):
        t = y / (H * S)
        draw.line([(0, y), (W * S, y)], fill=(int(6 + 14 * t), int(7 + 9 * t), int(16 + 30 * t)))
    rng = random.Random(7)
    small = ImageFont.truetype(os.path.join(FONTS, 'JetBrainsMono-Bold.otf'), 7 * S)
    for _ in range(26):
        x, y = rng.randrange(W * S), rng.randrange(H * S)
        level = rng.randrange(70, 220)
        draw.text((x, y), rng.choice('..+*'), font=small, fill=(level, level, min(255, level + 30)), anchor='mm')
    title = ImageFont.truetype(os.path.join(FONTS, 'JetBrainsMono-ExtraBold.otf'), 17 * S)
    label = ImageFont.truetype(os.path.join(FONTS, 'JetBrainsMono-Bold.otf'), 9 * S)
    draw.text((W * S // 2, 13 * S), 'A S C I I', font=label, fill=(255, 204, 120), anchor='mm')
    draw.text((W * S // 2, 31 * S), 'ODYSSEY', font=title, fill=(255, 240, 222), anchor='mm')
    draw.text((W * S // 2, 43 * S), '`-._.-\'`-._.-\'`', font=small, fill=(255, 106, 138), anchor='mm')
    im.resize((W, H), Image.LANCZOS).save(out)


if __name__ == '__main__':
    main()
