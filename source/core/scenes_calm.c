// SPDX-License-Identifier: GPL-3.0-only
// Still scenes: they move only with time, never with the music, so a voyage has places to breathe.
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

/* ---------- Petals ---------- */

typedef struct {
    float wind, branch_y, moon_x, moon_y;
    int left;
    uint32_t salt;
} Petals;

void petals_setup(void *state, Rng *r, const Frame *f) {
    Petals *p = state;
    p->wind = rng_between(r, 0.06f, 0.16f);
    p->left = rng_chance(r, 50);
    p->branch_y = rng_between(r, 0.45f, 0.75f);
    p->moon_x = (p->left ? 1 : -1) * rng_between(r, 0.3f, 0.7f) * f->grid->aspect;
    p->moon_y = rng_between(r, 0.2f, 0.6f);
    p->salt = rng_u32(r);
}

static float branch_at(const Petals *p, float x, float aspect) {
    float from = p->left ? -aspect : aspect, along = mabs(x - from) / aspect;
    return p->branch_y + 0.12f * msin(along * 3.1f) - along * 0.18f;
}

// A blossoming branch reaching in from one side, petals drifting down across a soft moon.
void petals_draw(void *state, const Frame *f) {
    Petals *p = state;
    Grid *g = f->grid;
    dust_stars(f, p->salt, 0.0f, 0.5f);
    moon_disc(f, p->moon_x, p->moon_y, 0.2f, palette_ramp(f->palette, 0.9f), p->salt, 0);
    Rgbf bark = palette_ramp(f->palette, 0.35f), bloom = palette_accent(f->palette, 0), pale = palette_ramp(f->palette, 0.95f);
    // The branch and its twigs.
    StrokePoint pts[40];
    float from = p->left ? -g->aspect : g->aspect, to = from + (p->left ? 1 : -1) * g->aspect * 1.25f;
    for (int i = 0; i < 40; i++) {
        float x = mixf(from, to, (float)i / 39.0f);
        pts[i] = (StrokePoint){x, branch_at(p, x, g->aspect), bark};
    }
    stroke_draw(f->stroker, pts, 40, 0);
    for (int t = 0; t < 5; t++) {
        float x = mixf(from, to, 0.15f + 0.17f * (float)t), y = branch_at(p, x, g->aspect), dir = (t & 1) ? 1.0f : -1.0f;
        StrokePoint twig[2] = {{x, y, bark}, {x + (p->left ? 0.12f : -0.12f), y + dir * 0.16f, bark}};
        stroke_draw(f->stroker, twig, 2, 0);
        // Blossoms clustered at the twig's end.
        for (int b = 0; b < 7; b++) {
            uint32_t id = p->salt + (uint32_t)(t * 31 + b);
            float bx = twig[1].x + (hash01(id) - 0.5f) * 0.14f, by = twig[1].y + (hash01(id + 1) - 0.5f) * 0.12f;
            put_cell(g, cell_col(g, bx), cell_row(g, by), b % 3 ? '*' : 'o', b % 2 ? bloom : pale);
        }
    }
    // Petals falling: each turns over as it goes, showing a different face.
    static const char faces[] = "`.,'~-";
    int count = g->cols * g->rows / 28;
    for (int i = 0; i < count; i++) {
        uint32_t id = p->salt + (uint32_t)i * 2654435761u;
        float speed = 0.06f + 0.06f * hash01(id + 2), life = 2.2f / speed;
        float age = mfract(hash01(id) + f->t / life) * life;
        float x = from + (to - from) * hash01(id + 1) + age * p->wind * (p->left ? 1 : -1) * 0.5f + 0.05f * msin(age * 1.7f + hash01(id + 3) * 6);
        float y = branch_at(p, x, g->aspect) - age * speed;
        if (y < -1.05f) continue;
        int face = (int)(age * 3 + hash01(id + 4) * 6) % 6;
        put_cell_under(g, cell_col(g, x), cell_row(g, y), faces[face], rgbf_mix(bloom, pale, hash01(id + 5) * 0.6f));
    }
    f->fx->glow = 0.45f;
}

/* ---------- Lava ---------- */

typedef struct {
    int count;
    float ax[7], ay[7], fx[7], fy[7], size[7];
} Lava;

