// SPDX-License-Identifier: GPL-3.0-only
// The demoscene's classics in text: fire, the twister, rotozoom, vector balls, a torus and a spirograph.
#include "mathx.h"
#include "noise.h"
#include "scene.h"

static float cell_x(const Grid *g, int col) {
    return ((float)col + 0.5f) / (float)g->cols * 2 * g->aspect - g->aspect;
}

static float cell_y(const Grid *g, int row) {
    return 1 - ((float)row + 0.5f) / (float)g->rows * 2;
}

/* ---------- Fire ---------- */

typedef struct {
    // 0 a wall of flame along the floor, 1 one bonfire, 2 two braziers.
    int shape;
    float speed, scale, seed;
} Fire;

void fire_setup(void *state, Rng *r, const Frame *f) {
    Fire *p = state;
    p->shape = rng_range(r, 0, 3);
    p->speed = rng_between(r, 0.9f, 1.4f);
    p->scale = rng_between(r, 1.8f, 2.6f);
    p->seed = rng_between(r, 0, 80);
    (void)f;
}

// How high the flames can reach above x, as a share of the screen.
static float fire_reach(const Fire *p, float x, float aspect) {
    float u = x / aspect;
    switch (p->shape) {
    case 1: return 0.95f * mexp(-u * u * 3.5f);
    case 2: return 0.8f * (mexp(-(u + 0.48f) * (u + 0.48f) * 12.0f) + mexp(-(u - 0.48f) * (u - 0.48f) * 12.0f));
    default: return 0.5f + 0.12f * msin(u * 4.0f + p->seed);
    }
}

// Flames licking up from the floor, shaped by noise rising through them. Every kick throws them higher, and the snare
// sends sparks drifting up out of them.
void fire_draw(void *state, const Frame *f) {
    Fire *p = state;
    Grid *g = f->grid;
    static const char ramp[] = " .,:;=+*#%";
    float kick = frame_pulse(f, ROLE_KICK, 0.3f, 1.0f);
    float rise = f->local * p->speed + frame_surge(f, ROLE_KICK, 0.3f, 0.35f);
    Rgbf core = palette_accent(f->palette, 1);
    for (int col = 0; col < g->cols; col++) {
        float x = cell_x(g, col), reach = fire_reach(p, x, g->aspect) * (1 + 0.3f * kick);
        if (reach < 0.03f) continue;
        for (int row = g->rows - 1; row >= 0; row--) {
            float h = (cell_y(g, row) + 1) * 0.5f, rel = h / reach;
            if (rel > 1.5f) break;
            // Two octaves of noise rising through the flame cut it into tongues: tall in the middle, licking at the top.
            float n = 0.65f * noise_fbm(x * p->scale * 2.2f + p->seed, h * p->scale - rise * 1.5f) +
                      0.35f * noise_fbm(x * p->scale * 5.0f + p->seed * 2, h * p->scale * 2.2f - rise * 2.6f);
            float heat = saturate((n - 0.2f) * 2.2f - rel * 1.05f + 0.25f);
            if (heat < 0.06f) continue;
            int glyph = ramp[clampi((int)(heat * 9.99f), 1, 9)];
            Rgbf color = rgbf_mix(palette_ramp(f->palette, 0.25f + 0.75f * heat), core, smoothstep(0.55f, 1.0f, heat) * 0.6f);
            put_cell(g, col, row, glyph, color);
        }
    }
    const ScoreEvent *hits[12];
    int n = recent_notes(f->score, ROLE_SNARE, f->t, 2.0f, hits, 12);
    for (int i = 0; i < n; i++) {
        float age = f->t - hits[i]->time, bright = (1 - age / 2.0f) * frame_bright(f);
        if (bright <= 0.05f) continue;
        for (int s = 0; s < 6; s++) {
            uint32_t id = (uint32_t)(hits[i]->time * 911.0f) * 31u + (uint32_t)s;
            float x0 = (hash01(id) * 2 - 1) * g->aspect * 0.8f;
            float x = x0 + 0.06f * msin(age * 5 + (float)s), y = -1 + fire_reach(p, x0, g->aspect) * 2 * 0.6f + age * 0.55f;
            put_cell(g, cell_col(g, x), cell_row(g, y), age < 0.5f ? '*' : '.', frame_dim(f, core, bright));
        }
    }
    f->fx->glow = 0.6f + 0.3f * kick;
}

