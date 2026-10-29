// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PALETTE_H
#define PALETTE_H

#include "compose.h"
#include "grid.h"

#define RAMP_STEPS 6

typedef struct {
    char name[24];
    Rgb bg;
    // From barely-there to brightest.
    Rgb ramp[RAMP_STEPS];
    Rgb accent[3];
    // Light grounds take no glow: bloom would wash them out.
    int light;
    // The rainbow paints by place instead of brightness, or a scene would show only the few hues its brightness
    // spans: its ramp is plain gray, and each lit cell takes its hue from where it sits.
    int rainbow;
} Palette;

Rgbf palette_ramp(const Palette *p, float v);
Rgbf palette_accent(const Palette *p, int i);
// Repaints a drawn grid in the rainbow: every lit cell keeps its brightness in its place's hue.
void palette_paint_rainbow(Grid *g, float t);
// The rainbow's hue at a cell, at full brightness.
Rgbf palette_rainbow_at(const Grid *g, int col, int row, float t);
// A piece's palettes: [0] for its calm parts, [1] and [3] taking turns through its drops, [2] for builds and breaks.
#define PALETTE_FAMILY 4

// Fills a piece's palettes, nearly always several different ones, so a voyage changes color as it goes. Each is
// hand-made or generated, and any can appear in any mood; the mood only tips the odds.
void palette_family(Rng *r, Mood mood, Palette out[PALETTE_FAMILY]);
// How many hand-made palettes there are, and one of them by index, for looking at them all.
int palette_curated_count(void);
void palette_curated(int index, Palette *out);

#endif