void lava_setup(void *state, Rng *r, const Frame *f) {
    Lava *l = state;
    l->count = rng_range(r, 5, 8);
    for (int i = 0; i < l->count; i++) {
        l->ax[i] = rng_between(r, 0.3f, 0.9f) * f->grid->aspect;
        l->ay[i] = rng_between(r, 0.5f, 0.85f);
        l->fx[i] = rng_between(r, 0.05f, 0.14f);
        l->fy[i] = rng_between(r, 0.04f, 0.1f);
        l->size[i] = rng_between(r, 0.18f, 0.32f);
    }
}

typedef struct {
    const Lava *lava;
    float bx[7], by[7];
    Rgbf top, base;
} LavaFrame;

static void lava_corner(const void *ctx, const Frame *f, float x, float y, float *ink, Rgbf *color) {
    const LavaFrame *lf = ctx;
    float field = 0;
    for (int k = 0; k < lf->lava->count; k++) {
        float dx = x - lf->bx[k], dy = y - lf->by[k];
        field += lf->lava->size[k] * lf->lava->size[k] / (dx * dx + dy * dy + 0.0004f);
    }
    *ink = field > 0.55f ? smoothstep(0.55f, 1.6f, field) : 0;
    *color = rgbf_mix(lf->base, lf->top, saturate(y * 0.5f + 0.5f));
    (void)f;
}

// Slow blobs of warm wax rising and sinking, merging and parting, shaded from their cool tops to their glowing bases.
void lava_draw(void *state, const Frame *f) {
    const Lava *l = state;
    LavaFrame lf = {.lava = l, .top = palette_ramp(f->palette, 0.95f), .base = palette_accent(f->palette, 0)};
    for (int i = 0; i < l->count; i++) {
        lf.bx[i] = l->ax[i] * msin(f->t * l->fx[i] * TAU_F + (float)i * 1.3f);
        lf.by[i] = l->ay[i] * msin(f->t * l->fy[i] * TAU_F + (float)i * 2.1f);
    }
    canvas_clear(f->canvas);
    canvas_from_corners(f, lava_corner, &lf);
    frame_resolve(f);
    f->fx->glow = 0.55f;
}

/* ---------- Clouds ---------- */

typedef struct {
    float moon_x, moon_y, speed;
    uint32_t salt;
} Clouds;

void clouds_setup(void *state, Rng *r, const Frame *f) {
    Clouds *c = state;
    c->moon_x = rng_between(r, -0.6f, 0.6f) * f->grid->aspect;
    c->moon_y = rng_between(r, 0.15f, 0.55f);
    c->speed = rng_between(r, 0.02f, 0.045f);
    c->salt = rng_u32(r);
}

typedef struct {
    const Clouds *clouds;
    float near_x, far_x, seed;
    Rgbf deep, lit;
} CloudFrame;

static void cloud_corner(const void *ctx, const Frame *f, float x, float y, float *ink, Rgbf *color) {
    const CloudFrame *cf = ctx;
    // Two banks at different heights and speeds; the nearer one moves faster.
    float near = noise_fbm(x * 0.9f + cf->near_x, y * 1.5f + 4), far = noise_fbm(x * 1.3f + cf->far_x, y * 2.0f);
    float dn = smoothstep(0.56f, 0.7f, near), df = smoothstep(0.6f, 0.74f, far) * 0.7f, density = mmax(dn, df);
    if (density < 0.05f) {
        *ink = 0;
        return;
    }
    // Edges are bright and depths dim: ink falls off as the cloud thickens. Near the moon they catch its light.
    float edge = 1 - smoothstep(0.25f, 0.95f, dn > df ? dn : df / 0.7f);
    float dx = x - cf->clouds->moon_x, dy = y - cf->clouds->moon_y, moon = 1 / (1 + (dx * dx + dy * dy) * 6);
    *ink = density * (0.35f + 0.65f * edge);
    *color = rgbf_mix(cf->deep, cf->lit, saturate(edge * 0.5f + moon));
    (void)f;
}

// A night sky of drifting cloud banks, dim in their depths and silvered at their edges where the moon catches them.
void clouds_draw(void *state, const Frame *f) {
    Clouds *cl = state;
    float seed = (float)(cl->salt % 101);
    CloudFrame cf = {cl, seed - f->t * cl->speed * 1.6f, seed * 2 - f->t * cl->speed, seed, palette_ramp(f->palette, 0.3f),
                     palette_ramp(f->palette, 0.95f)};
    canvas_clear(f->canvas);
    canvas_from_corners(f, cloud_corner, &cf);
    frame_resolve(f);
    // The moon and stars go in behind the clouds.
    moon_disc(f, cl->moon_x, cl->moon_y, 0.16f, palette_ramp(f->palette, 1.0f), cl->salt, 1);
    dust_stars(f, cl->salt, 0.0f, 0.7f);
    f->fx->glow = 0.5f;
}