/* ---------- Twister ---------- */

typedef struct {
    int columns, bands;
    float twist, spin, width, wave;
} Twister;

void twister_setup(void *state, Rng *r, const Frame *f) {
    Twister *t = state;
    t->columns = rng_range(r, 1, 4);
    t->bands = rng_chance(r, 50) ? rng_range(r, 3, 6) : 0;
    t->twist = rng_between(r, 1.5f, 3.2f);
    t->spin = rng_between(r, 0.6f, 1.1f) * (rng_chance(r, 50) ? 1 : -1);
    t->width = rng_between(r, 0.2f, 0.3f) / (float)t->columns * 1.6f;
    t->wave = rng_between(r, 0.8f, 1.6f);
    (void)f;
}

// The Amiga twister: a square column whose turn changes with height, so it wrings itself like a towel. Its faces
// take the palette's colors, every kick wrings it harder, and the snare turns its colors a face round.
void twister_draw(void *state, const Frame *f) {
    Twister *t = state;
    Grid *g = f->grid;
    dust_stars(f, 0x7a157e, 0.002f, 0.5f);
    float wring = 1 + frame_pulse(f, ROLE_KICK, 0.3f, 0.6f);
    float turn = f->local * t->spin + frame_surge(f, ROLE_KICK, 0.25f, 0.4f);
    int shift = (int)frame_surge(f, ROLE_SNARE, 0.2f, 1.0f);
    for (int c = 0; c < t->columns; c++) {
        float cx = ((float)c + 0.5f) / (float)t->columns * 2 * g->aspect * 0.8f - g->aspect * 0.8f;
        for (int row = 0; row < g->rows; row++) {
            float y = cell_y(g, row);
            float a = turn + (float)c * 0.9f + t->twist * wring * msin(y * t->wave + f->local * 0.6f + (float)c);
            float px[5];
            for (int k = 0; k < 5; k++) px[k] = cx + t->width * mcos(a + (float)k * PI_F * 0.5f);
            for (int k = 0; k < 4; k++) {
                float span = px[k + 1] - px[k];
                if (span <= 0) continue;
                // A face seen square-on is brightest; one turning away darkens.
                float facing = saturate(span / (t->width * 1.414f));
                int stripe = t->bands && ((row / t->bands) & 1);
                int face = (k + shift) & 3;
                Rgbf color = face == 3 ? palette_ramp(f->palette, 0.95f) : palette_accent(f->palette, face);
                color = frame_dim(f, color, 0.35f + 0.65f * facing * (stripe ? 0.75f : 1.0f));
                int glyph = facing > 0.7f ? (stripe ? '=' : '#') : facing > 0.35f ? (stripe ? '-' : '+') : ':';
                for (int col = cell_col(g, px[k]); col <= cell_col(g, px[k + 1]); col++) put_cell(g, col, row, glyph, color);
            }
        }
    }
    f->fx->glow = 0.55f;
}

/* ---------- Rotozoom ---------- */

typedef struct {
    int texture;
    float spin, zoom, drift_x, drift_y;
} Rotozoom;

void rotozoom_setup(void *state, Rng *r, const Frame *f) {
    Rotozoom *z = state;
    z->texture = rng_range(r, 0, 4);
    z->spin = rng_between(r, 0.15f, 0.35f) * (rng_chance(r, 50) ? 1 : -1);
    z->zoom = rng_between(r, 0.9f, 1.4f);
    z->drift_x = rng_between(r, -0.6f, 0.6f);
    z->drift_y = rng_between(r, -0.6f, 0.6f);
    (void)f;
}

