// SPDX-License-Identifier: GPL-3.0-only
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
} Palette;

Rgbf palette_ramp(const Palette *p, float v);
Rgbf palette_accent(const Palette *p, int i);
// A piece's three palettes: one for its calm parts, a sibling for its drops, and one for its builds and breaks.
// Each is hand-made or generated, and any can appear in any mood; the mood only tips the odds.
void palette_family(Rng *r, Mood mood, Palette out[3]);
// How many hand-made palettes there are, and one of them by index, for looking at them all.
int palette_curated_count(void);
void palette_curated(int index, Palette *out);

#endif
