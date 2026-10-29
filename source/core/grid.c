// SPDX-License-Identifier: GPL-3.0-or-later
#include "grid.h"

#include <stdlib.h>
#include <string.h>

void viewport_init(Viewport *vp, int wide, float margin) {
    if (margin < 0) margin = 0;
    if (margin > 0.15f) margin = 0.15f;
    int mx = (int)(FB_W * margin), my = (int)(FB_H * margin);
    vp->x = mx;
    vp->y = my;
    vp->w = FB_W - 2 * mx;
    vp->h = FB_H - 2 * my;
    vp->wide = wide;
}

int grid_init(Grid *g, const Viewport *vp, int design_w, int cell_h) {
    memset(g, 0, sizeof *g);
    g->face = font_face(design_w, cell_h, vp->wide);
    g->cell_w = g->face->cell_w;
    g->cell_h = g->face->cell_h;
    g->cols = vp->w / g->cell_w;
    g->rows = vp->h / g->cell_h;
    g->x0 = vp->x + (vp->w - g->cols * g->cell_w) / 2;
    g->y0 = vp->y + (vp->h - g->rows * g->cell_h) / 2;
    g->pixel_aspect = vp->wide ? 4.0f / 3.0f : 1.0f;
    g->aspect = (float)(g->cols * g->cell_w) * g->pixel_aspect / (float)(g->rows * g->cell_h);
    g->cells = malloc(sizeof(Cell) * g->cols * g->rows);
    if (!g->cells) return -1;
    grid_clear(g);
    return 0;
}

void grid_free(Grid *g) {
    free(g->cells);
    g->cells = NULL;
}

void grid_clear(Grid *g) {
    for (int i = 0; i < g->cols * g->rows; i++) g->cells[i] = (Cell){' ', {0, 0, 0}, 0};
}

void grid_put(Grid *g, int col, int row, int glyph, Rgb fg) {
    if (col < 0 || row < 0 || col >= g->cols || row >= g->rows) return;
    if (glyph < FONT_FIRST || glyph > FONT_LAST) glyph = ' ';
    *grid_at(g, col, row) = (Cell){(uint8_t)glyph, fg, 0};
}

int grid_center(const Grid *g, int w, int *nudge) {
    *nudge = (g->cols - w) & 1;
    return (g->cols - w) / 2;
}

void grid_nudge(Grid *g, int col, int row, int w) {
    if (row < 0 || row >= g->rows) return;
    for (int x = col < 0 ? 0 : col; x < col + w && x < g->cols; x++) grid_at(g, x, row)->nudge = 1;
}

int grid_text(Grid *g, int col, int row, const char *text, Rgb fg) {
    int n = 0;
    for (; text[n]; n++)
        if (text[n] != ' ') grid_put(g, col + n, row, (unsigned char)text[n], fg);
    return n;
}

void fx_reset(FrameFx *fx, Rgb bg) {
    memset(fx, 0, sizeof *fx);
    fx->bg = bg;
    fx->gain = 1;
}

static uint8_t channel(float v) {
    return v <= 0 ? 0 : v >= 255 ? 255 : (uint8_t)(v + 0.5f);
}

Rgb rgb_from_f(Rgbf c) {
    return (Rgb){channel(c.r), channel(c.g), channel(c.b)};
}

Rgb rgb_mix(Rgb a, Rgb b, float t) {
    return (Rgb){channel(a.r + (b.r - a.r) * t), channel(a.g + (b.g - a.g) * t), channel(a.b + (b.b - a.b) * t)};
}

Rgb rgb_scale(Rgb c, float k) {
    return (Rgb){channel(c.r * k), channel(c.g * k), channel(c.b * k)};
}