// One tile of the texture at (u, v), each in [0, 1): a level from 0 to 1, and which of two inks it takes.
static float roto_texture(int texture, float u, float v, int *ink) {
    float du = u - 0.5f, dv = v - 0.5f;
    switch (texture) {
    case 0:
        *ink = (u < 0.5f) != (v < 0.5f);
        return *ink ? 1.0f : 0.35f;
    case 1: {
        float d = mabs(du) + mabs(dv);
        *ink = d > 0.25f;
        return mfract(d * 2) < 0.5f ? 1.0f : 0.25f;
    }
    case 2: {
        float d = mhypot(du, dv);
        *ink = d < 0.22f;
        return d < 0.22f ? 1.0f : smoothstep(0.42f, 0.3f, d) * 0.7f;
    }
    default:
        // Bricks.
        *ink = 0;
        if (v < 0.5f) u = mfract(u + 0.5f);
        return (mfract(v * 2) > 0.12f && mfract(u) > 0.06f) ? 0.8f : 0.1f;
    }
}

// A flat texture turning and swooping toward and away, as on the Amiga: the kick plunges it nearer, the snare kicks
// its turn on.
void rotozoom_draw(void *state, const Frame *f) {
    Rotozoom *z = state;
    Grid *g = f->grid;
    static const char ramp[] = " .:-=+*#";
    float kick = frame_pulse(f, ROLE_KICK, 0.25f, 1.0f);
    float a = f->local * z->spin + frame_surge(f, ROLE_SNARE, 0.3f, 0.25f);
    float scale = z->zoom * (1 + 0.35f * msin(f->local * 0.35f)) * (1 - 0.18f * kick), ca = mcos(a) * scale, sa = msin(a) * scale;
    float ox = z->drift_x * f->local, oy = z->drift_y * f->local;
    Rgbf inks[2] = {palette_accent(f->palette, 0), palette_ramp(f->palette, 0.85f)};
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), u = x * ca - y * sa + ox, v = x * sa + y * ca + oy;
            int ink;
            float level = roto_texture(z->texture, mfract(u), mfract(v), &ink);
            // The far edge of the swoop fades, as if into fog.
            level *= 0.7f + 0.3f * (1 - mabs(y));
            int i = (int)(level * 7.99f);
            if (i <= 0) continue;
            put_cell(g, col, row, ramp[i], frame_dim(f, inks[ink], 0.35f + 0.65f * level + 0.2f * kick));
        }
    }
    f->fx->glow = 0.45f;
}

/* ---------- Vector balls ---------- */

#define BALLS 72

typedef struct {
    int from, to;
    float spin_x, spin_y, size;
} Balls;

void balls_setup(void *state, Rng *r, const Frame *f) {
    Balls *b = state;
    b->from = rng_range(r, 0, 4);
    b->to = (b->from + rng_range(r, 1, 4)) % 4;
    b->spin_x = rng_between(r, 0.25f, 0.5f);
    b->spin_y = rng_between(r, 0.35f, 0.7f) * (rng_chance(r, 50) ? 1 : -1);
    b->size = rng_between(r, 0.7f, 0.85f);
    (void)f;
}