/* ---------- Lanterns ---------- */

typedef struct {
    float water, rise, sway;
    uint32_t salt;
} Lanterns;

void lanterns_setup(void *state, Rng *r, const Frame *f) {
    Lanterns *l = state;
    l->water = rng_between(r, -0.55f, -0.35f);
    l->rise = rng_between(r, 0.04f, 0.08f);
    l->sway = rng_between(r, 0.02f, 0.05f);
    l->salt = rng_u32(r);
    (void)f;
}

static void lantern(const Frame *f, int col, int row, float glow, Rgbf warm, Rgbf paper) {
    Grid *g = f->grid;
    put_cell(g, col, row - 1, '_', frame_dim(f, paper, 0.7f));
    put_cell(g, col - 1, row, '(', paper);
    put_cell(g, col, row, '#', frame_dim(f, warm, 0.7f + 0.3f * glow));
    put_cell(g, col + 1, row, ')', paper);
    put_cell(g, col, row + 1, '\'', frame_dim(f, warm, 0.5f));
}

// Paper lanterns drifting up from a still lake, the near ones whole, the far ones only glowing points, each with its
// reflection trembling in the water below.
void lanterns_draw(void *state, const Frame *f) {
    Lanterns *l = state;
    Grid *g = f->grid;
    dust_stars(f, l->salt, 0.0f, 0.5f);
    Rgbf warm = palette_accent(f->palette, 1), paper = palette_ramp(f->palette, 0.8f), dim = palette_ramp(f->palette, 0.3f);
    int horizon = cell_row(g, l->water);
    // The far shore.
    StrokePoint shore[40];
    for (int i = 0; i < 40; i++) {
        float x = -g->aspect + (float)i / 39.0f * 2 * g->aspect;
        shore[i] = (StrokePoint){x, l->water + 0.03f + 0.08f * noise_fbm(x * 0.9f + (float)(l->salt % 89), 1), dim};
    }
    stroke_draw(f->stroker, shore, 40, 0);
    for (int row = horizon + 1; row < g->rows; row++)
        for (int col = 0; col < g->cols; col++)
            if (hash01((uint32_t)(row * 977 + col * 131) ^ (uint32_t)(f->t * 1.5f + (float)row)) < 0.05f) put_cell(g, col, row, '-', frame_dim(f, dim, 0.6f));
    int count = 50;
    for (int i = 0; i < count; i++) {
        uint32_t id = l->salt + (uint32_t)i * 2246822519u;
        float depth = hash01(id), speed = l->rise * (0.5f + depth), life = (1.0f - l->water + 0.2f) / speed;
        float age = mfract(hash01(id + 1) + f->t / life) * life;
        float x = (hash01(id + 2) * 2 - 1) * g->aspect * 0.95f + l->sway * msin(f->t * 0.6f + (float)i);
        float y = l->water + age * speed;
        float flicker = 0.75f + 0.25f * msin(f->t * (3 + 2 * hash01(id + 3)) + (float)i);
        int col = cell_col(g, x), row = cell_row(g, y);
        if (depth > 0.5f) {
            lantern(f, col, row, flicker, warm, paper);
            put_cell_under(g, col - 2, row, '.', frame_dim(f, warm, 0.3f * flicker));
            put_cell_under(g, col + 2, row, '.', frame_dim(f, warm, 0.3f * flicker));
        } else
            put_cell(g, col, row, depth > 0.3f ? 'o' : '.', frame_dim(f, warm, (0.4f + depth) * flicker));
        // Its reflection, as far below the waterline as the lantern is above it, shivering.
        int mirror = horizon + (horizon - row);
        if (mirror > horizon && mirror < g->rows) {
            int wobble = (int)(msin(f->t * 2.3f + (float)i) * 1.2f);
            put_cell(g, col + wobble, mirror, '~', frame_dim(f, warm, 0.35f * flicker * (depth + 0.3f)));
        }
    }
    f->fx->glow = 0.65f;
}

/* ---------- Jellyfish ---------- */

typedef struct {
    int count;
    float x[5], phase[5], size[5], speed[5];
    uint32_t salt;
} Jellyfish;

