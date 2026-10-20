// SPDX-License-Identifier: GPL-3.0-only
// Quiet places to rest the eyes: a koi pond, a night train's window, a campfire, an hourglass, birds flying home, a
// ringed planet, a rainy street and palms at dusk. None of them answer the music.
#include "mathx.h"
#include "noise.h"
#include "scene.h"

#include <string.h>

static float cell_x(const Grid *g, int col) {
    return ((float)col + 0.5f) / (float)g->cols * 2 * g->aspect - g->aspect;
}

static float cell_y(const Grid *g, int row) {
    return 1 - ((float)row + 0.5f) / (float)g->rows * 2;
}

static void line(const Frame *f, float x0, float y0, float x1, float y1, Rgbf color) {
    StrokePoint pts[2] = {{x0, y0, color}, {x1, y1, color}};
    stroke_draw(f->stroker, pts, 2, 0);
}

/* ---------- Koi pond ---------- */

#define KOI 5
#define PADS 6

typedef struct {
    float ax[KOI], ay[KOI], fx[KOI], fy[KOI], phase[KOI], px[PADS], py[PADS], pr[PADS];
    uint32_t salt;
} Koi;

void koi_setup(void *state, Rng *r, const Frame *f) {
    Koi *k = state;
    for (int i = 0; i < KOI; i++) {
        k->ax[i] = rng_between(r, 0.5f, 0.95f) * f->grid->aspect;
        k->ay[i] = rng_between(r, 0.45f, 0.8f);
        k->fx[i] = rng_between(r, 0.03f, 0.06f) * (rng_chance(r, 50) ? 1 : -1);
        k->fy[i] = k->fx[i] * rng_between(r, 1.3f, 2.1f);
        k->phase[i] = rng_float(r) * TAU_F;
    }
    for (int i = 0; i < PADS; i++) {
        k->px[i] = rng_between(r, -0.9f, 0.9f) * f->grid->aspect;
        k->py[i] = rng_between(r, -0.8f, 0.8f);
        k->pr[i] = rng_between(r, 0.08f, 0.15f);
    }
    k->salt = rng_u32(r);
}

static void koi_at(const Koi *k, int i, float t, float *x, float *y) {
    *x = k->ax[i] * msin(t * k->fx[i] * TAU_F + k->phase[i]);
    *y = k->ay[i] * msin(t * k->fy[i] * TAU_F + k->phase[i] * 1.7f);
}