// Ball i's place on one of four shapes: a sphere, a cube's surface, a torus, a double helix.
static void ball_place(int shape, int i, float out[3]) {
    float t = ((float)i + 0.5f) / BALLS;
    switch (shape) {
    case 0: {
        float z = 1 - 2 * t, r = msqrt(1 - z * z), a = (float)i * 2.39996f;
        out[0] = r * mcos(a), out[1] = z, out[2] = r * msin(a);
        break;
    }
    case 1: {
        // Four faces' worth of a lattice, folded onto the cube's sides.
        int face = i % 6, k = i / 6, u = k % 4, v = k / 4;
        float a = ((float)u - 1.5f) / 1.5f, c = ((float)v - 1.5f) / 1.5f;
        float p[3] = {a, c, face & 1 ? 1.0f : -1.0f};
        out[(face / 2) % 3] = p[2], out[(face / 2 + 1) % 3] = p[0], out[(face / 2 + 2) % 3] = p[1];
        out[0] *= 0.8f, out[1] *= 0.8f, out[2] *= 0.8f;
        break;
    }
    case 2: {
        float a = (float)(i % 12) / 12 * TAU_F, c = (float)(i / 12) / 6 * TAU_F;
        out[0] = (0.75f + 0.3f * mcos(c)) * mcos(a), out[1] = 0.3f * msin(c), out[2] = (0.75f + 0.3f * mcos(c)) * msin(a);
        break;
    }
    default: {
        float a = t * TAU_F * 2.5f + (i & 1) * PI_F;
        out[0] = 0.5f * mcos(a), out[1] = t * 2 - 1, out[2] = 0.5f * msin(a);
        break;
    }
    }
}

// Shiny balls in 3D, the Amiga's vector bobs: they hold one shape, then flow into the next, all turning; the nearest
// draw biggest and over the rest. The kick swells the whole figure.
void balls_draw(void *state, const Frame *f) {
    Balls *b = state;
    Grid *g = f->grid;
    float morph = smoothstep(0.35f, 0.65f, f->local / mmax(f->length, 1));
    float swell = b->size * (1 + 0.15f * frame_pulse(f, ROLE_KICK, 0.25f, 1.0f));
    float ax = f->local * b->spin_x + frame_surge(f, ROLE_SNARE, 0.3f, 0.2f), ay = f->local * b->spin_y;
    float sx = msin(ax), cx = mcos(ax), sy = msin(ay), cy = mcos(ay);
    float px[BALLS], py[BALLS], pz[BALLS];
    int order[BALLS];
    for (int i = 0; i < BALLS; i++) {
        float a[3], c[3];
        ball_place(b->from, i, a);
        ball_place(b->to, i, c);
        float x = mixf(a[0], c[0], morph), y = mixf(a[1], c[1], morph), z = mixf(a[2], c[2], morph);
        float x1 = x * cy + z * sy, z1 = -x * sy + z * cy, y1 = y * cx - z1 * sx, z2 = y * sx + z1 * cx;
        float d = 2.6f / (2.6f + z2);
        px[i] = x1 * swell * d * g->aspect * 0.75f, py[i] = y1 * swell * d, pz[i] = z2;
        // Farthest first, so the nearest balls land on top.
        int j = i;
        while (j > 0 && pz[order[j - 1]] < z2) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = i;
    }
    Canvas *cv = f->canvas;
    canvas_clear(cv);
    for (int k = 0; k < BALLS; k++) {
        int i = order[k];
        float near = saturate(0.5f - pz[i] * 0.5f), radius = 0.035f + 0.045f * near;
        Rgbf color = rgbf_mix(palette_ramp(f->palette, 0.4f + 0.6f * near), palette_accent(f->palette, i % 3), 0.4f);
        // A ball with a lit crown: the body, then a smaller, brighter disc toward its top left.
        canvas_disc(cv, px[i], py[i], radius, 0.55f + 0.35f * near, frame_dim(f, color, 0.5f + 0.5f * near));
        if (near > 0.35f)
            canvas_disc(cv, px[i] - radius * 0.3f, py[i] + radius * 0.3f, radius * 0.45f, 1.0f,
                        rgbf_mix(color, palette_ramp(f->palette, 1.0f), 0.6f));
    }
    frame_resolve(f);
    dust_stars(f, 0xba115, 0.004f, 0.6f);
    f->fx->glow = 0.6f;
}

/* ---------- Torus ---------- */

typedef struct {
    // 0 a ringed torus, 1 a trefoil knot, 2 a cinquefoil knot.
    int form;
    float spin_x, spin_y, spin_z;
} Torus;

