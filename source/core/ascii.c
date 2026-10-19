// SPDX-License-Identifier: GPL-3.0-only
#include "ascii.h"

#include "mathx.h"

#include <string.h>

const AsciiStyle ascii_default = {
    .ramp = " .:-=+*#%@",
    .alphabet = " .,:;'`\"^~-_=+*<>/\\|()[]{}!#%&@$0Oo8",
    .flat = 0.09f,
    .contrast = 1.6f,
    .threshold = 0.08f,
    .shade = 0,
    .correlate = 0,
};

void ascii_prepare(AsciiMatcher *m, const FontFace *face, const AsciiStyle *style) {
    memset(m, 0, sizeof *m);
    m->style = *style;
    float top = 0;
    for (const char *p = style->alphabet; *p && m->count < FONT_GLYPHS; p++) {
        int code = (unsigned char)*p;
        if (code < FONT_FIRST || code > FONT_LAST) continue;
        const unsigned char *s = &face->shape[(code - FONT_FIRST) * 8];
        float mean = 0, norm = 0;
        for (int k = 0; k < 8; k++) mean += s[k] / 255.0f / 8;
        for (int k = 0; k < 8; k++) norm += (s[k] / 255.0f - mean) * (s[k] / 255.0f - mean);
        // A glyph with no shape to speak of (a space, a centered bar) can't be told apart by correlation.
        if (style->correlate && norm < 0.004f) continue;
        m->glyph[m->count] = (unsigned char)code;
        for (int k = 0; k < 8; k++) {
            m->shape[m->count][k] = s[k] / 255.0f;
            m->unit[m->count][k] = norm > 0 ? (s[k] / 255.0f - mean) / msqrt(norm) : 0;
            if (m->shape[m->count][k] > top) top = m->shape[m->count][k];
        }
        m->count++;
    }
    // One normalizer for every glyph keeps their relative weights honest.
    for (int g = 0; g < m->count; g++)
        for (int k = 0; k < 8; k++) m->shape[g][k] /= top > 0 ? top : 1;
    for (int i = 0; i <= 256; i++) m->boost[i] = mpow((float)i / 256.0f, style->contrast);
    for (const char *p = style->ramp; *p && m->ramp_len < FONT_GLYPHS; p++) m->ramp[m->ramp_len++] = *p;
}

static int match_coverage(const AsciiMatcher *m, const float *v, float peak) {
    float w[8];
    for (int k = 0; k < 8; k++) w[k] = m->boost[(int)(v[k] / peak * 256.0f)] * peak;
    int best = 0;
    float best_d = 1e30f;
    for (int g = 0; g < m->count; g++) {
        const float *s = m->shape[g];
        float d = 0;
        for (int k = 0; k < 8 && d < best_d; k++) {
            float e = w[k] - s[k];
            d += e * e;
        }
        if (d < best_d) {
            best_d = d;
            best = g;
        }
    }
    return m->glyph[best];
}

static int match_shape(const AsciiMatcher *m, const float *v, float mean) {
    float c[8];
    for (int k = 0; k < 8; k++) c[k] = v[k] - mean;
    int best = -1;
    float best_dot = 0;
    for (int g = 0; g < m->count; g++) {
        const float *u = m->unit[g];
        float dot = c[0] * u[0] + c[1] * u[1] + c[2] * u[2] + c[3] * u[3] + c[4] * u[4] + c[5] * u[5] +
                    c[6] * u[6] + c[7] * u[7];
        if (dot > best_dot) {
            best_dot = dot;
            best = g;
        }
    }
    return best < 0 ? -1 : m->glyph[best];
}

static int ramp_glyph(const AsciiMatcher *m, float level) {
    int i = (int)(level * (float)m->ramp_len);
    return m->ramp[clampi(i, 0, m->ramp_len - 1)];
}

void ascii_resolve(const AsciiMatcher *m, const Canvas *c, Grid *g) {
    const AsciiStyle *st = &m->style;
    for (int row = 0; row < g->rows; row++)
        for (int col = 0; col < g->cols; col++) {
            float v[8], sum = 0, peak = 0, r = 0, gr = 0, b = 0;
            for (int y = 0; y < 4; y++)
                for (int x = 0; x < 2; x++) {
                    int k = (row * 4 + y) * c->w + col * 2 + x;
                    float a = c->ink[k];
                    v[y * 2 + x] = a;
                    sum += a;
                    if (a > peak) peak = a;
                    r += c->color[k].r * a;
                    gr += c->color[k].g * a;
                    b += c->color[k].b * a;
                }
            Cell *cell = grid_at(g, col, row);
            if (peak < st->threshold || sum <= 0) {
                *cell = (Cell){' ', {0, 0, 0}};
                continue;
            }
            float avg = sum / 8, spread = 0;
            for (int k = 0; k < 8; k++) spread += (v[k] - avg) * (v[k] - avg);
            spread = msqrt(spread / 8);
            int glyph = -1;
            if (spread >= st->flat) glyph = st->correlate ? match_shape(m, v, avg) : match_coverage(m, v, peak);
            if (glyph < 0) glyph = ramp_glyph(m, avg);
            float k = (1 - st->shade) + st->shade * avg;
            Rgb fg = {(uint8_t)(r / sum * k), (uint8_t)(gr / sum * k), (uint8_t)(b / sum * k)};
            *cell = (Cell){(uint8_t)glyph, fg};
        }
}
