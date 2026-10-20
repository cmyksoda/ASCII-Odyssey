// SPDX-License-Identifier: GPL-3.0-only
// Test patterns for tuning the ASCII matcher: lines at every angle, rings, discs and a shaded sphere.
#include "lab.h"

#include "../core/ascii.h"
#include "../core/mathx.h"
#include "../core/stroke.h"
#include "softgfx.h"

#include <stdio.h>
#include <string.h>

static const Rgb WHITE = {235, 235, 245}, GOLD = {255, 210, 140}, ROSE = {230, 120, 150};

static void ellipse(Canvas *c, float cx, float cy, float rx, float ry, float width, float ink, Rgbf color) {
    float px = 0, py = 0;
    for (int s = 0; s <= 120; s++) {
        float a = (float)s / 120.0f * TAU_F, x = cx + mcos(a) * rx, y = cy + msin(a) * ry;
        if (s > 0) canvas_line(c, px, py, x, y, width, ink, color);
        px = x;
        py = y;
    }
}

static void sphere(Canvas *c, float cx, float cy, float r, float floor_ink) {
    for (int j = 0; j < c->h; j++)
        for (int i = 0; i < c->w; i++) {
            float x = (canvas_x(c, i) - cx) / r, y = (canvas_y(c, j) - cy) / r, d2 = x * x + y * y;
            if (d2 >= 1) continue;
            float z = msqrt(1 - d2), lambert = mmax(0, -0.6f * x + 0.5f * y + 0.62f * z);
            canvas_max(c, i, j, floor_ink + (1 - floor_ink) * lambert,
                       rgbf_mix((Rgbf){60, 30, 70}, rgbf(ROSE), lambert));
        }
}

static void patterns(Canvas *c) {
    canvas_clear(c);
    float w = c->sy * 1.1f;
    for (int k = 0; k < 12; k++) {
        float a = (float)k / 12.0f * PI_F;
        float dx = mcos(a) * 0.36f, dy = msin(a) * 0.36f;
        canvas_line(c, -0.8f - dx, 0.5f - dy, -0.8f + dx, 0.5f + dy, w, 1, rgbf(WHITE));
    }
    for (int k = 1; k <= 4; k++) ellipse(c, 0.15f, 0.5f, 0.1f * (float)k, 0.1f * (float)k, w, 1, rgbf(GOLD));
    for (int k = 1; k <= 3; k++) ellipse(c, 0.95f, 0.55f, 0.12f * (float)k, 0.035f * (float)k, w, 1, rgbf(GOLD));
    canvas_disc(c, -1.05f, -0.45f, 0.05f, 1, rgbf(WHITE));
    canvas_disc(c, -0.85f, -0.45f, 0.1f, 1, rgbf(WHITE));
    canvas_disc(c, -0.5f, -0.45f, 0.2f, 0.6f, rgbf(WHITE));
    sphere(c, 0.2f, -0.5f, 0.32f, 0.25f);
    sphere(c, 0.95f, -0.5f, 0.3f, 0.25f);
    ellipse(c, 0.95f, -0.5f, 0.5f, 0.12f, w, 0.95f, rgbf(GOLD));
}

typedef struct {
    float cx, cy, rx, ry;
    Rgbf color;
} Ellipse;

static StrokePoint ellipse_at(float t, const void *context) {
    const Ellipse *e = context;
    float a = t * TAU_F;
    return (StrokePoint){e->cx + mcos(a) * e->rx, e->cy + msin(a) * e->ry, e->color};
}

static void strokes(Grid *g) {
    Stroker st;
    if (stroker_init(&st, g) != 0) return;
    grid_clear(g);
    for (int k = 0; k < 12; k++) {
        float a = (float)k / 12.0f * PI_F;
        float dx = mcos(a) * 0.36f, dy = msin(a) * 0.36f;
        StrokePoint line[2] = {{-0.8f - dx, 0.5f - dy, rgbf(WHITE)}, {-0.8f + dx, 0.5f + dy, rgbf(WHITE)}};
        stroke_draw(&st, line, 2, 0);
    }
    for (int k = 1; k <= 4; k++) {
        Ellipse e = {0.15f, 0.5f, 0.1f * (float)k, 0.1f * (float)k, rgbf(GOLD)};
        stroke_curve(&st, ellipse_at, &e, 120, 1);
    }
    for (int k = 1; k <= 3; k++) {
        Ellipse e = {0.95f, 0.55f, 0.12f * (float)k, 0.035f * (float)k, rgbf(GOLD)};
        stroke_curve(&st, ellipse_at, &e, 120, 1);
    }
    for (int k = 0; k < 5; k++) {
        StrokePoint wave[64];
        for (int i = 0; i < 64; i++) {
            float x = -1.25f + (float)i / 63.0f * 1.2f;
            wave[i] = (StrokePoint){x, -0.3f - 0.13f * (float)k + 0.05f * msin(x * (4.0f + (float)k * 3)), rgbf(ROSE)};
        }
        stroke_draw(&st, wave, 64, 0);
    }
    Ellipse ring = {0.7f, -0.55f, 0.45f, 0.13f, rgbf(GOLD)};
    stroke_curve(&st, ellipse_at, &ring, 160, 1);
    stroker_free(&st);
}

int lab_main(const char *out_dir) {
    Viewport vp;
    viewport_init(&vp, 0, 0);
    Grid g;
    Canvas c;
    if (grid_init(&g, &vp, 6, 12) != 0 || canvas_init(&c, &g) != 0) return 1;
    patterns(&c);
    struct {
        const char *name;
        int correlate;
        float flat;
    } variants[] = {{"coverage", 0, 0.09f}, {"correlate", 1, 0.09f}, {"correlate-flat15", 1, 0.15f}};
    Image fb, shown = {0};
    image_init(&fb, FB_W, FB_H);
    for (unsigned v = 0; v < sizeof variants / sizeof variants[0]; v++) {
        AsciiStyle style = ascii_default;
        style.correlate = variants[v].correlate;
        style.flat = variants[v].flat;
        AsciiMatcher m;
        ascii_prepare(&m, g.face, &style);
        ascii_resolve(&m, &c, &g);
        Screen s = {.scene = &g};
        fx_reset(&s.fx, (Rgb){6, 7, 16});
        soft_render(&s, &fb);
        soft_present(&fb, 0, 2, &shown);
        char path[512];
        snprintf(path, sizeof path, "%s/lab-%s.png", out_dir, variants[v].name);
        png_write(path, &shown);
    }
    strokes(&g);
    Screen s = {.scene = &g};
    fx_reset(&s.fx, (Rgb){6, 7, 16});
    soft_render(&s, &fb);
    soft_present(&fb, 0, 2, &shown);
    char path[512];
    snprintf(path, sizeof path, "%s/lab-strokes.png", out_dir);
    png_write(path, &shown);
    image_free(&fb);
    image_free(&shown);
    canvas_free(&c);
    grid_free(&g);
    return 0;
}