void torus_setup(void *state, Rng *r, const Frame *f) {
    Torus *t = state;
    t->form = rng_range(r, 0, 3);
    t->spin_x = rng_between(r, 0.2f, 0.45f);
    t->spin_y = rng_between(r, 0.3f, 0.6f) * (rng_chance(r, 50) ? 1 : -1);
    t->spin_z = rng_between(r, -0.15f, 0.15f);
    (void)f;
}

typedef struct {
    float m[9], scale, aspect;
} View;

static StrokePoint project(const View *v, const float p[3], const Frame *f, float bright) {
    float x = v->m[0] * p[0] + v->m[1] * p[1] + v->m[2] * p[2], y = v->m[3] * p[0] + v->m[4] * p[1] + v->m[5] * p[2];
    float z = v->m[6] * p[0] + v->m[7] * p[1] + v->m[8] * p[2], d = 2.8f / (2.8f + z), near = saturate(0.5f - z * 0.6f);
    Rgbf color = rgbf_mix(palette_ramp(f->palette, 0.3f + 0.7f * near), palette_accent(f->palette, near > 0.5f ? 1 : 0), 0.3f);
    return (StrokePoint){x * d * v->scale * v->aspect, y * d * v->scale, frame_dim(f, color, (0.55f + 0.6f * near) * bright)};
}

// A wireframe torus or a knot tied round one, turning in space; every kick gives it a shove.
void torus_draw(void *state, const Frame *f) {
    Torus *t = state;
    Grid *g = f->grid;
    dust_stars(f, 0x7025, 0.003f, 0.5f);
    float shove = frame_surge(f, ROLE_KICK, 0.25f, 0.35f);
    float ax = f->local * t->spin_x + shove, ay = f->local * t->spin_y, az = f->local * t->spin_z;
    float sa = msin(ax), ca = mcos(ax), sb = msin(ay), cb = mcos(ay), sc = msin(az), cc = mcos(az);
    View v = {{cb * cc, -cb * sc, sb, sa * sb * cc + ca * sc, -sa * sb * sc + ca * cc, -sa * cb, -ca * sb * cc + sa * sc,
               ca * sb * sc + sa * cc, ca * cb},
              (t->form ? 0.82f : 0.62f) * (1 + 0.1f * frame_pulse(f, ROLE_KICK, 0.25f, 1.0f)),
              g->aspect * 0.75f};
    StrokePoint pts[96];
    if (t->form == 0) {
        for (int ring = 0; ring < 18; ring++) {
            float a = (float)ring / 18 * TAU_F;
            for (int k = 0; k < 24; k++) {
                float c = (float)k / 24 * TAU_F, p[3] = {(1 + 0.4f * mcos(c)) * mcos(a), 0.4f * msin(c), (1 + 0.4f * mcos(c)) * msin(a)};
                pts[k] = project(&v, p, f, 1.0f);
            }
            stroke_draw(f->stroker, pts, 24, 1);
        }
        for (int lane = 0; lane < 6; lane++) {
            float c = (float)lane / 6 * TAU_F;
            for (int k = 0; k < 48; k++) {
                float a = (float)k / 48 * TAU_F, p[3] = {(1 + 0.4f * mcos(c)) * mcos(a), 0.4f * msin(c), (1 + 0.4f * mcos(c)) * msin(a)};
                pts[k] = project(&v, p, f, 0.7f);
            }
            stroke_draw(f->stroker, pts, 48, 1);
        }
    } else {
        // A (p, q) knot, with two thinner strands spiralling round it.
        float pk = 2, qk = t->form == 1 ? 3.0f : 5.0f;
        for (int strand = 0; strand < 3; strand++) {
            for (int k = 0; k < 96; k++) {
                float s = (float)k / 96 * TAU_F, r = 0.75f + 0.3f * mcos(qk * s);
                float p[3] = {r * mcos(pk * s), 0.3f * msin(qk * s), r * msin(pk * s)};
                if (strand) {
                    float wind = s * 9 + (float)strand * PI_F;
                    p[0] += 0.08f * mcos(wind) * mcos(pk * s), p[1] += 0.08f * msin(wind), p[2] += 0.08f * mcos(wind) * msin(pk * s);
                }
                pts[k] = project(&v, p, f, strand ? 0.6f : 1.0f);
            }
            stroke_draw(f->stroker, pts, 96, 1);
        }
    }
    f->fx->glow = 0.6f;
}