// A koi pond from above: fish drawn in ink gliding in slow loops beneath lily pads, a flower open on some pads, and
// a ring spreading now and then where a fish nudges the surface.
void koi_draw(void *state, const Frame *f) {
    Koi *k = state;
    Grid *g = f->grid;
    Rgbf water = palette_ramp(f->palette, 0.3f), pad = palette_ramp(f->palette, 0.6f), bloom = palette_accent(f->palette, 0);
    for (int row = 0; row < g->rows; row++)
        for (int col = 0; col < g->cols; col++) {
            float n = noise_fbm(cell_x(g, col) * 2 + f->t * 0.02f, cell_y(g, row) * 3 - f->t * 0.015f);
            if (n > 0.66f) put_cell(g, col, row, n > 0.72f ? '~' : '-', frame_dim(f, water, 0.5f + (n - 0.66f) * 3));
        }
    for (int i = 0; i < KOI; i++) {
        // The body: its outline traced along the fish's own wake, widest behind the head and tapering to the tail.
        StrokePoint left[12], right[12];
        float px[12], py[12];
        for (int s = 0; s < 12; s++) koi_at(k, i, f->t - (float)s * 0.075f, &px[s], &py[s]);
        Rgbf skin = i % 3 == 2 ? palette_ramp(f->palette, 0.85f) : bloom;
        for (int s = 0; s < 12; s++) {
            int a = s ? s - 1 : 0, b = s < 11 ? s + 1 : 11;
            float tx = px[a] - px[b], ty = py[a] - py[b], len = mhypot(tx, ty) + 0.0001f;
            float w = 0.065f * msin(PI_F * ((float)s + 1.5f) / 13.0f) * (s < 2 ? 0.8f : 1.0f);
            left[s] = (StrokePoint){px[s] - ty / len * w, py[s] + tx / len * w, skin};
            right[s] = (StrokePoint){px[s] + ty / len * w, py[s] - tx / len * w, skin};
        }
        stroke_draw(f->stroker, left, 12, 0);
        stroke_draw(f->stroker, right, 12, 0);
        StrokePoint nose[2] = {left[0], right[0]};
        stroke_draw(f->stroker, nose, 2, 0);
        // A fanned tail, and a few white patches along the back.
        float tx = px[10] - px[11], ty = py[10] - py[11], len = mhypot(tx, ty) + 0.0001f;
        for (int side = -1; side <= 1; side += 2) {
            StrokePoint fin[2] = {{px[11], py[11], skin},
                                  {px[11] - tx / len * 0.06f - ty / len * 0.05f * (float)side, py[11] - ty / len * 0.06f + tx / len * 0.05f * (float)side, skin}};
            stroke_draw(f->stroker, fin, 2, 0);
        }
        for (int s = 2; s < 9; s += 3)
            put_cell(g, cell_col(g, px[s]), cell_row(g, py[s]), 'o', hash01((uint32_t)(i * 7 + s)) < 0.6f ? palette_ramp(f->palette, 1.0f) : skin);
        put_cell(g, cell_col(g, px[0]), cell_row(g, py[0]), '.', palette_ramp(f->palette, 1.0f));
    }
    // Lily pads float over the fish: round, with the notch every pad has, and veins from the middle.
    for (int i = 0; i < PADS; i++) {
        float r = k->pr[i], notch = (float)i * 1.9f;
        for (int row = cell_row(g, k->py[i] + r); row <= cell_row(g, k->py[i] - r); row++)
            for (int col = cell_col(g, k->px[i] - r); col <= cell_col(g, k->px[i] + r); col++) {
                float dx = cell_x(g, col) - k->px[i], dy = cell_y(g, row) - k->py[i], a = matan2(dy, dx) - notch;
                a -= mfloor(a / TAU_F + 0.5f) * TAU_F;
                if (mhypot(dx, dy) > r || mabs(a) < 0.3f) continue;
                put_cell(g, col, row, ' ', pad);
            }
        StrokePoint rim[33];
        for (int p = 0; p <= 32; p++) {
            float a = notch + 0.3f + (TAU_F - 0.6f) * (float)p / 32;
            rim[p] = (StrokePoint){k->px[i] + mcos(a) * r, k->py[i] + msin(a) * r, pad};
        }
        stroke_draw(f->stroker, rim, 33, 0);
        for (int v = 0; v < 5; v++) {
            float a = notch + 0.9f + (float)v * 1.1f;
            StrokePoint vein[2] = {{k->px[i], k->py[i], frame_dim(f, pad, 0.6f)},
                                   {k->px[i] + mcos(a) * r * 0.8f, k->py[i] + msin(a) * r * 0.8f, frame_dim(f, pad, 0.6f)}};
            stroke_draw(f->stroker, vein, 2, 0);
        }
        if (i % 2 == 0) {
            int col = cell_col(g, k->px[i]), row = cell_row(g, k->py[i]);
            put_cell(g, col, row, '*', palette_ramp(f->palette, 1.0f));
            put_cell(g, col - 1, row, '{', bloom);
            put_cell(g, col + 1, row, '}', bloom);
        }
    }
    // A small ring now and then where a fish touches the surface.
    for (int j = 0; j < 2; j++) {
        float slot = mfloor(f->t / 2.3f) - (float)j, age = f->t - slot * 2.3f;
        int i = (int)(hash01((uint32_t)slot * 2654435761u ^ k->salt) * KOI);
        float x, y;
        koi_at(k, i, slot * 2.3f, &x, &y);
        float radius = 0.03f + age * 0.09f, fade = 1 - age / 2.6f;
        if (fade <= 0) continue;
        StrokePoint ring[32];
        for (int p = 0; p < 32; p++) {
            float a = (float)p / 32 * TAU_F;
            ring[p] = (StrokePoint){x + mcos(a) * radius, y + msin(a) * radius, frame_dim(f, palette_ramp(f->palette, 0.75f), fade * 0.7f)};
        }
        stroke_draw(f->stroker, ring, 32, 1);
    }
    f->fx->glow = 0.4f;
}

/* ---------- Train window ---------- */

typedef struct {
    float speed, moon_x;
    uint32_t salt;
} Train;

void train_setup(void *state, Rng *r, const Frame *f) {
    Train *t = state;
    t->speed = rng_between(r, 0.9f, 1.4f);
    t->moon_x = rng_between(r, -0.5f, 0.5f) * f->grid->aspect;
    t->salt = rng_u32(r);
}

