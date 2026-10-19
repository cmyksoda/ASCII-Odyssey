// SPDX-License-Identifier: GPL-3.0-only
#include "softgfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

int image_init(Image *im, int w, int h) {
    im->w = w;
    im->h = h;
    im->rgb = calloc((size_t)w * h, 3);
    return im->rgb ? 0 : -1;
}

void image_free(Image *im) {
    free(im->rgb);
    im->rgb = NULL;
}

static uint8_t clamp8(float v) {
    return v <= 0 ? 0 : v >= 255 ? 255 : (uint8_t)(v + 0.5f);
}

static void draw_grid(const Grid *g, const FrameFx *fx, Image *fb) {
    const FontFace *f = g->face;
    int sx = (int)(fx->shake_x >= 0 ? fx->shake_x + 0.5f : fx->shake_x - 0.5f);
    int sy = (int)(fx->shake_y >= 0 ? fx->shake_y + 0.5f : fx->shake_y - 0.5f);
    for (int row = 0; row < g->rows; row++)
        for (int col = 0; col < g->cols; col++) {
            const Cell *c = &g->cells[row * g->cols + col];
            if (c->glyph <= ' ' || c->glyph > FONT_LAST) continue;
            int index = c->glyph - FONT_FIRST;
            int ax = (index % f->columns) * f->cell_w, ay = (index / f->columns) * f->cell_h;
            float r = c->fg.r * fx->gain + fx->flash * 255, gr = c->fg.g * fx->gain + fx->flash * 255;
            float b = c->fg.b * fx->gain + fx->flash * 255;
            int x0 = g->x0 + col * g->cell_w + (c->nudge ? g->cell_w / 2 : 0) + sx, y0 = g->y0 + row * g->cell_h + sy;
            for (int y = 0; y < f->cell_h; y++) {
                int py = y0 + y;
                if (py < 0 || py >= fb->h) continue;
                for (int x = 0; x < f->cell_w; x++) {
                    int px = x0 + x;
                    if (px < 0 || px >= fb->w) continue;
                    float a = f->atlas[(ay + y) * f->atlas_w + ax + x] / 255.0f;
                    if (a <= 0) continue;
                    uint8_t *p = &fb->rgb[(py * fb->w + px) * 3];
                    p[0] = clamp8(p[0] + (r - p[0]) * a);
                    p[1] = clamp8(p[1] + (gr - p[1]) * a);
                    p[2] = clamp8(p[2] + (b - p[2]) * a);
                }
            }
        }
}

// Glow: a blurred half-resolution copy added back, like the Wii's EFB copy pass.
static void glow(Image *fb, float amount) {
    int w = fb->w / 2, h = fb->h / 2;
    float *half = calloc((size_t)w * h * 3, sizeof(float)), *tmp = calloc((size_t)w * h * 3, sizeof(float));
    if (!half || !tmp) {
        free(half);
        free(tmp);
        return;
    }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (int k = 0; k < 3; k++) {
                const uint8_t *p = fb->rgb;
                int i = ((2 * y) * fb->w + 2 * x) * 3 + k;
                half[(y * w + x) * 3 + k] = (p[i] + p[i + 3] + p[i + fb->w * 3] + p[i + fb->w * 3 + 3]) / 4.0f;
            }
    static const float kernel[5] = {0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f};
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                for (int k = 0; k < 3; k++) {
                    float s = 0;
                    for (int t = -2; t <= 2; t++) {
                        int xx = x + t * (pass + 1);
                        xx = xx < 0 ? 0 : xx >= w ? w - 1 : xx;
                        s += half[(y * w + xx) * 3 + k] * kernel[t + 2];
                    }
                    tmp[(y * w + x) * 3 + k] = s;
                }
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                for (int k = 0; k < 3; k++) {
                    float s = 0;
                    for (int t = -2; t <= 2; t++) {
                        int yy = y + t * (pass + 1);
                        yy = yy < 0 ? 0 : yy >= h ? h - 1 : yy;
                        s += tmp[(yy * w + x) * 3 + k] * kernel[t + 2];
                    }
                    half[(y * w + x) * 3 + k] = s;
                }
    }
    for (int y = 0; y < fb->h; y++)
        for (int x = 0; x < fb->w; x++) {
            float fx = (x - 0.5f) / 2, fy = (y - 0.5f) / 2;
            int x0 = fx < 0 ? 0 : (int)fx, y0 = fy < 0 ? 0 : (int)fy;
            int x1 = x0 + 1 < w ? x0 + 1 : x0, y1 = y0 + 1 < h ? y0 + 1 : y0;
            float ax = fx - x0, ay = fy - y0;
            if (ax < 0) ax = 0;
            if (ay < 0) ay = 0;
            for (int k = 0; k < 3; k++) {
                float a = half[(y0 * w + x0) * 3 + k], b = half[(y0 * w + x1) * 3 + k];
                float c = half[(y1 * w + x0) * 3 + k], d = half[(y1 * w + x1) * 3 + k];
                float v = (a + (b - a) * ax) * (1 - ay) + (c + (d - c) * ax) * ay;
                uint8_t *p = &fb->rgb[(y * fb->w + x) * 3 + k];
                *p = clamp8(*p + v * amount);
            }
        }
    free(half);
    free(tmp);
}

