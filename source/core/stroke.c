// SPDX-License-Identifier: GPL-3.0-only
#include "stroke.h"

#include "mathx.h"

#include <stdlib.h>

static int owner_id;
static int *owners;
static int owners_size;

int stroker_init(Stroker *s, Grid *g) {
    s->grid = g;
    s->aspect = g->aspect;
    s->best = malloc(sizeof(float) * g->cols * g->rows);
    if (!s->best) return -1;
    if (owners_size < g->cols * g->rows) {
        free(owners);
        owners = calloc((size_t)g->cols * g->rows, sizeof(int));
        owners_size = owners ? g->cols * g->rows : 0;
    }
    return owners ? 0 : -1;
}

void stroker_free(Stroker *s) {
    free(s->best);
    s->best = NULL;
}

#define GENTLE 1.15f
#define STEEP 3.7f

static int pick(float tx, float ty, float bend_x, float fy) {
    float steep = mabs(ty) / (mabs(tx) + 1e-6f);
    // Cells are twice as tall as wide, so '/' already stands for a 63 degree line.
    if (steep < GENTLE) {
        // The middle band is widest: long level lines should read as solid dashes, not dots.
        if (fy < 0.2f) return '`';
        if (fy < 0.36f) return '\'';
        if (fy < 0.68f) return '-';
        if (fy < 0.82f) return '.';
        return '_';
    }
    if (steep < STEEP) return (tx > 0) == (ty > 0) ? '/' : '\\';
    if (mabs(bend_x) < 0.6f) return '|';
    return bend_x > 0 ? '(' : ')';
}

static void plot(Stroker *s, int col, int row, float off, float tx, float ty, float bend_x, float fy, Rgbf color) {
    Grid *g = s->grid;
    if (col < 0 || row < 0 || col >= g->cols || row >= g->rows) return;
    int k = row * g->cols + col;
    if (owners[k] == owner_id && off >= s->best[k]) return;
    owners[k] = owner_id;
    s->best[k] = off;
    g->cells[k] = (Cell){(uint8_t)pick(tx, ty, bend_x, fy), rgb_from_f(color), 0};
}

// Walks one segment like a pen: one glyph per column while it is gentle, one per row once it is steep.
static void segment(Stroker *s, const StrokePoint *a, const StrokePoint *b, float bend_x) {
    Grid *g = s->grid;
    float sx = (float)g->cols / (2 * s->aspect), sy = 0.5f * (float)g->rows;
    float ax = (a->x + s->aspect) * sx, ay = (1 - a->y) * sy, bx = (b->x + s->aspect) * sx, by = (1 - b->y) * sy;
    float dx = bx - ax, dy = by - ay, tx = b->x - a->x, ty = b->y - a->y;
    if (mabs(dx) >= mabs(dy)) {
        if (dx == 0) return;
        float lo = mmin(ax, bx), hi = mmax(ax, bx);
        for (int col = mfloor_i(lo - 0.5f) + 1; (float)col + 0.5f < hi; col++) {
            float t = ((float)col + 0.5f - ax) / dx, y = ay + dy * t;
            int row = mfloor_i(y);
            float fy = y - (float)row;
            plot(s, col, row, mabs(fy - 0.5f), tx, ty, bend_x, fy, rgbf_mix(a->color, b->color, t));
        }
    } else {
        float lo = mmin(ay, by), hi = mmax(ay, by);
        for (int row = mfloor_i(lo - 0.5f) + 1; (float)row + 0.5f < hi; row++) {
            float t = ((float)row + 0.5f - ay) / dy, x = ax + dx * t;
            int col = mfloor_i(x);
            float fx = x - (float)col;
            plot(s, col, row, mabs(fx - 0.5f), tx, ty, bend_x, 0.5f, rgbf_mix(a->color, b->color, t));
        }
    }
}

void stroke_draw(Stroker *s, const StrokePoint *p, int count, int closed) {
    if (count < 2) return;
    owner_id++;
    int segments = closed ? count : count - 1;
    for (int i = 0; i < segments; i++) {
        const StrokePoint *a = &p[i], *b = &p[(i + 1) % count];
        const StrokePoint *prev = &p[i > 0 ? i - 1 : closed ? count - 1 : 0];
        const StrokePoint *next = &p[(i + 2) % count];
        if (!closed && i + 2 >= count) next = b;
        // Which side the curve bends toward, from the turn between its neighbors.
        float in_x = a->x - prev->x, in_y = a->y - prev->y, out_x = next->x - b->x, out_y = next->y - b->y;
        float in_l = mhypot(in_x, in_y), out_l = mhypot(out_x, out_y);
        float bend_x = 0;
        // The x part of the curvature: positive when the curve turns around a center to its right.
        if (in_l > 0 && out_l > 0) bend_x = (out_x / out_l - in_x / in_l) * 2 / (in_l + out_l);
        segment(s, a, b, bend_x);
    }
}

void stroke_curve(Stroker *s, CurveFn curve, const void *context, int segments, int closed) {
    StrokePoint points[512];
    if (segments > 511) segments = 511;
    for (int i = 0; i <= segments; i++) points[i] = curve((float)i / (float)segments, context);
    stroke_draw(s, points, closed ? segments : segments + 1, closed);
}