// Looking out of a night train: far hills sliding by slowly, a town's lights on the nearer ones, telephone poles
// flicking past with their wires swooping between, all framed by the window.
void train_draw(void *state, const Frame *f) {
    Train *t = state;
    Grid *g = f->grid;
    dust_stars(f, t->salt, 0.0f, 0.7f);
    moon_disc(f, t->moon_x - f->t * 0.004f, 0.55f, 0.12f, palette_ramp(f->palette, 0.95f), t->salt, 0);
    float run = f->t * t->speed;
    Rgbf far = palette_ramp(f->palette, 0.3f), near = palette_ramp(f->palette, 0.45f), lamp = palette_accent(f->palette, 1);
    for (int col = 0; col < g->cols; col++) {
        float x = cell_x(g, col);
        float h1 = -0.05f + 0.22f * noise_fbm(x * 0.5f + run * 0.03f + (float)(t->salt % 37), 1);
        float h2 = -0.3f + 0.2f * noise_fbm(x * 0.9f + run * 0.12f + (float)(t->salt % 53), 4);
        put_cell(g, col, cell_row(g, h1), '_', far);
        for (int row = cell_row(g, h2); row < g->rows; row++) put_cell(g, col, row, row == cell_row(g, h2) ? '-' : ' ', near);
        // Windows of a town spread over the nearer hills.
        for (int row = cell_row(g, h2) + 1; row < cell_row(g, h2) + 6 && row < g->rows; row++) {
            uint32_t id = (uint32_t)(row * 977) + (uint32_t)((x + run * 0.12f) * 30.0f) * 2654435761u;
            if (hash01(id ^ t->salt) < 0.06f) put_cell(g, col, row, '.', frame_dim(f, lamp, 0.6f + 0.4f * hash01(id + 1)));
        }
    }
    // Poles flick past; between each pair a wire sags, and the sag is what seems to swoop.
    float spacing = 0.9f, pole_run = run * 1.1f;
    float first = -g->aspect - mfract(pole_run / spacing) * spacing;
    Rgbf pole = palette_ramp(f->palette, 0.65f);
    for (int p = 0; p < 6; p++) {
        float x = first + (float)p * spacing;
        line(f, x, -1.0f, x, 0.62f, pole);
        line(f, x - 0.06f, 0.55f, x + 0.06f, 0.55f, pole);
        StrokePoint wire[16];
        for (int s = 0; s < 16; s++) {
            float u = (float)s / 15;
            wire[s] = (StrokePoint){x + u * spacing, 0.55f - 0.12f * 4 * u * (1 - u), frame_dim(f, pole, 0.8f)};
        }
        stroke_draw(f->stroker, wire, 16, 0);
    }
    // The window: a thick rounded frame, and the dark carriage wall around it.
    Rgbf wall = palette_ramp(f->palette, 0.2f), frame = palette_ramp(f->palette, 0.5f);
    int l = 3, r = g->cols - 4, top = 2, bottom = g->rows - 3;
    for (int row = 0; row < g->rows; row++)
        for (int col = 0; col < g->cols; col++) {
            int inside = col > l && col < r && row > top && row < bottom;
            int corner = (col <= l + 1 || col >= r - 1) && (row <= top + 1 || row >= bottom - 1);
            if (inside && !corner) continue;
            int rim = (col == l || col == r) && row >= top && row <= bottom ? 1 : (row == top || row == bottom) && col >= l && col <= r;
            put_cell(g, col, row, rim ? (col == l || col == r ? '|' : '=') : ' ', rim ? frame : wall);
        }
    f->fx->glow = 0.45f;
}

/* ---------- Campfire ---------- */

typedef struct {
    float tent_x, seed;
    uint32_t salt;
} Campfire;

void campfire_setup(void *state, Rng *r, const Frame *f) {
    Campfire *c = state;
    c->tent_x = (rng_chance(r, 50) ? -1 : 1) * rng_between(r, 0.6f, 0.85f) * f->grid->aspect;
    c->seed = rng_between(r, 0, 60);
    c->salt = rng_u32(r);
}

// A campfire under the stars: flames licking up from crossed logs in a ring of stones, sparks rising, a tent glowing
// at the edge of the light, and pines dark against the sky.
void campfire_draw(void *state, const Frame *f) {
    Campfire *c = state;
    Grid *g = f->grid;
    dust_stars(f, c->salt, 0.0f, 0.8f);
    float ground = -0.55f;
    Rgbf earth = palette_ramp(f->palette, 0.25f), warm = palette_accent(f->palette, 1), hot = palette_ramp(f->palette, 1.0f);
    for (int col = 0; col < g->cols; col++)
        for (int row = cell_row(g, ground); row < g->rows; row++) {
            // The ground catches the firelight near the middle.
            float x = cell_x(g, col), y = cell_y(g, row), lit = mexp(-(x * x * 2 + (y - ground) * (y - ground) * 8));
            int glyph = row == cell_row(g, ground) ? '_' : hash01((uint32_t)(row * 131 + col) ^ c->salt) < 0.15f ? '.' : ' ';
            put_cell(g, col, row, glyph, rgbf_mix(earth, warm, lit * 0.7f));
        }
    // Pines along the back.
    for (int k = 0; k < 9; k++) {
        float x = ((float)k / 8 * 2 - 1) * g->aspect * 0.95f + (hash01((uint32_t)k ^ c->salt) - 0.5f) * 0.2f;
        if (mabs(x) < 0.45f) continue;
        int top = cell_row(g, ground + 0.35f + 0.25f * hash01((uint32_t)k * 31u)), base = cell_row(g, ground);
        for (int row = top; row < base; row++) {
            int w = (row - top) / 2;
            for (int d = -w; d <= w; d++) put_cell(g, cell_col(g, x) + d, row, d < 0 ? '/' : d > 0 ? '\\' : '^', frame_dim(f, earth, 1.4f));
        }
    }
    // The tent, lit on its fire side.
    float tx = c->tent_x, tw = 0.32f, th = 0.4f;
    line(f, tx - tw, ground, tx, ground + th, rgbf_mix(earth, warm, 0.6f));
    line(f, tx, ground + th, tx + tw, ground, rgbf_mix(earth, warm, 0.6f));
    line(f, tx, ground + th, tx, ground, frame_dim(f, warm, 0.6f));
    // The flames: noise rising through a cone above the logs.
    for (int row = cell_row(g, ground + 0.45f); row <= cell_row(g, ground); row++)
        for (int col = cell_col(g, -0.2f); col <= cell_col(g, 0.2f); col++) {
            float x = cell_x(g, col), h = (cell_y(g, row) - ground) / 0.45f;
            float n = noise_fbm(x * 9 + c->seed, h * 3 - f->t * 1.6f);
            float heat = saturate((n - 0.25f) * 2.5f - h * 1.1f - mabs(x) * 4.0f + 0.4f);
            if (heat < 0.08f) continue;
            static const char ramp[] = ".,:;=+*#";
            put_cell(g, col, row, ramp[clampi((int)(heat * 7.99f), 0, 7)], rgbf_mix(warm, hot, heat * heat));
        }
    // Crossed logs and a ring of stones.
    line(f, -0.2f, ground - 0.02f, 0.12f, ground + 0.06f, frame_dim(f, earth, 1.8f));
    line(f, 0.2f, ground - 0.02f, -0.12f, ground + 0.06f, frame_dim(f, earth, 1.8f));
    for (int s = 0; s < 9; s++) {
        float a = (float)s / 9 * PI_F;
        put_cell(g, cell_col(g, mcos(a) * 0.3f), cell_row(g, ground - msin(a) * 0.04f), s % 2 ? 'o' : 'O', frame_dim(f, earth, 2.0f));
    }
    for (int k = 0; k < 14; k++) {
        uint32_t id = c->salt + (uint32_t)k * 2246822519u;
        float life = 1.5f + hash01(id), age = mfract(f->t / life + hash01(id + 1)) * life;
        float x = (hash01(id + 2) - 0.5f) * 0.2f + 0.06f * msin(age * 4 + (float)k), y = ground + 0.1f + age * 0.35f;
        put_cell(g, cell_col(g, x), cell_row(g, y), age < 0.6f ? '*' : '.', frame_dim(f, warm, 1 - age / life));
    }
    f->fx->glow = 0.7f;
}