void soft_render(const Screen *s, Image *fb) {
    for (int i = 0; i < fb->w * fb->h; i++) {
        fb->rgb[i * 3] = s->fx.bg.r;
        fb->rgb[i * 3 + 1] = s->fx.bg.g;
        fb->rgb[i * 3 + 2] = s->fx.bg.b;
    }
    if (s->scene) draw_grid(s->scene, &s->fx, fb);
    if (s->fx.glow > 0) glow(fb, s->fx.glow);
    FrameFx plain = s->fx;
    plain.shake_x = plain.shake_y = 0;
    plain.gain = 1;
    plain.flash = 0;
    if (s->ui) draw_grid(s->ui, &plain, fb);
    if (s->fx.scanlines > 0)
        for (int y = 1; y < fb->h; y += 2)
            for (int x = 0; x < fb->w * 3; x++) {
                uint8_t *p = &fb->rgb[y * fb->w * 3 + x];
                *p = clamp8(*p * (1 - 0.35f * s->fx.scanlines));
            }
}

void soft_present(const Image *fb, int wide, int scale, Image *out) {
    int w = wide ? fb->w * 4 / 3 : fb->w;
    if (out->w != w * scale || out->h != fb->h * scale) {
        image_free(out);
        image_init(out, w * scale, fb->h * scale);
    }
    for (int y = 0; y < out->h; y++)
        for (int x = 0; x < out->w; x++) {
            // Horizontal stretch is filtered like a TV's scaler; whole-number zoom stays sharp.
            float sxf = ((x / scale) + 0.5f) * fb->w / w - 0.5f;
            int sx0 = sxf < 0 ? 0 : (int)sxf, sx1 = sx0 + 1 < fb->w ? sx0 + 1 : sx0;
            float t = sxf - sx0;
            if (t < 0) t = 0;
            if (!wide) {
                sx0 = sx1 = x / scale;
                t = 0;
            }
            const uint8_t *a = &fb->rgb[((y / scale) * fb->w + sx0) * 3], *b = &fb->rgb[((y / scale) * fb->w + sx1) * 3];
            uint8_t *p = &out->rgb[(y * out->w + x) * 3];
            for (int k = 0; k < 3; k++) p[k] = clamp8(a[k] + (b[k] - a[k]) * t);
        }
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n) {
    uint8_t head[8];
    put32(head, n);
    memcpy(head + 4, type, 4);
    fwrite(head, 1, 8, f);
    if (n) fwrite(data, 1, n, f);
    uint32_t crc = (uint32_t)crc32(0, (const Bytef *)type, 4);
    if (n) crc = (uint32_t)crc32(crc, data, n);
    uint8_t tail[4];
    put32(tail, crc);
    fwrite(tail, 1, 4, f);
}

int png_write(const char *path, const Image *im) {
    size_t raw_n = (size_t)im->h * (im->w * 3 + 1);
    uint8_t *raw = malloc(raw_n);
    uLongf packed_n = compressBound(raw_n);
    uint8_t *packed = malloc(packed_n);
    if (!raw || !packed) {
        free(raw);
        free(packed);
        return -1;
    }
    for (int y = 0; y < im->h; y++) {
        raw[y * (im->w * 3 + 1)] = 0;
        memcpy(&raw[y * (im->w * 3 + 1) + 1], &im->rgb[y * im->w * 3], im->w * 3);
    }
    compress2(packed, &packed_n, raw, raw_n, 6);
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(raw);
        free(packed);
        return -1;
    }
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    put32(ihdr, im->w);
    put32(ihdr + 4, im->h);
    ihdr[8] = 8;
    ihdr[9] = 2;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", packed, (uint32_t)packed_n);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(packed);
    return 0;
}
