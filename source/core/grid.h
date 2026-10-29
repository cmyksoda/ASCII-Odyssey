// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GRID_H
#define GRID_H

#include "font.h"

#include <stdint.h>

#define FB_W 640
#define FB_H 480

typedef struct {
    uint8_t r, g, b;
} Rgb;

// Working colors stay in float, 0..255 per channel: the Wii's PowerPC pays dearly for every int-float conversion.
typedef struct {
    float r, g, b;
} Rgbf;

typedef struct {
    uint8_t glyph;
    Rgb fg;
    // Drawn half a cell to the right, so text of odd width can sit exactly centered.
    uint8_t nudge;
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
int grid_text(Grid *g, int col, int row, const char *text, Rgb fg);
// Where text w cells wide starts so it sits centered; when the grid can only fit it half a cell off, the caller
// nudges the cells it draws.
int grid_center(const Grid *g, int w, int *nudge);
void grid_nudge(Grid *g, int col, int row, int w);
void fx_reset(FrameFx *fx, Rgb bg);

static inline Cell *grid_at(Grid *g, int col, int row) {
    return &g->cells[row * g->cols + col];
}

static inline Rgb rgb(int r, int g, int b) {
    return (Rgb){(uint8_t)r, (uint8_t)g, (uint8_t)b};
}

static inline Rgbf rgbf(Rgb c) {
    return (Rgbf){(float)c.r, (float)c.g, (float)c.b};
}

static inline Rgbf rgbf_mix(Rgbf a, Rgbf b, float t) {
    return (Rgbf){a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

static inline Rgbf rgbf_scale(Rgbf c, float k) {
    return (Rgbf){c.r * k, c.g * k, c.b * k};
}

static inline Rgbf rgbf_add(Rgbf a, Rgbf b) {
    return (Rgbf){a.r + b.r, a.g + b.g, a.b + b.b};
}

Rgb rgb_from_f(Rgbf c);
Rgb rgb_mix(Rgb a, Rgb b, float t);
Rgb rgb_scale(Rgb c, float k);

#endif