/* ---------- Hourglass ---------- */

typedef struct {
    uint32_t salt;
} Hourglass;

void hourglass_setup(void *state, Rng *r, const Frame *f) {
    Hourglass *h = state;
    h->salt = rng_u32(r);
    (void)f;
}

// The glass's half-width at height y, from the neck at the middle out to the bulbs.
static float glass_width(float y) {
    float u = mabs(y) / 0.72f;
    return 0.03f + 0.32f * msin(saturate(u) * PI_F * 0.5f) * (u < 0.9f ? 1.0f : 1 - (u - 0.9f) * 3);
}

// An hourglass timing the whole piece: the top bulb empties as the song plays, a thin stream runs through the neck,
// and the pile below grows into a cone.
void hourglass_draw(void *state, const Frame *f) {
    Hourglass *h = state;
    Grid *g = f->grid;
    float progress = saturate(f->t / mmax(f->score->length, 1));
    Rgbf wood = palette_accent(f->palette, 1), glass = palette_ramp(f->palette, 0.6f), sand = palette_ramp(f->palette, 0.9f);
    for (int k = 0; k < 30; k++) {
        uint32_t id = h->salt + (uint32_t)k * 2654435761u;
        float x = (hash01(id) * 2 - 1) * g->aspect, y = mfract(hash01(id + 1) + f->t * 0.01f) * 2 - 1;
        put_cell(g, cell_col(g, x), cell_row(g, y), '.', frame_dim(f, glass, 0.4f));
    }
    // The sand: the top bulb holds what's left, filling from its floor (the neck) upward.
    float top_level = 0.72f * (1 - msqrt(progress)), pile = 0.72f * msqrt(progress);
    for (int row = cell_row(g, 0.74f); row <= cell_row(g, -0.74f); row++) {
        float y = cell_y(g, row), w = glass_width(y);
        for (int col = cell_col(g, -w); col <= cell_col(g, w); col++) {
            float x = cell_x(g, col);
            int full = 0;
            if (y > 0.02f) full = y < 0.02f + top_level;
            else if (y < -0.02f) full = -0.74f + pile * (1 - mabs(x) / (w + 0.001f)) > y;
            if (full) put_cell(g, col, row, (row + col) % 4 ? ':' : '.', sand);
        }
        // The glass walls.
        put_cell(g, cell_col(g, -w) - 1, row, y > 0 ? '\\' : '/', glass);
        put_cell(g, cell_col(g, w) + 1, row, y > 0 ? '/' : '\\', glass);
    }
    if (progress < 0.999f)
        for (int row = cell_row(g, 0.0f); row <= cell_row(g, -0.74f + pile); row++)
            put_cell(g, cell_col(g, 0), row, mfract(f->t * 6 + (float)row * 0.37f) < 0.5f ? '|' : ':', sand);
    // Plates top and bottom, and the posts.
    for (int s = 0; s < 2; s++) {
        int row = cell_row(g, s ? -0.8f : 0.8f);
        for (int col = cell_col(g, -0.48f); col <= cell_col(g, 0.48f); col++) put_cell(g, col, row, '=', wood);
    }
    for (int s = 0; s < 2; s++)
        for (int row = cell_row(g, 0.8f) + 1; row < cell_row(g, -0.8f); row++) put_cell(g, cell_col(g, s ? 0.44f : -0.44f), row, '|', frame_dim(f, wood, 0.8f));
    f->fx->glow = 0.45f;
}

/* ---------- Birds ---------- */

