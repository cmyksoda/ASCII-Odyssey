// SPDX-License-Identifier: GPL-3.0-only
#ifndef PALETTE_H
#define PALETTE_H

#include "compose.h"
#include "grid.h"

#define RAMP_STEPS 6

typedef struct {
    const char *name;
    Rgb bg;
    // From barely-there to brightest.
    Rgb ramp[RAMP_STEPS];
    Rgb accent[3];
    // Light grounds take no glow: bloom would wash them out.
    int light;
} Palette;

extern const Palette palettes[];
extern const int palette_count;

Rgbf palette_ramp(const Palette *p, float v);
Rgbf palette_accent(const Palette *p, int i);
const Palette *palette_pick(Rng *r, Mood mood);

#endif
