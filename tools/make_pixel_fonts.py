#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Writes ASCII Odyssey's own FIGlet faces from one hand-drawn 5x7 alphabet: pixel (dots as #) and dots (as o, like
a sign of bulbs). Only what titles use is drawn, A-Z, 0-9, space and ' - . !, and lowercase borrows the capitals."""
import sys
from pathlib import Path

GLYPHS = {
    'A': ".###. #...# #...# ##### #...# #...# #...#",
    'B': "####. #...# #...# ####. #...# #...# ####.",
    'C': ".###. #...# #.... #.... #.... #...# .###.",
    'D': "###.. #..#. #...# #...# #...# #..#. ###..",
    'E': "##### #.... #.... ####. #.... #.... #####",
    'F': "##### #.... #.... ####. #.... #.... #....",
    'G': ".###. #...# #.... #.### #...# #...# .####",
    'H': "#...# #...# #...# ##### #...# #...# #...#",
    'I': ".###. ..#.. ..#.. ..#.. ..#.. ..#.. .###.",
    'J': "..### ...#. ...#. ...#. ...#. #..#. .##..",
    'K': "#...# #..#. #.#.. ##... #.#.. #..#. #...#",
    'L': "#.... #.... #.... #.... #.... #.... #####",
    'M': "#...# ##.## #.#.# #.#.# #...# #...# #...#",
    'N': "#...# #...# ##..# #.#.# #..## #...# #...#",
    'O': ".###. #...# #...# #...# #...# #...# .###.",
    'P': "####. #...# #...# ####. #.... #.... #....",
    'Q': ".###. #...# #...# #...# #.#.# #..#. .##.#",
    'R': "####. #...# #...# ####. #.#.. #..#. #...#",
    'S': ".#### #.... #.... .###. ....# ....# ####.",
    'T': "##### ..#.. ..#.. ..#.. ..#.. ..#.. ..#..",
    'U': "#...# #...# #...# #...# #...# #...# .###.",
    'V': "#...# #...# #...# #...# #...# .#.#. ..#..",
    'W': "#...# #...# #...# #.#.# #.#.# #.#.# .#.#.",
    'X': "#...# #...# .#.#. ..#.. .#.#. #...# #...#",
    'Y': "#...# #...# .#.#. ..#.. ..#.. ..#.. ..#..",
    'Z': "##### ....# ...#. ..#.. .#... #.... #####",
    '0': ".###. #...# #..## #.#.# ##..# #...# .###.",
    '1': "..#.. .##.. ..#.. ..#.. ..#.. ..#.. .###.",
    '2': ".###. #...# ....# ...#. ..#.. .#... #####",
    '3': "##### ...#. ..#.. ...#. ....# #...# .###.",
    '4': "...#. ..##. .#.#. #..#. ##### ...#. ...#.",
    '5': "##### #.... ####. ....# ....# #...# .###.",
    '6': "..##. .#... #.... ####. #...# #...# .###.",
    '7': "##### ....# ...#. ..#.. .#... .#... .#...",
    '8': ".###. #...# #...# .###. #...# #...# .###.",
    '9': ".###. #...# #...# .#### ....# ...#. .##..",
    "'": ".# .# #. .. .. .. ..",
    '-': ".... .... .... #### .... .... ....",
    '.': ". . . . . . #",
    '!': "# # # # # . #",
}
HEIGHT = 7
FACES = {
    'pixel': ('#', 'Pixel: a 5x7 dot-matrix alphabet drawn in hashes.'),
    'dots': ('o', 'Dots: a 5x7 dot-matrix alphabet drawn in bulbs, like a lit-up sign.'),
}


def glyph_rows(ch, ink):
    if ch == ' ':
        return ['   '] * HEIGHT
    art = GLYPHS.get(ch.upper())
    if art is None:
        return [''] * HEIGHT
    # One column of air after every letter, since full-width layout sets them side by side as drawn.
    return [row.replace('#', ink).replace('.', ' ') + ' ' for row in art.split()]


def face(ink, note):
    comments = [note, 'Made for ASCII Odyssey; GPL-3.0-only like the rest of it.']
    width = max(len(r) for ch in GLYPHS for r in glyph_rows(ch, ink))
    lines = [f'flf2a$ {HEIGHT} {HEIGHT} {width + 2} -1 {len(comments)}'] + comments
    # The 95 printable characters, then the seven German ones every FIGfont carries, here left blank.
    for code in list(range(32, 127)) + [196, 214, 220, 228, 246, 252, 223]:
        rows = glyph_rows(chr(code), ink) if code < 127 else [''] * HEIGHT
        for i, row in enumerate(rows):
            lines.append(row + ('@@' if i == HEIGHT - 1 else '@'))
    return '\n'.join(lines) + '\n'


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / 'assets' / 'figlet'
    out.mkdir(parents=True, exist_ok=True)
    for name, (ink, note) in FACES.items():
        (out / f'{name}.flf').write_text(face(ink, note))
        print(out / f'{name}.flf')


main()
