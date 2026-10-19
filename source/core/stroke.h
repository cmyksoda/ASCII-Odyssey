// SPDX-License-Identifier: GPL-3.0-only
#ifndef STROKE_H
#define STROKE_H

#include "grid.h"

// Classic hand-drawn ASCII line art: `'-._ for gentle slopes, / and \ for diagonals,
// ( | ) for steep runs, chosen by where the curve crosses each cell.
typedef struct {
    Grid *grid;
    // Scene units, as on a canvas for this grid: y up in [-1, 1], x in [-aspect, aspect].
    float aspect;
    // Cells the current stroke has claimed, and how close to their center it passed.
    float *best;
} Stroker;

typedef struct {
    float x, y;
    Rgb color;
} StrokePoint;

int stroker_init(Stroker *s, Grid *g);
void stroker_free(Stroker *s);
// Draws one curve; points closer than a cell apart are fine, they are walked finely anyway.
void stroke_draw(Stroker *s, const StrokePoint *points, int count, int closed);
// A curve given as a function of t in [0, 1].
typedef StrokePoint (*CurveFn)(float t, const void *context);
void stroke_curve(Stroker *s, CurveFn curve, const void *context, int segments, int closed);

#endif