typedef struct {
    float sun_x, horizon;
    uint32_t salt;
} Birds;

void birds_setup(void *state, Rng *r, const Frame *f) {
    Birds *b = state;
    b->sun_x = rng_between(r, -0.5f, 0.5f) * f->grid->aspect;
    b->horizon = rng_between(r, -0.45f, -0.3f);
    b->salt = rng_u32(r);
}

// Birds flying home at dusk in long V's, wings beating each in its own time, past a low sun and streaks of cloud,
// over water that holds the sun's reflection.
void birds_draw(void *state, const Frame *f) {
    Birds *b = state;
    Grid *g = f->grid;
    Rgbf sun = palette_accent(f->palette, 1), cloud = palette_ramp(f->palette, 0.5f), sea = palette_ramp(f->palette, 0.3f);
    for (int row = 0; row < cell_row(g, b->horizon); row++)
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), y = cell_y(g, row);
            float n = noise_fbm(x * 0.6f - f->t * 0.01f + (float)(b->salt % 47), y * 6);
            if (n > 0.6f) put_cell(g, col, row, '-', rgbf_mix(cloud, sun, mexp(-mabs(x - b->sun_x) * 1.5f) * 0.6f));
        }
    moon_disc(f, b->sun_x, b->horizon + 0.12f, 0.17f, sun, b->salt, 0);
    for (int col = 0; col < g->cols; col++) put_cell(g, col, cell_row(g, b->horizon), '_', sea);
    for (int row = cell_row(g, b->horizon) + 1; row < g->rows; row++) {
        float y = cell_y(g, row), near = (b->horizon - y) / (b->horizon + 1);
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), w = msin(x * 8 * (1 + near) + f->t * 0.8f + y * 30);
            float path = mabs(x - b->sun_x) / (0.05f + near * 0.3f);
            if (path < 1 && w > 0) put_cell(g, col, row, w > 0.6f ? '=' : '-', frame_dim(f, sun, 0.4f + 0.5f * (1 - path)));
            else if (w > 0.85f) put_cell(g, col, row, '~', frame_dim(f, sea, 0.8f));
        }
    }
    // Three flocks, each a V crossing at its own height and speed.
    Rgbf dark = palette_ramp(f->palette, 0.85f);
    for (int k = 0; k < 3; k++) {
        uint32_t id = b->salt + (uint32_t)k * 7919u;
        float speed = 0.06f + 0.05f * hash01(id), span = g->aspect * 2.8f;
        float lead_x = mfract(hash01(id + 1) + f->t * speed / span) * span - span * 0.5f, lead_y = 0.15f + 0.6f * hash01(id + 2);
        int count = 5 + (int)(hash01(id + 3) * 6);
        for (int i = 0; i < count; i++) {
            int side = i % 2 ? 1 : -1, rank = (i + 1) / 2;
            float x = lead_x - (float)rank * 0.07f, y = lead_y + (float)(side * rank) * 0.045f;
            int up = mfract(f->t * 1.4f + (float)i * 0.23f + (float)k) < 0.5f;
            put_cell(g, cell_col(g, x), cell_row(g, y), up ? 'v' : '^', dark);
        }
    }
    f->fx->glow = 0.5f;
}

/* ---------- Ringed planet ---------- */

typedef struct {
    float cx, cy, radius, tilt, light;
    uint32_t salt;
} Planet;

void planet_setup(void *state, Rng *r, const Frame *f) {
    Planet *p = state;
    p->cx = rng_between(r, -0.35f, 0.1f) * f->grid->aspect;
    p->cy = rng_between(r, -0.15f, 0.1f);
    p->radius = rng_between(r, 0.42f, 0.55f);
    p->tilt = rng_between(r, 0.2f, 0.32f);
    p->light = rng_between(r, -0.8f, 0.8f);
    p->salt = rng_u32(r);
}

// The rings as fine ellipses: the far half only where the planet doesn't cover it, the near half across its face.
static void rings(const Frame *f, const Planet *p, int front) {
    Grid *g = f->grid;
    static const float radii[5] = {1.32f, 1.45f, 1.55f, 1.78f, 1.9f};
    for (int band = 0; band < 5; band++) {
        Rgbf color = frame_dim(f, band == 3 ? palette_accent(f->palette, 1) : palette_ramp(f->palette, 0.8f), band == 0 ? 0.6f : 1.0f);
        float rr = p->radius * radii[band];
        for (int k = 0; k < 200; k++) {
            float a = (float)k / 200 * TAU_F + f->t * 0.01f, s = msin(a);
            if ((s < 0) != front) continue;
            float x = p->cx + mcos(a) * rr, y = p->cy + s * rr * p->tilt;
            // Where the ring runs steeply on screen it draws as an upright stroke, elsewhere as a flat one.
            int glyph = mabs(mcos(a)) > 0.93f ? (mcos(a) > 0 ? ')' : '(') : s < 0 ? '_' : '-';
            (front ? put_cell : put_cell_under)(g, cell_col(g, x), cell_row(g, y), glyph, color);
        }
    }
}

