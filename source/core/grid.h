// SPDX-License-Identifier: GPL-3.0-only
#ifndef GRID_H
#define GRID_H

#include "font.h"

#include <stdint.h>

#define FB_W 640
#define FB_H 480

typedef struct {
    uint8_t r, g, b;
} Rgb;

typedef struct {
    uint8_t glyph;
    Rgb fg;
} Cell;

// The part of the 640x480 framebuffer that is safe to draw in, and how the TV stretches it.
typedef struct {
    int x, y, w, h;
    int wide;
} Viewport;

typedef struct {
    int cols, rows, cell_w, cell_h, x0, y0;
    // On-screen width of one framebuffer pixel relative to its height: 4/3 when a 16:9 TV stretches it.
    float pixel_aspect;
    // On-screen width / height of the whole grid.
    float aspect;
    const FontFace *face;
    Cell *cells;
} Grid;

typedef struct {
    Rgb bg;
    // Glyph colors are multiplied by gain, then flash is added.
    float gain, flash;
    float glow;
    float shake_x, shake_y;
    float scanlines;
} FrameFx;

void viewport_init(Viewport *vp, int wide, float margin);
int grid_init(Grid *g, const Viewport *vp, int design_w, int cell_h);
void grid_free(Grid *g);
void grid_clear(Grid *g);
void grid_put(Grid *g, int col, int row, int glyph, Rgb fg);
// Writes only over blank cells, for stars and sparks that sit behind the picture.
void grid_put_under(Grid *g, int col, int row, int glyph, Rgb fg);
int grid_text(Grid *g, int col, int row, const char *text, Rgb fg);
void fx_reset(FrameFx *fx, Rgb bg);

static inline Cell *grid_at(Grid *g, int col, int row) {
    return &g->cells[row * g->cols + col];
}

static inline Rgb rgb(int r, int g, int b) {
    return (Rgb){(uint8_t)r, (uint8_t)g, (uint8_t)b};
}

Rgb rgb_mix(Rgb a, Rgb b, float t);
Rgb rgb_scale(Rgb c, float k);
Rgb rgb_add(Rgb a, Rgb b);
Rgb rgb_hex(uint32_t hex);

#endif