void jellyfish_setup(void *state, Rng *r, const Frame *f) {
    Jellyfish *j = state;
    j->count = rng_range(r, 3, 6);
    for (int i = 0; i < j->count; i++) {
        j->x[i] = ((float)i + 0.5f) / (float)j->count * 2 - 1 + rng_between(r, -0.1f, 0.1f);
        j->phase[i] = rng_float(r) * TAU_F;
        j->size[i] = rng_between(r, 0.12f, 0.22f);
        j->speed[i] = rng_between(r, 0.03f, 0.06f);
    }
    j->salt = rng_u32(r);
    (void)f;
}

// Jellyfish drifting up through dark water, their bells slowly pulsing and their tentacles trailing behind.
void jellyfish_draw(void *state, const Frame *f) {
    Jellyfish *j = state;
    Grid *g = f->grid;
    // Specks of sea snow rising past.
    for (int s = 0; s < g->cols * g->rows / 30; s++) {
        uint32_t id = j->salt + (uint32_t)s * 2654435761u;
        int col = (int)(hash01(id) * (float)g->cols), row = (int)((1 - mfract(hash01(id + 1) + f->t * 0.015f)) * (float)g->rows);
        put_cell_under(g, col, row, '.', palette_ramp(f->palette, 0.3f + 0.3f * hash01(id + 2)));
    }
    for (int i = 0; i < j->count; i++) {
        float beat = msin(f->t * 0.9f + j->phase[i]), squeeze = 0.85f + 0.15f * beat;
        float span = 2.6f, y = -1.3f + mfract((f->t * j->speed[i] + j->phase[i] / TAU_F)) * span;
        float x = j->x[i] * g->aspect * 0.8f + 0.05f * msin(f->t * 0.3f + j->phase[i]);
        float w = j->size[i] * g->aspect * 0.6f * (2 - squeeze), h = j->size[i] * squeeze;
        Rgbf body = palette_accent(f->palette, i), glow = palette_ramp(f->palette, 0.95f);
        // The bell: a half ellipse with a softly lit dome.
        StrokePoint bell[24];
        for (int k = 0; k < 24; k++) {
            float a = PI_F * (float)k / 23.0f;
            bell[k] = (StrokePoint){x + mcos(a) * w, y + msin(a) * h, rgbf_mix(body, glow, msin(a) * 0.5f)};
        }
        stroke_draw(f->stroker, bell, 24, 0);
        StrokePoint rim[2] = {{x - w, y, body}, {x + w, y, body}};
        stroke_draw(f->stroker, rim, 2, 0);
        // Tentacles, swaying a beat behind the bell.
        for (int t = 0; t < 5; t++) {
            StrokePoint arm[10];
            float tx = x + w * ((float)t / 2.0f - 1) * 0.8f;
            for (int k = 0; k < 10; k++) {
                float down = (float)k * h * 0.45f;
                arm[k] = (StrokePoint){tx + 0.04f * msin(f->t * 1.2f - (float)k * 0.5f + (float)t + j->phase[i]), y - down,
                                       frame_dim(f, body, 0.8f - (float)k * 0.06f)};
            }
            stroke_draw(f->stroker, arm, 10, 0);
        }
    }
    f->fx->glow = 0.7f;
}

/* ---------- Terrain ---------- */

typedef struct {
    float speed, height, seed, sun_x;
    int sun;
} Terrain;

void terrain_setup(void *state, Rng *r, const Frame *f) {
    Terrain *t = state;
    t->speed = rng_between(r, 0.5f, 0.9f);
    t->height = rng_between(r, 0.45f, 0.65f);
    t->seed = rng_between(r, 0, 100);
    t->sun = rng_chance(r, 75);
    t->sun_x = rng_between(r, -0.45f, 0.45f) * f->grid->aspect;
}

#define MESH_X 25
#define MESH_Z 16