// A ringed planet hanging in space: lit from one side with banded clouds, its rings passing behind it and across its
// face, and two moons on their slow rounds.
void planet_draw(void *state, const Frame *f) {
    Planet *p = state;
    Canvas *c = f->canvas;
    canvas_clear(c);
    Rgbf a = palette_accent(f->palette, 1), b = palette_ramp(f->palette, 0.6f);
    float lx = mcos(p->light), ly = 0.45f, lz = 0.6f, ln = msqrt(lx * lx + ly * ly + lz * lz);
    for (int j = 0; j < c->h; j++) {
        float y = canvas_y(c, j) - p->cy;
        if (mabs(y) > p->radius) continue;
        for (int i = 0; i < c->w; i++) {
            float x = canvas_x(c, i) - p->cx, d2 = (x * x + y * y) / (p->radius * p->radius);
            if (d2 > 1) continue;
            float nz = msqrt(1 - d2), nx = x / p->radius, ny = y / p->radius;
            float lit = saturate((nx * lx + ny * ly + nz * lz) / ln);
            float bands = 0.5f + 0.5f * msin(ny * 14 + noise_value(nx * 3 + (float)(p->salt % 31), ny * 8) * 2);
            Rgbf color = rgbf_mix(a, b, bands);
            canvas_max(c, i, j, 0.12f + 0.88f * lit, color);
        }
    }
    // Two moons; the far half of each orbit passes behind the planet.
    for (int m = 0; m < 2; m++) {
        float ang = f->t * (0.12f + 0.08f * (float)m) + (float)m * 2.5f, orbit = p->radius * (2.3f + 0.5f * (float)m);
        float x = p->cx + mcos(ang) * orbit, y = p->cy + msin(ang) * orbit * p->tilt * 1.4f;
        int behind = msin(ang) > 0 && mhypot(x - p->cx, y - p->cy) < p->radius;
        if (!behind) canvas_disc(c, x, y, 0.035f + 0.015f * (float)m, 0.9f, palette_ramp(f->palette, 0.95f));
    }
    frame_resolve(f);
    rings(f, p, 0);
    rings(f, p, 1);
    dust_stars(f, p->salt, 0.0f, 0.8f);
    f->fx->glow = 0.55f;
}

/* ---------- Rainy street ---------- */

typedef struct {
    float speed, horizon;
    uint32_t salt;
} Street;

void street_setup(void *state, Rng *r, const Frame *f) {
    Street *s = state;
    s->speed = rng_between(r, 0.25f, 0.45f);
    s->horizon = rng_between(r, 0.05f, 0.2f);
    s->salt = rng_u32(r);
    (void)f;
}

// A rainy street at night, walked slowly: lamps down both sides throwing pools of light on the wet road, rain showing
// only where it crosses the light, and the lamps' reflections shivering in the puddles.
void street_draw(void *state, const Frame *f) {
    Street *s = state;
    Grid *g = f->grid;
    float hz = s->horizon, walk = f->t * s->speed;
    Rgbf lamp = palette_accent(f->palette, 1), road = palette_ramp(f->palette, 0.3f), house = palette_ramp(f->palette, 0.2f);
    // Houses far down the street, a few windows lit.
    for (int col = 0; col < g->cols; col++) {
        float x = cell_x(g, col), h = 0.06f + 0.12f * noise_value(x * 6 + (float)(s->salt % 41), 2);
        for (int row = cell_row(g, hz + h); row <= cell_row(g, hz); row++) {
            int window = hash01((uint32_t)(row * 977 + col * 31) ^ s->salt) < 0.1f;
            put_cell(g, col, row, window ? '.' : ' ', window ? frame_dim(f, lamp, 0.7f) : house);
        }
    }
    line(f, -0.05f, hz, -g->aspect * 1.6f, -1.1f, road);
    line(f, 0.05f, hz, g->aspect * 1.6f, -1.1f, road);
    for (int side = -1; side <= 1; side += 2)
        for (int k = 0; k < 7; k++) {
            float z = 1.2f + (float)k * 1.6f - mfract(walk / 1.6f) * 1.6f, x = (float)side * 1.1f / z;
            float base = hz - 0.9f / z, top = base + 1.25f / z, near = saturate(1.6f / z);
            Rgbf post = frame_dim(f, palette_ramp(f->palette, 0.6f), 0.4f + 0.6f * near);
            line(f, x, base, x, top, post);
            line(f, x, top, x - (float)side * 0.12f / z, top + 0.03f / z, post);
            float hx = x - (float)side * 0.12f / z;
            put_cell(g, cell_col(g, hx), cell_row(g, top), 'O', frame_dim(f, lamp, 0.6f + 0.6f * near));
            // The pool of light on the ground, and rain falling through the light's cone.
            float pool = 0.35f / z;
            for (int d = 0; d < 26; d++) {
                uint32_t id = (uint32_t)(k * 131 + d + (side > 0 ? 7 : 0)) ^ s->salt;
                float a = hash01(id) * TAU_F, rr = msqrt(hash01(id + 1));
                put_cell_under(g, cell_col(g, hx + mcos(a) * rr * pool * 1.5f), cell_row(g, base + msin(a) * rr * pool * 0.3f), '.',
                               frame_dim(f, lamp, 0.25f + 0.4f * near * (1 - rr)));
            }
            for (int d = 0; d < 10; d++) {
                uint32_t id = (uint32_t)(k * 977 + d * 31 + (side > 0 ? 5 : 0)) ^ s->salt;
                float rx = hx + (hash01(id) - 0.5f) * pool * 1.6f, ry = top - mfract(hash01(id + 1) + f->t * 1.6f) * (top - base);
                put_cell(g, cell_col(g, rx), cell_row(g, ry), '/', frame_dim(f, lamp, 0.35f + 0.5f * near));
            }
            // Its reflection, broken by the ripples on the wet road.
            int rcol = cell_col(g, hx);
            for (int row = cell_row(g, base) + 1; row <= cell_row(g, base) + (int)(8 * near) && row < g->rows; row++) {
                int wob = (int)(msin(f->t * 3 + (float)row * 1.7f + (float)k) * 1.2f);
                put_cell(g, rcol + wob, row, (row & 1) ? '~' : '-', frame_dim(f, lamp, 0.45f * near));
            }
        }
    f->fx->glow = 0.65f;
}