/* ---------- Spirograph ---------- */

typedef struct {
    int big, small;
    float pen, turn;
} Spirograph;

void spirograph_setup(void *state, Rng *r, const Frame *f) {
    Spirograph *s = state;
    static const int pairs[6][2] = {{5, 3}, {7, 3}, {8, 3}, {7, 2}, {9, 4}, {7, 4}};
    int k = rng_range(r, 0, 6);
    s->big = pairs[k][0];
    s->small = pairs[k][1];
    s->pen = rng_between(r, 0.6f, 1.0f);
    s->turn = rng_between(r, 0, TAU_F);
    (void)f;
}

typedef struct {
    float R, r, d, scale, aspect, turn;
} Wheel;

static void wheel_point(const Wheel *w, float t, float *x, float *y) {
    float k = (w->R - w->r) / w->r;
    float px = (w->R - w->r) * mcos(t) + w->d * mcos(k * t), py = (w->R - w->r) * msin(t) - w->d * msin(k * t);
    float c = mcos(w->turn), s = msin(w->turn);
    *x = (px * c - py * s) * w->scale * w->aspect;
    *y = (px * s + py * c) * w->scale;
}

// A spirograph drawing itself: the pen runs round inside its ring and the figure fills in over the shot. Notes hurry
// the pen along, the newest line burns brightest, and older loops cool behind it.
void spirograph_draw(void *state, const Frame *f) {
    Spirograph *s = state;
    Grid *g = f->grid;
    dust_stars(f, 0x5b16, 0.0f, 0.4f);
    Wheel w = {(float)s->big, (float)s->small, (float)s->small * s->pen, 0, g->aspect * 0.75f, s->turn + f->local * 0.03f};
    w.scale = 0.88f / (w.R - w.r + w.d);
    // The figure closes after the small wheel has gone round as many times as it takes to come back to its start.
    int loops = s->small;
    for (int a = s->big, b = s->small; b;) {
        int t = a % b;
        a = b, b = t;
        loops = s->small / a;
    }
    float whole = TAU_F * (float)loops;
    int notes = 0;
    static const Role sung[4] = {ROLE_LEAD, ROLE_BELL, ROLE_PLUCK, ROLE_ARP};
    for (int v = 0; v < 4; v++) notes += score_count(f->score, sung[v], f->t - f->local, f->t);
    float drawn = mmin(1.0f, f->local / (mmax(f->length, 1) * 0.8f) + (float)notes * 0.004f * f->react) * whole;
    Rgbf ring = frame_dim(f, palette_ramp(f->palette, 0.5f), 0.35f);
    StrokePoint pts[64];
    for (int k = 0; k < 64; k++) {
        float a = (float)k / 64 * TAU_F;
        pts[k] = (StrokePoint){mcos(a) * w.R * w.scale * w.aspect, msin(a) * w.R * w.scale, ring};
    }
    stroke_draw(f->stroker, pts, 64, 1);
    const ChordMark *chord = score_chord(f->score, f->t);
    int hue = chord ? chord->chord.root_pc : 0;
    int segments = (int)(drawn / whole * 1200) + 2;
    if (segments > 1200) segments = 1200;
    for (int k0 = 0; k0 < segments - 1; k0 += 63) {
        int n = segments - k0 < 64 ? segments - k0 : 64;
        for (int k = 0; k < n; k++) {
            float t = drawn * (float)(k0 + k) / (float)(segments - 1), age = (drawn - t) / whole, x, y;
            wheel_point(&w, t, &x, &y);
            Rgbf color = rgbf_mix(palette_accent(f->palette, hue + (k0 + k) / 200), palette_ramp(f->palette, 1.0f), 0.25f);
            pts[k] = (StrokePoint){x, y, frame_dim(f, color, 0.5f + 0.7f * mexp(-age * 4))};
        }
        stroke_draw(f->stroker, pts, n, 0);
    }
    float x, y;
    wheel_point(&w, drawn, &x, &y);
    float kx = (w.R - w.r) * mcos(drawn), ky = (w.R - w.r) * msin(drawn), c = mcos(w.turn), sn = msin(w.turn);
    float wx = (kx * c - ky * sn) * w.scale * w.aspect, wy = (kx * sn + ky * c) * w.scale;
    for (int k = 0; k < 32; k++) {
        float a = (float)k / 32 * TAU_F;
        pts[k] = (StrokePoint){wx + mcos(a) * w.r * w.scale * w.aspect, wy + msin(a) * w.r * w.scale, frame_dim(f, ring, 0.8f)};
    }
    stroke_draw(f->stroker, pts, 32, 1);
    put_cell(g, cell_col(g, x), cell_row(g, y), '*', palette_ramp(f->palette, 1.0f));
    f->fx->glow = 0.6f;
}