// Rolling wireframe hills flown over at a steady speed, under a low moon; the ground is flattest straight ahead.
void terrain_draw(void *state, const Frame *f) {
    Terrain *t = state;
    Grid *g = f->grid;
    dust_stars(f, (uint32_t)(t->seed * 977), 0.0f, 0.5f);
    const float horizon = 0.08f, step = 0.45f, eye = 0.7f;
    if (t->sun) moon_disc(f, t->sun_x, horizon + 0.3f, 0.2f, palette_accent(f->palette, 1), (uint32_t)(t->seed * 31), 0);
    for (int col = 0; col < g->cols; col++) put_cell(g, col, cell_row(g, horizon), '_', palette_ramp(f->palette, 0.4f));
    float travel = f->t * t->speed, offset = mfract(travel / step) * step, row0 = mfloor(travel / step);
    StrokePoint mesh[MESH_Z][MESH_X];
    for (int k = 0; k < MESH_Z; k++) {
        float z = 0.5f + (float)k * step - offset, fade = 1 - (float)k / (float)MESH_Z;
        Rgbf color = rgbf_mix(palette_ramp(f->palette, 0.2f), palette_ramp(f->palette, 0.9f), fade * fade);
        for (int i = 0; i < MESH_X; i++) {
            float x = ((float)i - (MESH_X - 1) * 0.5f) * 0.4f;
            float h = noise_fbm(x * 0.35f + t->seed, ((float)k + row0) * step * 0.35f) * t->height * (0.25f + 2.6f * smoothstep(1.0f, 4.0f, mabs(x)));
            mesh[k][i] = (StrokePoint){x / z, horizon + (h - eye) / z, color};
        }
    }
    for (int k = 0; k < MESH_Z; k++) stroke_draw(f->stroker, mesh[k], MESH_X, 0);
    for (int i = 0; i < MESH_X; i++) {
        StrokePoint column[MESH_Z];
        for (int k = 0; k < MESH_Z; k++) column[k] = mesh[k][i];
        stroke_draw(f->stroker, column, MESH_Z, 0);
    }
    f->fx->glow = 0.6f;
}

/* ---------- Galaxy ---------- */

typedef struct {
    float spin, wind, tilt, seed;
    int arms;
} Galaxy;

void galaxy_setup(void *state, Rng *r, const Frame *f) {
    Galaxy *g = state;
    g->spin = rng_between(r, 0.03f, 0.07f) * (rng_chance(r, 50) ? 1 : -1);
    g->wind = rng_between(r, 2.2f, 3.4f);
    g->tilt = rng_between(r, 0.55f, 0.85f);
    g->seed = rng_between(r, 0, 90);
    g->arms = rng_chance(r, 70) ? 2 : 3;
    (void)f;
}

// A spiral galaxy turning slowly: a bright core, arms of stars winding out, and dark lanes of dust between them.
void galaxy_draw(void *state, const Frame *f) {
    Galaxy *gx = state;
    Grid *g = f->grid;
    dust_stars(f, (uint32_t)(gx->seed * 131), 0.0f, 0.6f);
    static const char ramp[] = " .:;+*#";
    float turn = f->t * gx->spin;
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row) / gx->tilt;
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), radius = mhypot(x, y);
            if (radius > 1.25f) continue;
            float a = matan2(y, x) - turn;
            float arm = 0.5f + 0.5f * mcos((float)gx->arms * (a - gx->wind * mlog2(radius + 0.05f)));
            float dust = noise_fbm(x * 3 + gx->seed, y * 3);
            float level = mexp(-radius * 5.0f) * 1.3f + arm * arm * arm * mexp(-radius * 1.1f) * (0.35f + 0.75f * dust);
            level = saturate(level - 0.06f);
            int i = (int)(level * 6.99f);
            if (i <= 0) continue;
            Rgbf color = rgbf_mix(palette_accent(f->palette, 2), palette_ramp(f->palette, 0.5f + 0.5f * level), saturate(level * 1.5f));
            *grid_at(g, col, row) = (Cell){(uint8_t)ramp[i], rgb_from_f(color), 0};
        }
    }
    f->fx->glow = 0.65f;
}

/* ---------- Fireflies ---------- */

typedef struct {
    float moon_x, grass;
    uint32_t salt;
} Fireflies;

void fireflies_setup(void *state, Rng *r, const Frame *f) {
    Fireflies *p = state;
    p->moon_x = rng_between(r, -0.6f, 0.6f) * f->grid->aspect;
    p->grass = rng_between(r, -0.6f, -0.4f);
    p->salt = rng_u32(r);
}