/* ---------- Palms ---------- */

#define PALMS 3

typedef struct {
    float horizon, sun_r, lean[PALMS], height[PALMS], x[PALMS];
    int count;
    uint32_t salt;
} Palms;

void palms_setup(void *state, Rng *r, const Frame *f) {
    Palms *p = state;
    p->horizon = rng_between(r, -0.3f, -0.15f);
    p->sun_r = rng_between(r, 0.3f, 0.42f);
    p->count = rng_range(r, 2, PALMS + 1);
    for (int i = 0; i < p->count; i++) {
        int right = i % 2;
        p->x[i] = (right ? 1 : -1) * f->grid->aspect * rng_between(r, 0.72f, 0.98f);
        p->lean[i] = (right ? -1 : 1) * rng_between(r, 0.1f, 0.25f);
        p->height[i] = rng_between(r, 1.0f, 1.4f);
    }
    // Shorter palms stand farther back and are drawn first.
    for (int i = 0; i < p->count; i++)
        for (int k = i + 1; k < p->count; k++)
            if (p->height[k] < p->height[i]) {
                float swap = p->height[i];
                p->height[i] = p->height[k];
                p->height[k] = swap;
            }
    p->salt = rng_u32(r);
}

// Clears each cell along a curve and its four neighbors, so a line drawn there afterwards stands clear of the glow.
static void carve(Grid *g, const StrokePoint *pts, int n) {
    static const int AROUND[5][2] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (int k = 0; k + 1 < n; k++)
        for (int s = 0; s <= 6; s++) {
            float t = (float)s / 6;
            int col = cell_col(g, mixf(pts[k].x, pts[k + 1].x, t)), row = cell_row(g, mixf(pts[k].y, pts[k + 1].y, t));
            for (int d = 0; d < 5; d++) {
                int c = col + AROUND[d][0], r = row + AROUND[d][1];
                if (c >= 0 && c < g->cols && r >= 0 && r < g->rows) grid_at(g, c, r)->glyph = ' ';
            }
        }
}

static void trace(const Frame *f, const StrokePoint *pts, int n, int carving) {
    if (carving) carve(f->grid, pts, n);
    else stroke_draw(f->stroker, pts, n, 0);
}

// One palm in neon line art: a tapering ringed trunk, fronds arching from the crown and drooping in the breeze with
// leaflets hanging from them, and a few coconuts. Called twice: once to carve its shape, once to draw it.
static void palm(const Frame *f, const Palms *p, int i, int carving, Rgbf neon) {
    static const float ANGLE[4] = {-0.26f, 0.26f, 0.79f, 1.31f}, REACH[4] = {0.28f, 0.36f, 0.34f, 0.26f};
    static const float DROOP[4] = {0.16f, 0.22f, 0.26f, 0.22f};
    Grid *g = f->grid;
    StrokePoint left[16], right[16];
    float base = -1.05f, h = p->height[i], lean = p->lean[i] * 1.4f;
    for (int s = 0; s < 16; s++) {
        float t = (float)s / 15, x = p->x[i] + lean * t * t, y = base + t * h;
        float tx = 2 * lean * t, len = mhypot(tx, h), half = mixf(0.045f, 0.02f, t);
        left[s] = (StrokePoint){x - h / len * half, y + tx / len * half, neon};
        right[s] = (StrokePoint){x + h / len * half, y - tx / len * half, neon};
    }
    trace(f, left, 16, carving);
    trace(f, right, 16, carving);
    float cx = p->x[i] + lean, cy = base + h;
    if (!carving)
        for (int s = 1; s < 10; s += 2) {
            float t = (float)s / 15;
            put_cell(g, cell_col(g, p->x[i] + lean * t * t), cell_row(g, base + t * h), '=', frame_dim(f, neon, 0.55f));
        }
    // Nearer (taller) palms have bigger crowns.
    float breeze = 0.04f * msin(f->t * 0.45f + (float)i), crown = 0.85f + 0.35f * saturate((h - 1) / 0.4f);
    for (int k = 0; k < 8; k++) {
        int j = k % 4;
        float side = k < 4 ? 1.0f : -1.0f, sway = 0.035f * msin(f->t * 0.8f + (float)i * 1.7f + (float)k * 0.6f);
        float dx = mcos(ANGLE[j]) * side * REACH[j] * crown, dy = msin(ANGLE[j]) * REACH[j] * crown, droop = (DROOP[j] + sway) * crown;
        StrokePoint frond[12];
        for (int s = 0; s < 12; s++) {
            float u = (float)s / 11;
            frond[s] = (StrokePoint){cx + dx * u + breeze * u * u, cy + dy * u - droop * u * u, neon};
        }
        trace(f, frond, 12, carving);
        for (int s = 3; s < 11; s += 2) {
            float drop = 0.05f * crown * (1 - 0.4f * (float)s / 11);
            StrokePoint leaf[2] = {frond[s], {frond[s].x + side * 0.37f * drop, frond[s].y - 0.93f * drop, frame_dim(f, neon, 0.8f)}};
            trace(f, leaf, 2, carving);
        }
    }
    static const float NUT[3][2] = {{-0.035f, -0.05f}, {0.025f, -0.06f}, {-0.005f, -0.09f}};
    for (int n = 0; n < 3; n++) {
        int col = cell_col(g, cx + NUT[n][0]), row = cell_row(g, cy + NUT[n][1]);
        if (carving && col >= 0 && col < g->cols && row >= 0 && row < g->rows) grid_at(g, col, row)->glyph = ' ';
        if (!carving) put_cell(g, col, row, 'o', frame_dim(f, neon, 0.8f));
    }
}

