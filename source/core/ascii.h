// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ASCII_H
#define ASCII_H

#include "canvas.h"

typedef struct {
    // Density ramp for flat cells, emptiest first.
    const char *ramp;
    // Glyphs the shape matcher may pick for cells with an edge in them.
    const char *alphabet;
    // Sample spread below which a cell counts as flat.
    float flat;
    // Exponent that sharpens contrast inside a cell before matching.
    float contrast;
    // Ink below which a cell stays blank.
    float threshold;
    // 0 keeps the canvas color; 1 darkens it all the way down with the cell's ink.
    float shade;
    // Match by shape alone (correlation), so a line over a filled area still reads as a line.
    int correlate;
} AsciiStyle;

typedef struct {
    int count;
    unsigned char glyph[FONT_GLYPHS];
    float shape[FONT_GLYPHS][8];
    // Each glyph's shape with its mean removed and scaled to unit length, for correlation.
    float unit[FONT_GLYPHS][8];
    float boost[257];
    char ramp[FONT_GLYPHS + 1];
    int ramp_len;
    AsciiStyle style;
} AsciiMatcher;

extern const AsciiStyle ascii_default;

void ascii_prepare(AsciiMatcher *m, const FontFace *face, const AsciiStyle *style);
void ascii_resolve(const AsciiMatcher *m, const Canvas *c, Grid *g);

#endif
