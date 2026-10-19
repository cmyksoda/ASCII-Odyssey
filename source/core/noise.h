// SPDX-License-Identifier: GPL-3.0-only
#ifndef NOISE_H
#define NOISE_H

#include <stdint.h>

// A baked, tileable fractal noise texture: cheap enough to sample per pixel on the Wii.
#define NOISE_SIZE 256

void noise_init(void);
// Smooth value noise with 4 octaves in [0,1]: one coarse feature per unit, wrapping every 8 units.
float noise_fbm(float x, float y);
// One octave of the same noise.
float noise_value(float x, float y);

#endif
