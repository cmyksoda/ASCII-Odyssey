// SPDX-License-Identifier: GPL-3.0-only
#include "canvas.h"

#include "mathx.h"

#include <stdlib.h>
#include <string.h>

int canvas_init(Canvas *c, const Grid *g) {
    memset(c, 0, sizeof *c);
    c->w = g->cols * 2;
    c->h = g->rows * 4;
    c->aspect = g->aspect;
    c->sx = 2 * c->aspect / (float)c->w;
    c->sy = 2.0f / (float)c->h;
    c->ink = malloc(sizeof(float) * c->w * c->h);
    c->color = malloc(sizeof(Rgb) * c->w * c->h);
    if (!c->ink || !c->color) return -1;
    canvas_clear(c);
    return 0;
}

void canvas_free(Canvas *c) {
    free(c->ink);
    free(c->color);
    c->ink = NULL;
    c->color = NULL;
}

void canvas_clear(Canvas *c) {
    memset(c->ink, 0, sizeof(float) * c->w * c->h);
    memset(c->color, 0, sizeof(Rgb) * c->w * c->h);
}

void canvas_over(Canvas *c, int i, int j, float ink, Rgb color) {
    if (i < 0 || j < 0 || i >= c->w || j >= c->h || ink <= 0) return;
    int k = j * c->w + i;
    if (ink > 1) ink = 1;
    float old = c->ink[k], out = ink + old * (1 - ink);
    if (out <= 0) return;
    c->color[k] = rgb_mix(c->color[k], color, ink / out);
    c->ink[k] = out;
}

void canvas_max(Canvas *c, int i, int j, float ink, Rgb color) {
    if (i < 0 || j < 0 || i >= c->w || j >= c->h) return;
    int k = j * c->w + i;
    if (ink > c->ink[k]) {
        c->ink[k] = ink > 1 ? 1 : ink;
        c->color[k] = color;
    }
}

void canvas_disc(Canvas *c, float x, float y, float radius, float ink, Rgb color) {
    float soft = c->sx * 1.5f;
    int i0 = (int)((x - radius - soft + c->aspect) / c->sx), i1 = (int)((x + radius + soft + c->aspect) / c->sx) + 1;
    int j0 = (int)((1 - (y + radius + soft)) / c->sy), j1 = (int)((1 - (y - radius - soft)) / c->sy) + 1;
    for (int j = j0 < 0 ? 0 : j0; j <= j1 && j < c->h; j++)
        for (int i = i0 < 0 ? 0 : i0; i <= i1 && i < c->w; i++) {
            float d = mhypot(canvas_x(c, i) - x, canvas_y(c, j) - y);
            float a = 1 - smoothstep(radius - soft * 0.5f, radius + soft * 0.5f, d);
            if (a > 0) canvas_over(c, i, j, a * ink, color);
        }
}

void canvas_line(Canvas *c, float x0, float y0, float x1, float y1, float width, float ink, Rgb color) {
    float dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    float pad = width + c->sx * 1.5f;
    float lo_x = mmin(x0, x1) - pad, hi_x = mmax(x0, x1) + pad;
    float lo_y = mmin(y0, y1) - pad, hi_y = mmax(y0, y1) + pad;
    int i0 = (int)((lo_x + c->aspect) / c->sx), i1 = (int)((hi_x + c->aspect) / c->sx) + 1;
    int j0 = (int)((1 - hi_y) / c->sy), j1 = (int)((1 - lo_y) / c->sy) + 1;
    float soft = c->sx * 0.75f;
    for (int j = j0 < 0 ? 0 : j0; j <= j1 && j < c->h; j++)
        for (int i = i0 < 0 ? 0 : i0; i <= i1 && i < c->w; i++) {
            float px = canvas_x(c, i) - x0, py = canvas_y(c, j) - y0;
            float t = len2 > 0 ? saturate((px * dx + py * dy) / len2) : 0;
            float d = mhypot(px - dx * t, py - dy * t);
            float a = 1 - smoothstep(width * 0.5f - soft, width * 0.5f + soft, d);
            if (a > 0) canvas_over(c, i, j, a * ink, color);
        }
}
