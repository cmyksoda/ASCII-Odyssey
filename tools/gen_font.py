#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bake the ASCII glyph atlases and 2x4 shape vectors for every cell size into one C file.

  gen_font.py OUT.c [--preview DIR] [--font outline:BOLD.otf[,HEAVY.otf] | --font bdf:FILE.bdf[,FILE.bdf...]]
"""
import argparse
import glob
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(HERE, '..', 'vendor', 'fonts')
FIRST, LAST = 32, 126
COLUMNS = 16
SUPER = 8

# (cell width, cell height, the 4:3 width it imitates, weight). Wide faces are their 4:3 twin
# squeezed by 3/4, so a 16:9 TV stretches them back to the same shape.
FACES = [
    (6, 12, 6, 'Bold'),
    (8, 16, 8, 'Bold'),
    (5, 12, 6, 'ExtraBold'),
    (6, 16, 8, 'Bold'),
]

def outline_metrics(path):
    # Advance per em, and where ink starts and ends below the ascent line, so any face centers in its cell.
    font = ImageFont.truetype(path, 1000)
    bbox = font.getbbox('|Hgjy_', anchor='la')
    return font.getlength('M') / 1000, bbox[1] / 1000, bbox[3] / 1000


def outline_face(paths, cw, ch, design_w, weight):
    path = paths[1] if weight == 'ExtraBold' and len(paths) > 1 else paths[0]
    advance, ink_top, ink_bottom = outline_metrics(path)
    size = design_w / advance
    font = ImageFont.truetype(path, round(size * SUPER))
    top = (ch / 2 - (ink_top + ink_bottom) / 2 * size) * SUPER
    images = []
    for code in range(FIRST, LAST + 1):
        hi = Image.new('L', (design_w * SUPER, ch * SUPER), 0)
        if code != 32:
            ImageDraw.Draw(hi).text((0, top), chr(code), font=font, fill=255)
        images.append(hi)
    return images


def load_bdf(path):
    """A BDF bitmap font as {code: Image} drawn into its full cell, plus the cell size."""
    width = height = xoff = yoff = ascent = 0
    glyphs, code, bbx, rows, reading = {}, None, None, [], False
    for line in open(path, encoding='latin-1'):
        word = line.split()
        if not word:
            continue
        if word[0] == 'FONTBOUNDINGBOX':
            width, height, xoff, yoff = map(int, word[1:5])
        elif word[0] == 'FONT_ASCENT':
            ascent = int(word[1])
        elif word[0] == 'ENCODING':
            code = int(word[1])
        elif word[0] == 'BBX':
            bbx = list(map(int, word[1:5]))
        elif word[0] == 'BITMAP':
            rows, reading = [], True
        elif word[0] == 'ENDCHAR':
            reading = False
            if code is not None and FIRST <= code <= LAST:
                w, h, gx, gy = bbx
                cell = Image.new('L', (width, height), 0)
                for r, bits in enumerate(rows):
                    value = int(bits, 16)
                    for c in range(w):
                        if value >> (len(bits) * 4 - 1 - c) & 1:
                            cell.putpixel((gx - xoff + c, ascent - gy - h + r), 255)
                glyphs[code] = cell
        elif reading:
            rows.append(word[0])
    return (width, height), glyphs


def condense(glyph, width):
    """Narrows a bitmap glyph without blurring it: blank columns go first, rightmost first, then the most
    alike neighboring columns merge, so every stem stays one crisp pixel."""
    w, h = glyph.size
    cols = [[glyph.getpixel((x, y)) for y in range(h)] for x in range(w)]
    while len(cols) > width:
        blank = [i for i, col in enumerate(cols) if not any(col)]
        if blank:
            del cols[blank[-1]]
            continue
        i = min(range(len(cols) - 1), key=lambda k: sum(a != b for a, b in zip(cols[k], cols[k + 1])))
        cols[i] = [max(a, b) for a, b in zip(cols[i], cols[i + 1])]
        del cols[i + 1]
    out = Image.new('L', (width, h), 0)
    for x, col in enumerate(cols):
        for y, v in enumerate(col):
            out.putpixel((x, y), v)
    return out


def bdf_face(fonts, cw, ch, design_w, weight):
    # The native size when there is one, else the nearest larger face scaled down. A narrow twin one column
    # short drops its blank spacing column and stays crisp; narrower than that, a soft squeeze keeps letters apart.
    sizes = sorted(fonts, key=lambda s: (s != (design_w, ch), s[1] < ch, abs(s[1] - ch)))
    native = sizes[0]
    images = []
    for code in range(FIRST, LAST + 1):
        glyph = fonts[native].get(code, Image.new('L', native, 0))
        if native != (design_w, ch):
            glyph = glyph.resize((design_w, ch), Image.BOX)
        if cw == design_w - 1:
            glyph = condense(glyph, cw)
        elif cw != design_w:
            glyph = glyph.resize((cw, ch), Image.BOX)
        images.append(glyph.resize((cw * SUPER, ch * SUPER), Image.NEAREST))
    return images


def render_face(source, cw, ch, design_w, weight):
    kind, value = source
    hires = (outline_face(value, cw, ch, design_w, weight) if kind == 'outline'
             else bdf_face(value, cw, ch, design_w, weight))
    glyphs, shapes = [], []
    for hi in hires:
        if hi.width != cw * SUPER:
            hi = hi.resize((cw * SUPER, ch * SUPER), Image.BOX)
        glyphs.append(hi.resize((cw, ch), Image.BOX))
        rw, rh = cw * SUPER // 2, ch * SUPER // 4
        shape = []
        for row in range(4):
            for col in range(2):
                region = hi.crop((col * rw, row * rh, (col + 1) * rw, (row + 1) * rh))
                pixels = list(region.get_flattened_data())
                shape.append(round(sum(pixels) / len(pixels)))
        shapes.append(shape)
    return glyphs, shapes


def font_source(spec):
    # Terminus Bold, drawn pixel by pixel at both of the app's cell sizes.
    if not spec:
        spec = 'bdf:' + ','.join(os.path.join(FONTS, 'terminus', f) for f in ('ter-u12b.bdf', 'ter-u16b.bdf'))
    kind, _, files = spec.partition(':')
    paths = [p for pattern in files.split(',') for p in sorted(glob.glob(pattern))]
    if kind == 'bdf':
        fonts = dict(load_bdf(p) for p in paths)
        return 'bdf', fonts
    return 'outline', paths


def atlas_of(glyphs, cw, ch):
    rows = (len(glyphs) + COLUMNS - 1) // COLUMNS
    width, height = COLUMNS * cw, rows * ch
    # GX textures are tiled 8x4, so pad the sheet to whole tiles.
    width, height = (width + 7) // 8 * 8, (height + 3) // 4 * 4
    sheet = Image.new('L', (width, height), 0)
    for i, glyph in enumerate(glyphs):
        sheet.paste(glyph, ((i % COLUMNS) * cw, (i // COLUMNS) * ch))
    return sheet


def c_bytes(data, indent='    '):
    lines = []
    for i in range(0, len(data), 24):
        lines.append(indent + ', '.join(str(b) for b in data[i:i + 24]) + ',')
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('out')
    parser.add_argument('--preview')
    parser.add_argument('--font')
    args = parser.parse_args()
    source = font_source(args.font)
    parts = [f'// Generated by tools/gen_font.py from {args.font or "Terminus Bold (SIL OFL 1.1)"}. Do not edit.',
             '#include "font.h"', '']
    table = []
    for cw, ch, design_w, weight in FACES:
        glyphs, shapes = render_face(source, cw, ch, design_w, weight)
        sheet = atlas_of(glyphs, cw, ch)
        name = f'{cw}x{ch}'
        parts.append(f'static const unsigned char atlas_{name}[{sheet.width * sheet.height}] '
                     '__attribute__((aligned(32))) = {')
        parts.append(c_bytes(list(sheet.get_flattened_data())))
        parts.append('};')
        parts.append(f'static const unsigned char shape_{name}[{len(shapes) * 8}] = {{')
        parts.append(c_bytes([v for s in shapes for v in s]))
        parts.append('};')
        table.append(f'    {{ {cw}, {ch}, {design_w}, {sheet.width}, {sheet.height}, {COLUMNS}, '
                     f'atlas_{name}, shape_{name} }},')
        if args.preview:
            os.makedirs(args.preview, exist_ok=True)
            sheet.resize((sheet.width * 4, sheet.height * 4), Image.NEAREST).save(
                os.path.join(args.preview, f'atlas-{name}.png'))
    parts.append('')
    parts.append('const FontFace font_faces[] = {')
    parts.extend(table)
    parts.append('};')
    parts.append(f'const int font_face_count = {len(FACES)};')
    with open(args.out, 'w') as out:
        out.write('\n'.join(parts) + '\n')


if __name__ == '__main__':
    main()
