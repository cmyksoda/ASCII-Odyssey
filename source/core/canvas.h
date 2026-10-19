// SPDX-License-Identifier: GPL-3.0-only
#ifndef CANVAS_H
#define CANVAS_H

#include "grid.h"

// The hidden picture behind a grid: 2x4 samples per cell, each with ink (coverage) and color.
// Scene units: y runs -1 (bottom) to 1 (top), x runs -aspect to aspect.
typedef struct {
    int w, h;
    float aspect;
    // Scene units per sample.
    float sx, sy;
    float *ink;
    Rgb *color;
} Canvas;

int canvas_init(Canvas *c, const Grid *g);
void canvas_free(Canvas *c);
void canvas_clear(Canvas *c);

static inline float canvas_x(const Canvas *c, int i) {
    return ((float)i + 0.5f) * c->sx - c->aspect;
}

static inline float canvas_y(const Canvas *c, int j) {
    return 1 - ((float)j + 0.5f) * c->sy;
}

// Paints ink over what is there, the way a translucent layer would.
void canvas_over(Canvas *c, int i, int j, float ink, Rgb color);
// Keeps the stronger of the two inks.
void canvas_max(Canvas *c, int i, int j, float ink, Rgb color);
void canvas_disc(Canvas *c, float x, float y, float radius, float ink, Rgb color);
void canvas_line(Canvas *c, float x0, float y0, float x1, float y1, float width, float ink, Rgb color);

#endif