/* ---------- Julia set ---------- */

typedef struct {
    float angle, speed, zoom, ox, oy;
} Julia;

void julia_setup(void *state, Rng *r, const Frame *f) {
    Julia *j = state;
    j->angle = rng_between(r, 0, TAU_F);
    j->speed = rng_between(r, 0.025f, 0.05f) * (rng_chance(r, 50) ? 1 : -1);
    j->zoom = rng_between(r, 1.15f, 1.5f);
    j->ox = rng_between(r, -0.15f, 0.15f);
    j->oy = rng_between(r, -0.1f, 0.1f);
    (void)f;
}

#define JULIA_STEPS 26

// A Julia set whose parameter circles the edge of the Mandelbrot set, so its coastlines unfurl, pinch off into
// islands and fold back again; each band of escape speed takes the next shade.
void julia_draw(void *state, const Frame *f) {
    Julia *j = state;
    Grid *g = f->grid;
    static const char ramp[] = " .,:-=+*#%";
    float a = j->angle + f->local * j->speed, cr = 0.7885f * mcos(a), ci = 0.7885f * msin(a);
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row) * j->zoom + j->oy;
        for (int col = 0; col < g->cols; col++) {
            float zr = cell_x(g, col) * j->zoom + j->ox, zi = y, m = 0;
            int n = 0;
            for (; n < JULIA_STEPS; n++) {
                float r2 = zr * zr, i2 = zi * zi;
                if (r2 + i2 > 16) {
                    m = r2 + i2;
                    break;
                }
                zi = 2 * zr * zi + ci;
                zr = r2 - i2 + cr;
            }
            if (n == JULIA_STEPS) continue;
            // A smooth count, so the bands blend instead of stepping.
            float smooth = (float)n + 1 - mlog2(mlog2(m) * 0.5f), level = saturate(smooth / (float)JULIA_STEPS * 1.6f);
            // Points that fly off at once stay dark, so the set's coastline stands out against open space.
            level = saturate((level - 0.16f) / 0.84f);
            level *= level * (3 - 2 * level);
            int i = (int)(level * 9.99f);
            if (i <= 0) continue;
            Rgbf color = rgbf_mix(palette_ramp(f->palette, 0.3f + 0.7f * level), palette_accent(f->palette, (int)(smooth * 0.4f)), 0.35f);
            put_cell(g, col, row, ramp[i], color);
        }
    }
    f->fx->glow = 0.55f;
}
