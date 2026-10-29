#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Writes ASCII Odyssey's own FIGlet faces from one hand-drawn 5x7 alphabet: pixel (dots as #), dots (as o, like a
sign of bulbs), wire (traced as strokes), scanline (in rows of # and =) and neon (# in a halo of dots). Only what
titles use is drawn, A-Z, 0-9, space and ' - . !, and lowercase borrows the capitals."""
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


def inked(ink):
    # One column of air after every letter, since full-width layout sets them side by side as drawn.
    return lambda art: [row.replace('#', ink).replace('.', ' ') + ' ' for row in art]


def wire(art):
    # Each dot drawn as the stroke it belongs to: runs across as -, down as |, joints as +, slants as / and \.
    on = lambda y, x: 0 <= y < len(art) and 0 <= x < len(art[0]) and art[y][x] == '#'
    rows = []
    for y, line in enumerate(art):
        row = ''
        for x in range(len(line)):
            across, down = on(y, x - 1) or on(y, x + 1), on(y - 1, x) or on(y + 1, x)
            if not on(y, x):
                row += ' '
            elif across or down:
                row += '+' if across and down else '-' if across else '|'
            else:
                row += ('/' if on(y - 1, x + 1) or on(y + 1, x - 1)
                        else '\\' if on(y - 1, x - 1) or on(y + 1, x + 1) else 'o')
        rows.append(row + ' ')
    return rows


def scanline(art):
    return [row.replace('#', '=' if i % 2 else '#').replace('.', ' ') + ' ' for i, row in enumerate(art)]


def neon(art):
    # A halo of dots one cell wide all round, which smushes into the next letter's halo instead of doubling it.
    on = lambda y, x: 0 <= y < len(art) and 0 <= x < len(art[0]) and art[y][x] == '#'
    near = lambda y, x: any(on(y + dy, x + dx) for dy in (-1, 0, 1) for dx in (-1, 0, 1))
    return [''.join('#' if on(y, x) else '.' if near(y, x) else ' ' for x in range(-1, len(art[0]) + 1))
            for y in range(-1, len(art) + 1)]


# Each face: how it draws a letter, its height, its FIGlet layout (-1 sets letters side by side, 1 smushes equal
# characters together) and the note in its header.
FACES = {
    'pixel': (inked('#'), HEIGHT, -1, 'Pixel: a 5x7 dot-matrix alphabet drawn in hashes.'),
    'dots': (inked('o'), HEIGHT, -1, 'Dots: a 5x7 dot-matrix alphabet drawn in bulbs, like a lit-up sign.'),
    'wire': (wire, HEIGHT, -1, 'Wire: a 5x7 dot-matrix alphabet traced as strokes of - | + / and \\.'),
    'scanline': (scanline, HEIGHT, -1, 'Scanline: a 5x7 dot-matrix alphabet in rows of # and =, like a television.'),
    'neon': (neon, HEIGHT + 2, 1, 'Neon: a 5x7 dot-matrix alphabet in hashes with a halo of dots, like a lit sign.'),
}


def glyph_rows(ch, draw, height, layout):
    if ch == ' ':
        # Hardblanks, so a smushing face keeps its word gaps.
        return ['$$$' if layout >= 0 else '   '] * height
    art = GLYPHS.get(ch.upper())
    return draw(art.split()) if art else [''] * height


def face(draw, height, layout, note):
    comments = [note, 'Made for ASCII Odyssey; GPL-3.0-or-later like the rest of it.']
    width = max(len(r) for ch in GLYPHS for r in glyph_rows(ch, draw, height, layout))
    baseline = height - 1 if height > HEIGHT else height
    lines = [f'flf2a$ {height} {baseline} {width + 2} {layout} {len(comments)}'] + comments
    # The 95 printable characters, then the seven German ones every FIGfont carries, here left blank.
    for code in list(range(32, 127)) + [196, 214, 220, 228, 246, 252, 223]:
        rows = glyph_rows(chr(code), draw, height, layout) if code < 127 else [''] * height
        for i, row in enumerate(rows):
            lines.append(row + ('@@' if i == height - 1 else '@'))
    return '\n'.join(lines) + '\n'


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / 'assets' / 'figlet'
    out.mkdir(parents=True, exist_ok=True)
    for name, spec in FACES.items():
        (out / f'{name}.flf').write_text(face(*spec))
        print(out / f'{name}.flf')


main()