// Dusk on a beach: the sky glowing hotter toward a striped sun settling into the sea, framed by neon palms whose fronds
// stir in the breeze; small waves wash in.
void palms_draw(void *state, const Frame *f) {
    Palms *p = state;
    Grid *g = f->grid;
    float hz = p->horizon, sun_y = hz + p->sun_r * 0.35f;
    static const char glow[] = " .:-=+";
    Rgbf high = palette_ramp(f->palette, 0.35f), low = palette_accent(f->palette, 1), hot = palette_ramp(f->palette, 1.0f);
    dust_stars(f, p->salt, 0.0f, 0.6f);
    for (int row = 0; row <= cell_row(g, hz); row++) {
        float y = cell_y(g, row), up = saturate((y - hz) / (1 - hz));
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), near_sun = mexp(-mhypot(x * 0.6f, y - sun_y) * 1.8f);
            float level = saturate((1 - up) * (1 - up) * 0.75f + near_sun * 0.45f);
            int i = (int)(level * 5.99f);
            if (i <= 0) continue;
            Rgbf color = rgbf_mix(rgbf_mix(high, low, 1 - up), hot, near_sun * 0.5f);
            put_cell(g, col, row, glow[i], frame_dim(f, color, 0.3f + 0.6f * level));
        }
    }
    Rgbf top = palette_ramp(f->palette, 1.0f), bottom = palette_accent(f->palette, 1);
    for (int row = cell_row(g, sun_y + p->sun_r); row <= cell_row(g, hz); row++) {
        float y = cell_y(g, row), v = (sun_y + p->sun_r - y) / (2 * p->sun_r);
        // Bands that thin out toward the horizon, sliding slowly down; the gaps between them show the sky.
        float band = mfract(v * 7 - f->t * 0.08f), gap = v > 0.45f ? (v - 0.45f) * 1.2f : 0;
        if (band < gap) continue;
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), d = mhypot(x, y - sun_y);
            if (d < p->sun_r) put_cell(g, col, row, '#', rgbf_mix(top, bottom, saturate(v * 1.2f)));
        }
    }
    // The sea, with the sun's reflection broken into dashes, and a strip of beach.
    int beach = cell_row(g, -0.78f);
    for (int row = cell_row(g, hz) + 1; row < g->rows; row++) {
        float y = cell_y(g, row), near = (hz - y) / (hz + 1);
        for (int col = 0; col < g->cols; col++) {
            if (row >= beach) {
                if ((row * 31 + col * 7) % 5 == 0) put_cell(g, col, row, '.', frame_dim(f, low, 0.4f));
                continue;
            }
            float x = cell_x(g, col), w = msin(x * 6 * (1 + near * 2) + f->t * 0.9f + y * 25);
            if (mabs(x) < p->sun_r * (0.6f + near) && w > 0.2f)
                put_cell(g, col, row, w > 0.7f ? '=' : '-', frame_dim(f, bottom, 0.5f + 0.4f * (1 - near)));
            else if (w > 0.35f)
                put_cell(g, col, row, w > 0.8f ? '~' : '-', frame_dim(f, rgbf_mix(low, high, near), 0.3f + 0.25f * (1 - near)));
        }
    }
    for (int col = 0; col < g->cols; col++) put_cell(g, col, cell_row(g, hz), '_', palette_ramp(f->palette, 0.6f));
    for (int i = 0; i < p->count; i++) {
        Rgbf neon = frame_dim(f, palette_accent(f->palette, 0), 0.6f + 0.4f * saturate((p->height[i] - 1) / 0.4f));
        palm(f, p, i, 1, neon);
        palm(f, p, i, 0, neon);
    }
    f->fx->glow = 0.6f;
}