// A meadow at dusk: tall grass swaying under a big moon, and fireflies wandering, each blinking in its own time.
void fireflies_draw(void *state, const Frame *f) {
    Fireflies *p = state;
    Grid *g = f->grid;
    dust_stars(f, p->salt, 0.0f, 0.55f);
    moon_disc(f, p->moon_x, 0.5f, 0.18f, palette_ramp(f->palette, 0.95f), p->salt, 0);
    Rgbf blade = palette_ramp(f->palette, 0.22f), tip = palette_ramp(f->palette, 0.4f);
    int top = cell_row(g, p->grass);
    // Grass in tufts: each blade's tip bends with the breeze, and the stems below fade into the dark.
    for (int col = 0; col < g->cols; col++) {
        uint32_t id = (uint32_t)col * 2654435761u ^ p->salt;
        if (hash01(id) < 0.35f) continue;
        float sway = msin(f->t * 0.8f + (float)col * 0.25f);
        int crown = top + (int)(hash01(id + 1) * 4);
        for (int row = crown; row < g->rows && row < crown + 3; row++) {
            int glyph = row == crown ? (sway > 0.3f ? '/' : sway < -0.3f ? '\\' : '|') : '|';
            put_cell(g, col, row, glyph, frame_dim(f, row == crown ? tip : blade, 1 - 0.25f * (float)(row - crown)));
        }
    }
    Rgbf light = palette_accent(f->palette, 1);
    for (int i = 0; i < 96; i++) {
        uint32_t id = p->salt + (uint32_t)i * 2246822519u;
        float x = (noise_value(f->t * 0.05f + (float)i * 7.3f, 1) * 2 - 1) * g->aspect * 1.1f;
        float y = p->grass + 0.05f + noise_value(f->t * 0.04f + (float)i * 3.1f, 5) * 0.75f;
        float blink = msin(f->t * (1.2f + hash01(id) * 1.5f) + hash01(id + 1) * TAU_F);
        if (blink < 0.05f) continue;
        int col = cell_col(g, x), row = cell_row(g, y);
        put_cell(g, col, row, blink > 0.75f ? '*' : 'o', frame_dim(f, light, 0.4f + blink * 0.8f));
        if (blink > 0.6f) {
            put_cell_under(g, col - 1, row, '.', frame_dim(f, light, 0.35f * blink));
            put_cell_under(g, col + 1, row, '.', frame_dim(f, light, 0.35f * blink));
        }
    }
    f->fx->glow = 0.7f;
}

/* ---------- Ocean ---------- */

typedef struct {
    float horizon, moon_x, moon_y, moon_r, swell;
    uint32_t salt;
} Ocean;

void ocean_setup(void *state, Rng *r, const Frame *f) {
    Ocean *o = state;
    o->horizon = rng_between(r, -0.25f, -0.05f);
    o->moon_x = rng_between(r, -0.45f, 0.45f) * f->grid->aspect;
    o->moon_y = rng_between(r, 0.35f, 0.6f);
    o->moon_r = rng_between(r, 0.14f, 0.2f);
    o->swell = rng_between(r, 0.6f, 1.0f);
    o->salt = rng_u32(r);
}

// Open water under a full moon: rows of waves closing in toward the horizon, and the moon's path across them made of
// short bright dashes that break apart with every swell.
void ocean_draw(void *state, const Frame *f) {
    Ocean *o = state;
    Grid *g = f->grid;
    dust_stars(f, o->salt, 0.0f, 0.6f);
    moon_disc(f, o->moon_x, o->moon_y, o->moon_r, palette_ramp(f->palette, 1.0f), o->salt, 0);
    int horizon = cell_row(g, o->horizon);
    Rgbf moon = palette_ramp(f->palette, 1.0f), deep = palette_ramp(f->palette, 0.3f);
    for (int col = 0; col < g->cols; col++) put_cell(g, col, horizon, '_', palette_ramp(f->palette, 0.45f));
    for (int row = horizon + 1; row < g->rows; row++) {
        float near = (float)(row - horizon) / (float)(g->rows - horizon);
        float scale = 1.0f / (0.15f + near);
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col);
            float wave = msin(x * 3.0f * scale + f->t * o->swell * (0.6f + near) + noise_value(x * scale, near * 9) * 4);
            // The moon's path widens toward the viewer and only catches the wave crests.
            float path = mabs(x - o->moon_x) / (0.05f + near * 0.45f);
            if (path < 1 && wave > 0.1f - near * 0.3f) {
                put_cell(g, col, row, wave > 0.6f ? '=' : '-', frame_dim(f, moon, 0.5f + 0.5f * (1 - path)));
            } else if (wave > 0.75f - near * 0.25f) {
                put_cell(g, col, row, '~', rgbf_mix(deep, palette_ramp(f->palette, 0.6f), near));
            } else if (wave > 0.4f && hash01((uint32_t)(row * 733 + col)) < 0.3f) {
                put_cell(g, col, row, '-', frame_dim(f, deep, 0.8f));
            }
        }
    }
    f->fx->glow = 0.55f;
}
