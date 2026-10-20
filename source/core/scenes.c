// SPDX-License-Identifier: GPL-3.0-only
#include "mathx.h"
#include "figlet.h"
#include "noise.h"
#include "scene.h"

#include <stdio.h>

#include <stdint.h>
#include <string.h>

void frame_resolve(const Frame *f) {
    ascii_resolve(f->matcher, f->canvas, f->grid);
}

Rgbf frame_dim(const Frame *f, Rgbf color, float k) {
    Rgbf ground = rgbf(f->palette->bg);
    return rgbf_add(ground, rgbf_scale(rgbf_add(color, rgbf_scale(ground, -1)), k));
}

float frame_bright(const Frame *f) {
    return f->react * (f->calm ? 0.3f : 1.0f);
}

float frame_pulse(const Frame *f, Role role, float tau, float strength) {
    if (f->react <= 0) return 0;
    float p = score_pulse(f->score, role, f->t, f->calm ? tau * 2.5f : tau);
    return p * strength * 1.5f * f->react * (f->calm ? 0.3f : 1.0f);
}

// Still scenes move at a steady speed instead of the music's.
float frame_travel(const Frame *f, float base, float with_energy) {
    float start = f->t - f->local, steady = with_energy * 0.6f * f->local;
    float music = with_energy * (score_energy_area(f->score, f->t) - score_energy_area(f->score, start));
    return base * f->local + mixf(steady, music, f->react);
}

float frame_surge(const Frame *f, Role role, float tau, float strength) {
    if (f->react <= 0) return 0;
    if (f->calm) {
        tau *= 2.5f;
        strength *= 0.3f;
    }
    strength *= f->react;
    return strength * (score_pulse_area(f->score, role, f->t, tau) - score_pulse_area(f->score, role, f->t - f->local, tau));
}

void put_cell(Grid *g, int col, int row, int glyph, Rgbf color) {
    if (col < 0 || row < 0 || col >= g->cols || row >= g->rows) return;
    *grid_at(g, col, row) = (Cell){(uint8_t)glyph, rgb_from_f(color), 0};
}

void put_cell_under(Grid *g, int col, int row, int glyph, Rgbf color) {
    if (col < 0 || row < 0 || col >= g->cols || row >= g->rows) return;
    Cell *c = grid_at(g, col, row);
    if (c->glyph == ' ') *c = (Cell){(uint8_t)glyph, rgb_from_f(color), 0};
}

// Scene units to grid cells.
int cell_col(const Grid *g, float x) {
    return mfloor_i((x + g->aspect) / (2 * g->aspect) * (float)g->cols);
}

int cell_row(const Grid *g, float y) {
    return mfloor_i((1 - y) * 0.5f * (float)g->rows);
}

// A faint field of far stars, shared by the space scenes.
void dust_stars(const Frame *f, uint32_t salt, float drift, float level) {
    Grid *g = f->grid;
    int count = g->cols * g->rows / 22;
    for (int s = 0; s < count; s++) {
        uint32_t id = salt + (uint32_t)s * 2654435761u;
        float hx = hash01(id), hy = hash01(id + 1), hp = hash01(id + 2);
        float fx = hx * (float)g->cols - f->t * drift * (0.3f + hp);
        fx -= mfloor(fx / (float)g->cols) * (float)g->cols;
        float tw = 0.6f + 0.4f * msin(f->t * (0.8f + 2.5f * hp) + hp * TAU_F);
        Rgbf color = frame_dim(f, palette_ramp(f->palette, 0.55f + 0.45f * hp), level * tw);
        put_cell_under(g, (int)fx, (int)(hy * (float)g->rows), hp > 0.92f ? '+' : '.', color);
    }
}

// Walks back through a role's notes from t toward t - span, newest first.
int recent_notes(const Score *s, Role role, float t, float span, const ScoreEvent **out, int max) {
    const int *index = s->by_role[role];
    int lo = 0, hi = s->role_count[role], count = 0;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (s->events[index[mid]].time <= t)
            lo = mid + 1;
        else
            hi = mid;
    }
    for (int k = lo - 1; k >= 0 && count < max; k--) {
        const ScoreEvent *e = &s->events[index[k]];
        if (e->time < t - span) break;
        out[count++] = e;
    }
    return count;
}

/* ---------- Starfield warp ---------- */

typedef struct {
    float speed, swirl, streak;
    int count;
    uint32_t salt;
} Warp;

static void warp_setup(void *state, Rng *r, const Frame *f) {
    Warp *w = state;
    w->speed = rng_between(r, 0.12f, 0.24f);
    w->swirl = rng_chance(r, 40) ? rng_between(r, -0.25f, 0.25f) : 0;
    w->streak = rng_between(r, 0.5f, 1.0f);
    w->count = f->grid->cols * f->grid->rows / 14;
    w->salt = rng_u32(r);
}

// Stars travel toward the viewer; the music pushes them faster and draws streaks behind the near ones.
static void warp_draw(void *state, const Frame *f) {
    Warp *w = state;
    Grid *g = f->grid;
    float push = frame_pulse(f, ROLE_KICK, 0.18f, 0.6f) + frame_pulse(f, ROLE_SNARE, 0.25f, 0.3f);
    float speed = w->speed * (0.5f + f->energy * 1.6f);
    float travel = frame_travel(f, w->speed * 0.5f, w->speed * 1.6f) + frame_surge(f, ROLE_KICK, 0.18f, 0.25f) +
                   frame_surge(f, ROLE_SNARE, 0.25f, 0.12f);
    float spin = w->swirl * f->local;
    for (int i = 0; i < w->count; i++) {
        uint32_t id = w->salt + (uint32_t)i * 2246822519u;
        float x = hash01(id) * 2 - 1, y = hash01(id + 1) * 2 - 1, z0 = hash01(id + 2);
        float z = mfract(z0 - travel);
        if (z < 0.02f) continue;
        float a = spin * (1 - z);
        float rx = x * mcos(a) - y * msin(a), ry = x * msin(a) + y * mcos(a);
        float sx = rx / z * 0.35f, sy = ry / z * 0.35f;
        if (mabs(sx) > g->aspect || mabs(sy) > 1) continue;
        float near = 1 - z;
        Rgbf color = palette_ramp(f->palette, 0.25f + near * 0.75f);
        if ((id & 15) == 0) color = palette_accent(f->palette, (int)(id >> 4));
        float trail = (speed + push * 0.6f) * w->streak * 0.35f;
        if (near > 0.55f && trail > 0.05f && !f->still) {
            float z1 = z + trail * near;
            StrokePoint seg[2] = {{rx / z1 * 0.35f, ry / z1 * 0.35f, frame_dim(f, color, 0.45f)}, {sx, sy, color}};
            stroke_draw(f->stroker, seg, 2, 0);
        }
        int glyph = near > 0.9f ? '@' : near > 0.75f ? '*' : near > 0.45f ? '+' : '.';
        put_cell(g, cell_col(g, sx), cell_row(g, sy), glyph, frame_dim(f, color, 0.35f + near * 0.75f));
    }
    f->fx->glow = 0.45f;
}

/* ---------- Tunnel ---------- */

#define TEX 256

typedef struct {
    float speed, twist, spin;
    int square, texture, around;
    uint32_t seed;
} Tunnel;

// Per-sample angle and depth, computed once per canvas shape: a tunnel frame is then only integer adds and lookups.
static struct {
    int w, h, square;
    uint16_t *u, *v;
    uint8_t *fog;
} tunnel_lut;

static uint8_t tunnel_tex[2][TEX * TEX];
static uint32_t tunnel_tex_seed[2] = {1, 1};

static void tunnel_lut_build(const Canvas *c, int square) {
    if (tunnel_lut.w == c->w && tunnel_lut.h == c->h && tunnel_lut.square == square && tunnel_lut.u) return;
    static uint16_t u_store[512 * 320], v_store[512 * 320];
    static uint8_t fog_store[512 * 320];
    if (c->w * c->h > 512 * 320) return;
    tunnel_lut.u = u_store;
    tunnel_lut.v = v_store;
    tunnel_lut.fog = fog_store;
    for (int j = 0; j < c->h; j++)
        for (int i = 0; i < c->w; i++) {
            float x = canvas_x(c, i), y = canvas_y(c, j);
            float r = square ? mmax(mabs(x), mabs(y)) : mhypot(x, y);
            if (r < 0.02f) r = 0.02f;
            float a = matan2(y, x) / TAU_F + 0.5f;
            int k = j * c->w + i;
            tunnel_lut.u[k] = (uint16_t)(a * 65535.0f);
            tunnel_lut.v[k] = (uint16_t)mmin(65535.0f, 0.4f / r * 4096.0f);
            tunnel_lut.fog[k] = (uint8_t)(saturate(r * 1.4f) * 255.0f);
        }
    tunnel_lut.w = c->w;
    tunnel_lut.h = c->h;
    tunnel_lut.square = square;
}

static int tunnel_texture(int kind, uint32_t seed) {
    int slot = tunnel_tex_seed[0] == seed ? 0 : tunnel_tex_seed[1] == seed ? 1 : -1;
    if (slot >= 0) return slot;
    slot = tunnel_tex_seed[0] == 1 ? 0 : tunnel_tex_seed[1] == 1 ? 1 : (int)(seed & 1);
    uint8_t *t = tunnel_tex[slot];
    for (int y = 0; y < TEX; y++)
        for (int x = 0; x < TEX; x++) {
            float u = (float)x / TEX, v = (float)y / TEX, ink;
            switch (kind) {
            case 0: {
                // Checker tiles with dark grout, so the tiles read as separate glyph clusters.
                int cx = x & 31, cy = y & 31;
                ink = (((x >> 5) ^ (y >> 5)) & 1 ? 0.85f : 0.3f) * (cx > 2 && cy > 2 ? 1.0f : 0.0f);
                break;
            }
            case 1: {
                float ring = 0.5f + 0.5f * msin(v * TAU_F * 4);
                ink = mpow(ring, 6) * 0.95f + 0.08f;
                break;
            }
            case 2: {
                float stripe = 0.5f + 0.5f * msin((u * 6 + v * 2) * TAU_F);
                ink = smoothstep(0.55f, 0.85f, stripe) * 0.9f + 0.05f;
                break;
            }
            default: {
                // Woven threads: warp over weft, then weft over warp.
                float wu = mfract(u * 12), wv = mfract(v * 8);
                int over = ((int)(u * 12) + (int)(v * 8)) & 1;
                float warp = smoothstep(0.42f, 0.25f, mabs(wu - 0.5f)), weft = smoothstep(0.42f, 0.25f, mabs(wv - 0.5f));
                ink = (over ? mmax(warp, weft * 0.6f) : mmax(weft, warp * 0.6f)) * 0.9f;
                break;
            }
            }
            t[y * TEX + x] = (uint8_t)(saturate(ink) * 255);
        }
    tunnel_tex_seed[slot] = seed;
    return slot;
}

static void tunnel_setup(void *state, Rng *r, const Frame *f) {
    Tunnel *t = state;
    t->speed = rng_between(r, 0.5f, 1.1f);
    t->twist = rng_chance(r, 50) ? rng_between(r, -0.8f, 0.8f) : 0;
    t->spin = rng_between(r, -0.06f, 0.06f);
    t->square = rng_chance(r, 30);
    t->texture = rng_range(r, 0, 4);
    t->seed = rng_u32(r) | 2;
    (void)f;
}

static void tunnel_draw(void *state, const Frame *f) {
    Tunnel *t = state;
    Canvas *c = f->canvas;
    tunnel_lut_build(c, t->square);
    if (!tunnel_lut.u) return;
    const uint8_t *tex = tunnel_tex[tunnel_texture(t->texture, t->seed)];
    float kick = frame_pulse(f, ROLE_KICK, 0.15f, 1.0f);
    float travel = frame_travel(f, t->speed * 0.6f, t->speed) + frame_surge(f, ROLE_KICK, 0.15f, 0.5f);
    uint32_t dv = (uint32_t)(int32_t)(travel * 65536.0f * 4), du = (uint32_t)(int32_t)(f->local * t->spin * 65536.0f);
    int twist = (int)(t->twist * 64);
    // Colors by depth, looked up rather than mixed per sample.
    Rgbf shade[64];
    for (int i = 0; i < 64; i++) {
        float d = (float)i / 63.0f;
        shade[i] = rgbf_mix(palette_accent(f->palette, 2), palette_ramp(f->palette, 0.35f + 0.65f * d), d);
        shade[i] = rgbf_mix(shade[i], palette_accent(f->palette, 0), 0.3f * kick * d);
    }
    float inks[256];
    for (int i = 0; i < 256; i++) inks[i] = (float)i / 255.0f;
    int n = c->w * c->h;
    for (int k = 0; k < n; k++) {
        uint32_t v = (uint32_t)tunnel_lut.v[k] * 16 + dv;
        uint32_t u = ((uint32_t)tunnel_lut.u[k] + du + (uint32_t)(int32_t)(((int)tunnel_lut.v[k] * twist) >> 6)) & 0xffff;
        int tx = (int)(u >> 8), ty = (int)((v >> 10) & 255);
        int fog = tunnel_lut.fog[k];
        c->ink[k] = inks[tex[ty * TEX + tx]] * inks[fog] * 0.62f;
        c->color[k] = shade[fog >> 2];
    }
    frame_resolve(f);
    f->fx->glow = 0.35f + 0.25f * kick;
}

/* ---------- Plasma: liquid color, topographic contours, or interference rings ---------- */

typedef struct {
    float a, b, c, d, speed, cycle, bands;
    float cx[3], cy[3], wx[3], wy[3];
    int look;
} Plasma;

static void plasma_setup(void *state, Rng *r, const Frame *f) {
    Plasma *p = state;
    p->a = rng_between(r, 1.5f, 3.5f);
    p->b = rng_between(r, 1.5f, 3.5f);
    p->c = rng_between(r, 1.0f, 2.5f);
    p->d = rng_between(r, 2.0f, 5.0f);
    p->speed = rng_between(r, 0.35f, 0.7f);
    p->cycle = rng_between(r, 0.03f, 0.08f);
    p->bands = rng_between(r, 5, 9);
    p->look = rng_range(r, 0, 3);
    for (int k = 0; k < 3; k++) {
        p->cx[k] = rng_between(r, -0.8f, 0.8f);
        p->cy[k] = rng_between(r, -0.6f, 0.6f);
        p->wx[k] = rng_between(r, 0.1f, 0.3f);
        p->wy[k] = rng_between(r, 0.1f, 0.3f);
    }
    (void)f;
}

static float plasma_field(const Plasma *p, float x, float y, float t) {
    if (p->look == 2) {
        float v = 0;
        for (int k = 0; k < 3; k++) {
            float cx = p->cx[k] + 0.4f * msin(t * p->wx[k] + (float)k), cy = p->cy[k] + 0.3f * mcos(t * p->wy[k] + (float)k * 2);
            v += msin(mhypot(x - cx, y - cy) * p->d * 3 - t * 2);
        }
        return v / 3;
    }
    return (msin(x * p->a + t) + msin(y * p->b + t * 1.3f) + msin((x + y) * p->c + t * 0.7f) +
            msin(msqrt(x * x + y * y) * p->d - t)) * 0.25f;
}

// Three stops around the palette, so the colors flow rather than just brighten.
static Rgbf flow_color(const Palette *pal, float h) {
    h = mfract(h) * 3;
    Rgbf a = palette_ramp(pal, 0.55f), b = palette_accent(pal, 0), c = palette_accent(pal, 2);
    if (h < 1) return rgbf_mix(a, b, h);
    if (h < 2) return rgbf_mix(b, c, h - 1);
    return rgbf_mix(c, a, h - 2);
}

static void plasma_draw(void *state, const Frame *f) {
    Plasma *p = state;
    Grid *g = f->grid;
    static const char ramp[] = " .:-=+*#";
    float t = f->local * p->speed, lift = frame_pulse(f, ROLE_KICK, 0.22f, 0.12f);
    float dx = 2 * g->aspect / (float)g->cols, dy = 2.0f / (float)g->rows;
    for (int row = 0; row < g->rows; row++) {
        float y = 1 - ((float)row + 0.5f) * dy;
        for (int col = 0; col < g->cols; col++) {
            float x = ((float)col + 0.5f) * dx - g->aspect;
            float level = 0.5f + 0.5f * plasma_field(p, x, y, t);
            Cell *cell = grid_at(g, col, row);
            if (p->look == 1) {
                // Contour lines only, like a topographic map; the glyph follows the slope of the land.
                float band = mfract(level * p->bands + f->local * 0.05f);
                if (band > 0.14f && band < 0.86f) continue;
                float gx = plasma_field(p, x + dx, y, t) - plasma_field(p, x - dx, y, t);
                float gy = plasma_field(p, x, y + dy, t) - plasma_field(p, x, y - dy, t);
                float along = matan2(gx, -gy), a = mabs(along) > PI_F / 2 ? PI_F - mabs(along) : mabs(along);
                int glyph = a < 0.45f ? '|' : a < 1.12f ? ((gx > 0) == (gy > 0) ? '\\' : '/') : (band < 0.5f ? '-' : '~');
                Rgbf color = flow_color(f->palette, level * 0.9f + f->local * p->cycle);
                *cell = (Cell){(uint8_t)glyph, rgb_from_f(frame_dim(f, color, 0.75f + lift * 2)), 0};
                continue;
            }
            float ink = (p->look == 2 ? smoothstep(0.55f, 0.97f, level) : smoothstep(0.3f, 0.95f, level)) + lift;
            if (ink < 0.08f) continue;
            int glyph = ramp[clampi((int)(ink * 8.0f), 1, 7)];
            Rgbf color = frame_dim(f, flow_color(f->palette, level * 0.8f + f->local * p->cycle), 0.55f + 0.45f * ink);
            *cell = (Cell){(uint8_t)glyph, rgb_from_f(color), 0};
        }
    }
    f->fx->glow = 0.4f;
}

/* ---------- Synthwave sunset over a neon grid ---------- */

typedef struct {
    float horizon, sun_r, grid_speed, ridge_seed;
    int stripes;
} Sunset;

static void sunset_setup(void *state, Rng *r, const Frame *f) {
    Sunset *s = state;
    s->horizon = rng_between(r, -0.15f, 0.05f);
    s->sun_r = rng_between(r, 0.32f, 0.46f);
    s->grid_speed = rng_between(r, 0.6f, 1.2f);
    s->ridge_seed = rng_between(r, 0, 100);
    s->stripes = rng_range(r, 4, 8);
    (void)f;
}

static float ridge(float x, float seed, float scale) {
    return noise_fbm(x * scale + seed, seed * 0.37f) - 0.5f;
}

static void sunset_draw(void *state, const Frame *f) {
    Sunset *s = state;
    Grid *g = f->grid;
    // Stars only in the sky: cleared from below the horizon before the floor's lines go down, so the dots those
    // lines are drawn with stay.
    dust_stars(f, 0x5eed, 0.0f, 0.8f);
    for (int col = 0; col < g->cols; col++)
        for (int row = cell_row(g, s->horizon) + 1; row < g->rows; row++) grid_at(g, col, row)->glyph = ' ';
    float kick = frame_pulse(f, ROLE_KICK, 0.2f, 1.0f);
    float sun_y = s->horizon + s->sun_r * 0.55f;
    Rgbf top = palette_ramp(f->palette, 1.0f), bottom = palette_accent(f->palette, 1);
    // The sun as scanlines of '=' that thin into '-' as the bands slide down into the horizon.
    for (int row = 0; row < g->rows; row++) {
        float y = 1 - ((float)row + 0.5f) / (float)g->rows * 2;
        if (y < s->horizon || y > sun_y + s->sun_r) continue;
        float v = (sun_y + s->sun_r - y) / (2 * s->sun_r);
        float band = mfract(v * (float)s->stripes - f->local * 0.15f);
        float gap = v > 0.4f ? (v - 0.4f) * 1.1f : 0;
        if (band < gap) continue;
        Rgbf color = frame_dim(f, rgbf_mix(top, bottom, saturate(v * 1.15f)), 0.8f + 0.2f * kick);
        int glyph = band < gap + 0.2f && gap > 0 ? '-' : '=';
        for (int col = 0; col < g->cols; col++) {
            float x = ((float)col + 0.5f) / (float)g->cols * 2 * g->aspect - g->aspect;
            float d = mhypot(x, y - sun_y);
            if (d < s->sun_r) put_cell(g, col, row, d > s->sun_r * 0.94f ? (x < 0 ? '(' : ')') : glyph, color);
        }
    }
    // Mountains in two ranges, the far one dimmer, drawn as hand-traced line art.
    for (int layer = 0; layer < 2; layer++) {
        StrokePoint pts[96];
        float scale = layer ? 1.6f : 0.9f, height = layer ? 0.2f : 0.32f;
        Rgbf color = layer ? palette_ramp(f->palette, 0.7f) : palette_ramp(f->palette, 0.4f);
        for (int i = 0; i < 96; i++) {
            float x = -g->aspect + (float)i / 95.0f * 2 * g->aspect;
            float peak = mabs(ridge(x, s->ridge_seed + (float)layer * 31, scale)) * height * 2.2f;
            pts[i] = (StrokePoint){x, s->horizon + peak * smoothstep(0.1f, 0.9f, mabs(x) / g->aspect + 0.25f), color};
        }
        stroke_draw(f->stroker, pts, 96, 0);
    }
    // The floor: lines fanning from the vanishing point, and crossbars rolling toward us on the beat.
    Rgbf neon = palette_accent(f->palette, 0);
    float glow = 0.55f + 0.45f * kick;
    for (int k = -12; k <= 12; k++) {
        float spread = (float)k * 0.22f;
        StrokePoint line[2] = {{spread * 0.05f, s->horizon - 0.01f, frame_dim(f, neon, 0.25f)},
                               {spread * 4.5f, -1.4f, frame_dim(f, neon, glow)}};
        stroke_draw(f->stroker, line, 2, 0);
    }
    float scroll = mfract(frame_travel(f, s->grid_speed * 0.5f, s->grid_speed));
    for (int k = 0; k < 9; k++) {
        float z = (float)k + 1 - scroll, y = s->horizon - 0.9f / z;
        if (y < -1.05f || y > s->horizon - 0.01f) continue;
        float near = saturate(1.2f / z);
        StrokePoint bar[2] = {{-g->aspect, y, frame_dim(f, neon, 0.2f + near * glow)}, {g->aspect, y, frame_dim(f, neon, 0.2f + near * glow)}};
        stroke_draw(f->stroker, bar, 2, 0);
    }
    f->fx->glow = 0.5f;
}

/* ---------- Wireframe solids ---------- */

typedef struct {
    int shape, count;
    float spin_x, spin_y, size;
} Solids;

static const float CUBE[8][3] = {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
                                 {-1, -1, 1},  {1, -1, 1},  {1, 1, 1},  {-1, 1, 1}};
static const int CUBE_EDGES[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                      {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
static const float OCTA[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
static const int OCTA_EDGES[12][2] = {{0, 2}, {0, 3}, {0, 4}, {0, 5}, {1, 2}, {1, 3},
                                      {1, 4}, {1, 5}, {2, 4}, {4, 3}, {3, 5}, {5, 2}};
#define PHI 1.618034f
static const float ICOSA[12][3] = {{-1, PHI, 0}, {1, PHI, 0},  {-1, -PHI, 0}, {1, -PHI, 0}, {0, -1, PHI}, {0, 1, PHI},
                                   {0, -1, -PHI}, {0, 1, -PHI}, {PHI, 0, -1},  {PHI, 0, 1},  {-PHI, 0, -1}, {-PHI, 0, 1}};
static const int ICOSA_EDGES[30][2] = {{0, 1},  {0, 5},  {0, 7},  {0, 10}, {0, 11}, {1, 5},  {1, 7},  {1, 8},
                                       {1, 9},  {2, 3},  {2, 4},  {2, 6},  {2, 10}, {2, 11}, {3, 4},  {3, 6},
                                       {3, 8},  {3, 9},  {4, 5},  {4, 9},  {4, 11}, {5, 9},  {5, 11}, {6, 7},
                                       {6, 8},  {6, 10}, {7, 8},  {7, 10}, {8, 9},  {10, 11}};

static void solids_setup(void *state, Rng *r, const Frame *f) {
    Solids *s = state;
    s->shape = rng_range(r, 0, 3);
    s->count = rng_range(r, 1, 3);
    s->spin_x = rng_between(r, 0.2f, 0.5f);
    s->spin_y = rng_between(r, 0.3f, 0.7f);
    s->size = rng_between(r, 0.38f, 0.55f);
    (void)f;
}

static void solid(const Frame *f, const float (*v)[3], int nv, const int (*e)[2], int ne, float scale, float ax, float ay,
                  float cx, Rgbf color) {
    float px[12], py[12], depth[12];
    Rgbf far = palette_ramp(f->palette, 0.45f);
    float sx = msin(ax), cxr = mcos(ax), sy = msin(ay), cy = mcos(ay);
    for (int i = 0; i < nv && i < 12; i++) {
        float x = v[i][0], y = v[i][1], z = v[i][2];
        float y1 = y * cxr - z * sx, z1 = y * sx + z * cxr;
        float x2 = x * cy + z1 * sy, z2 = -x * sy + z1 * cy;
        float persp = 3.2f / (3.2f + z2 * scale);
        px[i] = cx + x2 * scale * persp;
        py[i] = y1 * scale * persp;
        depth[i] = z2;
    }
    for (int k = 0; k < ne; k++) {
        int a = e[k][0], b = e[k][1];
        float near_a = saturate(0.5f - depth[a] * 0.3f), near_b = saturate(0.5f - depth[b] * 0.3f);
        StrokePoint seg[2] = {{px[a], py[a], frame_dim(f, rgbf_mix(far, color, near_a), 0.45f + 0.55f * near_a)},
                              {px[b], py[b], frame_dim(f, rgbf_mix(far, color, near_b), 0.45f + 0.55f * near_b)}};
        stroke_draw(f->stroker, seg, 2, 0);
    }
    for (int i = 0; i < nv && i < 12; i++)
        if (depth[i] < 0) put_cell(f->grid, cell_col(f->grid, px[i]), cell_row(f->grid, py[i]), 'o', color);
}

static void solids_draw(void *state, const Frame *f) {
    Solids *s = state;
    float kick = frame_pulse(f, ROLE_KICK, 0.18f, 0.12f);
    float t = f->still ? f->local * 0.5f : f->local;
    dust_stars(f, 0xc0be, 0.4f, 0.6f);
    for (int i = 0; i < s->count; i++) {
        float offset = s->count > 1 ? ((float)i - 0.5f) * 1.1f : 0;
        float scale = s->size * (1 + kick) * (s->count > 1 ? 0.7f : 1.0f);
        Rgbf color = i ? palette_accent(f->palette, 0) : rgbf_mix(palette_ramp(f->palette, 1.0f), palette_accent(f->palette, 2), 0.35f);
        float ax = t * s->spin_x + (float)i, ay = t * s->spin_y * (i ? -1.0f : 1.0f);
        int shape = (s->shape + i) % 3;
        if (shape == 0) solid(f, CUBE, 8, CUBE_EDGES, 12, scale * 0.8f, ax, ay, offset, color);
        if (shape == 1) solid(f, OCTA, 6, OCTA_EDGES, 12, scale * 1.2f, ax, ay, offset, color);
        if (shape == 2) solid(f, ICOSA, 12, ICOSA_EDGES, 30, scale * 0.62f, ax, ay, offset, color);
    }
    f->fx->glow = 0.5f;
}

/* ---------- Aurora over mountains, mirrored in a lake ---------- */

typedef struct {
    float base, amp, k1, k2, w1, w2, fold_k, seed, ground, ridge_seed;
} Aurora;

static void aurora_setup(void *state, Rng *r, const Frame *f) {
    Aurora *a = state;
    a->base = rng_between(r, 0.05f, 0.25f);
    a->amp = rng_between(r, 0.1f, 0.22f);
    a->k1 = rng_between(r, 0.8f, 1.6f);
    a->k2 = rng_between(r, 2.0f, 3.5f);
    a->w1 = rng_between(r, 0.08f, 0.18f);
    a->w2 = rng_between(r, 0.05f, 0.12f);
    a->fold_k = rng_between(r, 3.0f, 6.0f);
    a->seed = rng_between(r, 0, 50);
    a->ground = rng_between(r, -0.42f, -0.3f);
    a->ridge_seed = rng_between(r, 0, 90);
    (void)f;
}

static float aurora_glow(const Aurora *a, float x, float y, float t, float swell, float *above) {
    float drift = noise_fbm(x * 0.5f + a->seed + t * 0.03f, t * 0.01f) - 0.5f;
    float base = a->base + a->amp * msin(x * a->k1 + t * a->w1) + a->amp * 0.5f * msin(x * a->k2 - t * a->w2) + drift * 0.3f;
    float fold = 0.5f + 0.5f * msin(x * a->fold_k + drift * 6 + t * 0.25f);
    fold = fold * fold * fold;
    float rays = noise_value(x * 14 + a->seed, t * 0.06f);
    float h = y - base;
    *above = h;
    if (h < -0.03f || h > 0.95f) return 0;
    // A bright hem along the bottom, rays fading upward, brightest where the curtain folds toward us.
    float hem = h < 0.04f ? smoothstep(-0.03f, 0.0f, h) * 0.6f : 0;
    return (fold * 0.85f + 0.15f) * (mexp(-h * 2.4f) * (0.35f + 0.65f * rays) + hem) * (0.8f + swell);
}

static void aurora_draw(void *state, const Frame *f) {
    Aurora *a = state;
    Grid *g = f->grid;
    static const char rays[] = " .':;|!";
    float swell = frame_pulse(f, ROLE_PAD, 1.0f, 0.25f) + frame_pulse(f, ROLE_BELL, 0.7f, 0.2f);
    float t = f->local;
    int ground_row = cell_row(g, a->ground);
    dust_stars(f, 0xa707a, 0.03f, 0.85f);
    for (int col = 0; col < g->cols; col++) {
        float x = ((float)col + 0.5f) / (float)g->cols * 2 * g->aspect - g->aspect;
        for (int row = 0; row < ground_row && row < g->rows; row++) {
            float y = 1 - ((float)row + 0.5f) / (float)g->rows * 2, above;
            float glow = aurora_glow(a, x, y, t, swell, &above);
            if (glow < 0.1f) continue;
            Rgbf color = rgbf_mix(palette_ramp(f->palette, 0.5f + 0.5f * glow), palette_accent(f->palette, 0),
                                  smoothstep(0.15f, 0.7f, above));
            *grid_at(g, col, row) = (Cell){(uint8_t)rays[clampi((int)(glow * 7.5f), 1, 6)], rgb_from_f(color), 0};
        }
        // The lake below the mountains repeats the sky upside down, broken into ripples.
        for (int row = ground_row + 1; row < g->rows; row++) {
            float y = 1 - ((float)row + 0.5f) / (float)g->rows * 2, depth = a->ground - y, above;
            float mirror = a->ground + depth * 1.3f + 0.05f * msin(x * 9 + t * 1.5f + depth * 20);
            float glow = aurora_glow(a, x, mirror, t, swell, &above) * (0.55f - depth * 0.4f);
            float ripple = msin(x * 18 + depth * 40 - t * 2.2f);
            if (glow < 0.08f || ripple < -0.2f) continue;
            Rgbf color = frame_dim(f, rgbf_mix(palette_ramp(f->palette, 0.5f), palette_accent(f->palette, 0), 0.3f), 0.5f + glow);
            *grid_at(g, col, row) = (Cell){(uint8_t)(ripple > 0.6f ? '~' : '-'), rgb_from_f(color), 0};
        }
    }
    StrokePoint ridge_line[96];
    for (int i = 0; i < 96; i++) {
        float x = -g->aspect + (float)i / 95.0f * 2 * g->aspect;
        float peak = mabs(noise_fbm(x * 1.1f + a->ridge_seed, a->ridge_seed * 0.3f) - 0.5f) * 0.5f;
        ridge_line[i] = (StrokePoint){x, a->ground + peak, palette_ramp(f->palette, 0.3f)};
    }
    stroke_draw(f->stroker, ridge_line, 96, 0);
    // Clear the sky behind the mountains so the ridge reads as solid rock.
    for (int col = 0; col < g->cols; col++) {
        float x = ((float)col + 0.5f) / (float)g->cols * 2 * g->aspect - g->aspect;
        float peak = mabs(noise_fbm(x * 1.1f + a->ridge_seed, a->ridge_seed * 0.3f) - 0.5f) * 0.5f;
        for (int row = cell_row(g, a->ground + peak) + 1; row <= ground_row && row < g->rows; row++)
            if (row >= 0) grid_at(g, col, row)->glyph = ' ';
    }
    f->fx->glow = 0.55f;
}

/* ---------- A moonlit sea ---------- */

typedef struct {
    float horizon, moon_x, moon_y, moon_r, phase, swell;
} Sea;

static void sea_setup(void *state, Rng *r, const Frame *f) {
    Sea *s = state;
    s->horizon = rng_between(r, -0.18f, 0.02f);
    s->moon_x = rng_between(r, -0.7f, 0.7f) * f->grid->aspect;
    s->moon_y = rng_between(r, 0.4f, 0.65f);
    s->moon_r = rng_between(r, 0.14f, 0.22f);
    s->phase = rng_between(r, -0.9f, 0.9f);
    s->swell = rng_between(r, 0.5f, 1.0f);
}

static void sea_draw(void *state, const Frame *f) {
    Sea *s = state;
    Grid *g = f->grid;
    Canvas *c = f->canvas;
    canvas_clear(c);
    Rgbf pearl = palette_ramp(f->palette, 1.0f), glint = palette_accent(f->palette, 1);
    // The moon: a lit sphere whose terminator sits where the seed put the phase.
    float lx = s->phase, lz = msqrt(mmax(0, 1 - lx * lx));
    for (int j = 0; j < c->h; j++) {
        float y = (canvas_y(c, j) - s->moon_y) / s->moon_r;
        if (mabs(y) > 1.05f) continue;
        for (int i = 0; i < c->w; i++) {
            float x = (canvas_x(c, i) - s->moon_x) / s->moon_r, d2 = x * x + y * y;
            if (d2 >= 1) continue;
            float z = msqrt(1 - d2), lit = smoothstep(-0.05f, 0.25f, x * lx + z * lz);
            float crater = noise_fbm(x * 2 + 7, y * 2 + 3) - 0.5f;
            canvas_max(c, i, j, 0.14f + 0.5f * lit + crater * 0.35f * lit, rgbf_mix(palette_ramp(f->palette, 0.35f), pearl, lit));
        }
    }
    frame_resolve(f);
    dust_stars(f, 0x5ea, 0.0f, 0.8f);
    int horizon_row = cell_row(g, s->horizon);
    for (int row = 0; row <= horizon_row && row < g->rows; row++)
        for (int col = 0; col < g->cols; col++) {
            Cell *cell = grid_at(g, col, row);
            if (row == horizon_row) cell->glyph = ' ';
        }
    float glints = frame_pulse(f, ROLE_BELL, 0.6f, 1.0f) + frame_pulse(f, ROLE_PLUCK, 0.5f, 0.8f);
    float travel = frame_travel(f, 0.4f * s->swell, 0.3f);
    for (int row = horizon_row + 1; row < g->rows; row++) {
        float y = 1 - ((float)row + 0.5f) / (float)g->rows * 2, depth = (s->horizon - y) / (s->horizon + 1);
        float scale = 1 / (depth + 0.08f);
        for (int col = 0; col < g->cols; col++) {
            float x = ((float)col + 0.5f) / (float)g->cols * 2 * g->aspect - g->aspect;
            float wave = msin(x * 3.2f * scale * 0.25f + travel * (1 + depth) + noise_value(x * scale * 0.3f, depth * 9) * 4);
            float reflect = mabs(x - s->moon_x) < (0.04f + depth * 0.3f) * (0.6f + 0.4f * noise_value(x * 30, depth * 50 + f->local));
            int glyph = 0;
            Rgbf color = palette_ramp(f->palette, 0.25f + 0.45f * depth);
            if (reflect && wave > -0.3f) {
                glyph = wave > 0.5f ? '=' : wave > 0 ? '-' : ':';
                color = rgbf_mix(pearl, glint, 0.3f);
            } else if (wave > 0.82f - depth * 0.3f) {
                glyph = '~';
            } else if (wave > 0.45f && hash01((uint32_t)(row * 733 + col)) < 0.25f + depth * 0.5f) {
                glyph = '-';
                color = frame_dim(f, color, 0.7f);
            }
            if (glints > 0.05f && wave > 0.9f && hash01((uint32_t)(row * 31 + col * 17) ^ (uint32_t)(f->t * 8)) < glints * 0.08f) {
                glyph = '*';
                color = glint;
            }
            if (glyph) *grid_at(g, col, row) = (Cell){(uint8_t)glyph, rgb_from_f(color), 0};
        }
    }
    f->fx->glow = 0.5f;
}

/* ---------- Nebula ---------- */

typedef struct {
    float scale, drift_x, drift_y, seed;
} Nebula;

static void nebula_setup(void *state, Rng *r, const Frame *f) {
    Nebula *n = state;
    n->scale = rng_between(r, 0.7f, 1.3f);
    n->drift_x = rng_between(r, -0.04f, 0.04f);
    n->drift_y = rng_between(r, -0.02f, 0.02f);
    n->seed = rng_between(r, 0, 80);
    (void)f;
}

// Two veils of gas in different hues, drawn in a speckled dust alphabet so they read as glowing clouds.
static void nebula_draw(void *state, const Frame *f) {
    Nebula *n = state;
    Grid *g = f->grid;
    static const char dust[] = " .`,:;'+*";
    float breathe = frame_pulse(f, ROLE_PAD, 1.2f, 0.15f) + frame_pulse(f, ROLE_KICK, 0.3f, 0.08f);
    float zoom = n->scale * (1 + 0.08f * msin(f->local * 0.05f));
    for (int row = 0; row < g->rows; row++) {
        float y = (1 - ((float)row + 0.5f) / (float)g->rows * 2) * zoom;
        for (int col = 0; col < g->cols; col++) {
            float x = (((float)col + 0.5f) / (float)g->cols * 2 * g->aspect - g->aspect) * zoom;
            float u = x + n->drift_x * f->local, v = y + n->drift_y * f->local;
            float warp = noise_fbm(u * 0.8f + n->seed, v * 0.8f) * 1.5f;
            float a = smoothstep(0.45f, 0.8f, noise_fbm(u * 1.3f + warp, v * 1.3f + n->seed));
            float b = smoothstep(0.5f, 0.85f, noise_fbm(u * 1.9f - warp + 13, v * 1.9f + 7));
            float density = mmax(a, b * 0.85f) + breathe * (a + b);
            if (density < 0.1f) continue;
            float jitter = hash01((uint32_t)(row * 977 + col * 131)) - 0.5f;
            int level = clampi((int)(density * 8.5f + jitter * 1.2f), 1, 8);
            Rgbf color = rgbf_mix(palette_accent(f->palette, 0), palette_accent(f->palette, 2), b / (a + b + 0.001f));
            color = rgbf_mix(palette_ramp(f->palette, 0.3f), color, saturate(density * 1.3f));
            *grid_at(g, col, row) = (Cell){(uint8_t)dust[level], rgb_from_f(color), 0};
        }
    }
    dust_stars(f, 0xe8b1, 0.01f, 1.0f);
    f->fx->glow = 0.6f;
}

/* ---------- Constellations that light up with the melody ---------- */

#define STARS 24
#define CLUSTERS 3

typedef struct {
    float x[STARS], y[STARS];
    int8_t edge_a[STARS], edge_b[STARS];
    int edges;
    float band_tilt, band_offset;
    uint32_t salt;
} Constellation;

static void constellation_setup(void *state, Rng *r, const Frame *f) {
    Constellation *c = state;
    float aspect = f->grid->aspect;
    // Three star figures spread across the sky, each a hand-drawn chart grown nearest-first within its own stars.
    c->edges = 0;
    int per = STARS / CLUSTERS;
    for (int k = 0; k < CLUSTERS; k++) {
        float cx = ((float)k + 0.5f) / CLUSTERS * 1.6f * aspect - 0.8f * aspect + rng_between(r, -0.15f, 0.15f);
        float cy = rng_between(r, -0.35f, 0.45f);
        int first = k * per;
        for (int i = first; i < first + per; i++) {
            c->x[i] = cx + rng_between(r, -0.38f, 0.38f) * aspect * 0.6f;
            c->y[i] = cy + rng_between(r, -0.4f, 0.4f);
        }
        int in_tree[STARS] = {0};
        in_tree[first] = 1;
        for (int n = 1; n < per; n++) {
            float best = 1e9f;
            int ba = first, bb = first;
            for (int a = first; a < first + per; a++)
                for (int b = first; b < first + per; b++)
                    if (in_tree[a] && !in_tree[b]) {
                        float d = mhypot(c->x[a] - c->x[b], c->y[a] - c->y[b]);
                        if (d < best) {
                            best = d;
                            ba = a;
                            bb = b;
                        }
                    }
            in_tree[bb] = 1;
            c->edge_a[c->edges] = (int8_t)ba;
            c->edge_b[c->edges++] = (int8_t)bb;
        }
    }
    c->band_tilt = rng_between(r, -0.6f, 0.6f);
    c->band_offset = rng_between(r, -0.3f, 0.3f);
    c->salt = rng_u32(r);
}

// A star chart drawing itself: the Milky Way across the sky, three figures whose lines grow in over the shot (faster
// with every note), stars that flare at the notes that name them, and a shooting star now and then on the snare.
static void constellation_draw(void *state, const Frame *f) {
    Constellation *c = state;
    Grid *g = f->grid;
    dust_stars(f, c->salt, 0.004f, 0.8f);
    Rgbf faint = palette_ramp(f->palette, 0.35f), milk = palette_ramp(f->palette, 0.55f);
    for (int row = 0; row < g->rows; row++) {
        float y = 1 - ((float)row + 0.5f) / (float)g->rows * 2;
        for (int col = 0; col < g->cols; col++) {
            float x = ((float)col + 0.5f) / (float)g->cols * 2 * g->aspect - g->aspect;
            float d = y - (x * c->band_tilt + c->band_offset), band = mexp(-d * d * 9.0f);
            float dust = band * noise_fbm(x * 2.2f + (float)(c->salt % 53), y * 2.2f + f->t * 0.01f);
            uint32_t id = (uint32_t)(row * 977 + col * 131) ^ c->salt;
            if (dust > 0.42f && hash01(id) < (dust - 0.38f) * 2.2f) {
                static const char specks[] = ".,'`.:";
                int glyph = dust > 0.62f ? ':' : specks[hash_u32(id) % 6];
                put_cell_under(g, col, row, glyph, rgbf_mix(faint, milk, saturate((dust - 0.42f) * 3)));
            }
        }
    }
    int notes = 0;
    float start = f->t - f->local;
    static const Role sung[5] = {ROLE_LEAD, ROLE_BELL, ROLE_PLUCK, ROLE_ARP, ROLE_CHORD};
    for (int v = 0; v < 5; v++) notes += score_count(f->score, sung[v], start, f->t);
    // Every line is drawn by two thirds of the way through the shot; notes hurry it along.
    float drawn = f->local / (f->length * 0.66f) * (float)c->edges * 0.7f + (float)notes * 0.25f * f->react;
    Rgbf line = frame_dim(f, palette_ramp(f->palette, 0.75f), 0.65f);
    for (int e = 0; e < c->edges; e++) {
        float grow = saturate(drawn - (float)e);
        if (grow <= 0) break;
        int a = c->edge_a[e], b = c->edge_b[e];
        float bx = c->x[a] + (c->x[b] - c->x[a]) * grow, by = c->y[a] + (c->y[b] - c->y[a]) * grow;
        StrokePoint seg[2] = {{c->x[a], c->y[a], line}, {bx, by, line}};
        stroke_draw(f->stroker, seg, 2, 0);
    }
    float flare[STARS] = {0};
    for (int v = 0; v < 5 && f->react > 0; v++) {
        const ScoreEvent *ev[16];
        int n = recent_notes(f->score, sung[v], f->t, 0.9f, ev, 16);
        for (int i = 0; i < n; i++) {
            int star = (int)((unsigned)(ev[i]->note * 7 + v * 5) % STARS);
            flare[star] = mmax(flare[star], ev[i]->velocity * mexp(-(f->t - ev[i]->time) / 0.35f) * frame_bright(f));
        }
    }
    float kick = frame_pulse(f, ROLE_KICK, 0.2f, 0.4f);
    Rgbf white = palette_ramp(f->palette, 1.0f), gold = palette_accent(f->palette, 1);
    for (int i = 0; i < STARS; i++) {
        float tw = (0.65f + 0.35f * msin(f->t * 1.7f + (float)i * 2.1f)) * (1 + kick);
        int col = cell_col(g, c->x[i]), row = cell_row(g, c->y[i]);
        Rgbf color = rgbf_mix(frame_dim(f, white, tw), gold, saturate(flare[i] * 1.5f));
        put_cell(g, col, row, flare[i] > 0.25f ? '*' : '+', color);
        if (flare[i] > 0.15f) {
            Rgbf ray = frame_dim(f, color, 0.4f + flare[i] * 0.6f);
            put_cell(g, col - 1, row, '-', ray);
            put_cell(g, col + 1, row, '-', ray);
            if (flare[i] > 0.45f) {
                put_cell(g, col, row - 1, '|', ray);
                put_cell(g, col, row + 1, '|', ray);
            }
        }
    }
    if (f->react > 0) {
        const ScoreEvent *hits[8];
        int n = recent_notes(f->score, ROLE_SNARE, f->t, 0.7f, hits, 8);
        for (int i = 0; i < n; i++) {
            uint32_t id = (uint32_t)(hits[i]->time * 1000.0f) ^ c->salt;
            if (hash01(id) > 0.3f) continue;
            float age = f->t - hits[i]->time, x0 = (hash01(id + 1) * 1.4f - 0.4f) * g->aspect, y0 = 0.4f + hash01(id + 2) * 0.5f;
            for (int k = 0; k < 6; k++) {
                float at = age - (float)k * 0.03f;
                if (at < 0) continue;
                float x = x0 - at * 1.6f, y = y0 - at * 0.7f;
                put_cell(g, cell_col(g, x), cell_row(g, y), k ? (k < 3 ? '-' : '.') : '*',
                         frame_dim(f, white, (1 - age / 0.7f) * (1 - (float)k * 0.15f) * frame_bright(f)));
            }
        }
    }
    f->fx->glow = 0.6f;
}

/* ---------- Cracktro: copper bars behind a sine scroller ---------- */

typedef struct {
    int bars, font;
    float bar_speed, scroll_speed, wave;
} Cracktro;

static void cracktro_setup(void *state, Rng *r, const Frame *f) {
    Cracktro *k = state;
    k->bars = rng_range(r, 3, 6);
    k->font = rng_chance(r, 50) ? FIGLET_BIG : FIGLET_STANDARD;
    k->bar_speed = rng_between(r, 0.8f, 1.6f);
    k->scroll_speed = rng_between(r, 10, 16);
    k->wave = rng_between(r, 0.12f, 0.25f);
    (void)f;
}

static void cracktro_draw(void *state, const Frame *f) {
    Cracktro *k = state;
    Grid *g = f->grid;
    // A parallax field of streaking stars, the cracktro classic.
    for (int s = 0; s < g->rows * 3; s++) {
        uint32_t id = 0xc4ac + (uint32_t)s * 2654435761u;
        float layer = 1 + (float)(id % 3), fx = hash01(id) * (float)g->cols - f->local * layer * 6;
        fx -= mfloor(fx / (float)g->cols) * (float)g->cols;
        put_cell(g, (int)fx, (int)(hash01(id + 1) * (float)g->rows), layer > 2 ? '-' : '.',
                 frame_dim(f, palette_ramp(f->palette, 0.4f + layer * 0.2f), 0.4f + layer * 0.2f));
    }
    float kick = frame_pulse(f, ROLE_KICK, 0.2f, 1.0f);
    for (int b = 0; b < k->bars; b++) {
        float center = (0.5f + 0.38f * msin(f->local * k->bar_speed + (float)b * 0.9f)) * (float)g->rows;
        Rgbf color = palette_accent(f->palette, b);
        for (int d = -2; d <= 2; d++) {
            int row = (int)center + d;
            if (row < 0 || row >= g->rows) continue;
            float shade = 1 - (float)(d < 0 ? -d : d) * 0.3f;
            for (int col = 0; col < g->cols; col++)
                put_cell(g, col, row, d == 0 ? '=' : '-', frame_dim(f, color, shade * (0.55f + 0.3f * kick)));
        }
    }
    const FigletFont *font = figlet_font((FigletId)k->font);
    if (!font) return;
    static char art[FIGLET_MAX_HEIGHT * 512];
    static char text[96];
    snprintf(text, sizeof text, "%s  *  ASCII ODYSSEY  *  ", f->score->info->title);
    for (char *p = text; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);
    int w = figlet_render(font, text, art, 512);
    if (w <= 0) return;
    int scroll = (int)(f->local * k->scroll_speed);
    int base = (g->rows - font->height) / 2;
    for (int col = 0; col < g->cols; col++) {
        int x = ((col + scroll) % w + w) % w;
        int lift = (int)(msin(((float)col * 0.12f) + f->local * 2.5f) * k->wave * (float)g->rows);
        for (int y = 0; y < font->height; y++) {
            char ch = art[y * 512 + x];
            if (ch == ' ') continue;
            Rgbf color = flow_color(f->palette, (float)col / (float)g->cols * 0.6f + f->local * 0.1f);
            put_cell(g, col, base + y + lift, ch, rgbf_mix(color, palette_ramp(f->palette, 1.0f), (float)y / (float)font->height * 0.5f));
        }
    }
    f->fx->glow = 0.5f;
}


/* ---------- Rain on a night window ---------- */

typedef struct {
    float x[16], y[16], radius[16], hue[16], twinkle[16];
    int lights, pane;
    float pane_x, pane_y, pour;
    uint32_t salt;
} Rain;

static void rain_setup(void *state, Rng *r, const Frame *f) {
    Rain *w = state;
    w->lights = rng_range(r, 9, 17);
    for (int i = 0; i < w->lights; i++) {
        w->x[i] = rng_between(r, -1, 1) * f->grid->aspect;
        w->y[i] = rng_between(r, -0.95f, 0.55f);
        w->radius[i] = rng_between(r, 0.05f, 0.2f);
        w->hue[i] = rng_float(r);
        w->twinkle[i] = rng_float(r) * TAU_F;
    }
    w->pane = rng_chance(r, 60);
    w->pane_x = rng_between(r, -0.4f, 0.4f) * f->grid->aspect;
    w->pane_y = rng_between(r, -0.25f, 0.35f);
    w->pour = rng_between(r, 0.7f, 1.3f);
    w->salt = rng_u32(r);
}

// An out-of-focus light: brightest in the middle and fading to nothing at its rim, with no edge for the matcher to trace.
// The finest grid has 5-pixel cells.
#define CORNERS_MAX ((FB_W / 5 + 1) * (FB_H / 12 + 1))

void canvas_from_corners(const Frame *f, CornerFn fn, const void *ctx) {
    static float ink[CORNERS_MAX];
    static Rgbf color[CORNERS_MAX];
    const Grid *g = f->grid;
    Canvas *c = f->canvas;
    int w = g->cols + 1, h = g->rows + 1;
    if (w * h > CORNERS_MAX) return;
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            fn(ctx, f, (float)col / (float)g->cols * 2 * g->aspect - g->aspect, 1 - (float)row / (float)g->rows * 2,
               &ink[row * w + col], &color[row * w + col]);
    // Cells with no ink at any corner are skipped whole, and each cell takes one color: the resolve averages the
    // colors of a cell's samples anyway.
    for (int row = 0; row < g->rows; row++)
        for (int col = 0; col < g->cols; col++) {
            int k = row * w + col;
            float k00 = ink[k], k10 = ink[k + 1], k01 = ink[k + w], k11 = ink[k + w + 1];
            if (k00 <= 0.01f && k10 <= 0.01f && k01 <= 0.01f && k11 <= 0.01f) continue;
            Rgbf tint = rgbf_scale(rgbf_add(rgbf_add(color[k], color[k + 1]), rgbf_add(color[k + w], color[k + w + 1])), 0.25f);
            for (int y = 0; y < 4; y++) {
                float v = ((float)y + 0.5f) / 4, left = mixf(k00, k01, v), right = mixf(k10, k11, v);
                for (int x = 0; x < 2; x++) {
                    float level = mixf(left, right, ((float)x + 0.5f) / 2);
                    if (level > 0.01f) canvas_max(c, col * 2 + x, row * 4 + y, level, tint);
                }
            }
        }
}

void moon_disc(const Frame *f, float x, float y, float radius, Rgbf color, uint32_t salt, int behind) {
    Grid *g = f->grid;
    float reach = radius * 1.7f;
    int c0 = cell_col(g, x - reach), c1 = cell_col(g, x + reach), r0 = cell_row(g, y + reach), r1 = cell_row(g, y - reach);
    for (int row = r0; row <= r1; row++)
        for (int col = c0; col <= c1; col++) {
            float cx = ((float)col + 0.5f) / (float)g->cols * 2 * g->aspect - g->aspect, cy = 1 - ((float)row + 0.5f) / (float)g->rows * 2;
            float d = mhypot(cx - x, cy - y) / radius;
            uint32_t id = (uint32_t)(row * 977 + col * 131) ^ salt;
            if (d < 0.9f) {
                // The face: mostly bright fill, with a few darker crater marks.
                float crater = noise_value(cx * 9 + (float)(salt % 61), cy * 9);
                int glyph = crater > 0.72f ? 'o' : crater > 0.62f ? ':' : '#';
                (behind ? put_cell_under : put_cell)(g, col, row, glyph, frame_dim(f, color, crater > 0.62f ? 0.75f : 1.0f - d * 0.15f));
            } else if (d < 1.7f && hash01(id) < 0.35f * (1.7f - d)) {
                put_cell_under(g, col, row, '.', frame_dim(f, color, 0.45f * (1.7f - d)));
            }
        }
    if (behind) return;
    StrokePoint rim[48];
    for (int i = 0; i < 48; i++) {
        float a = (float)i / 48.0f * TAU_F;
        rim[i] = (StrokePoint){x + mcos(a) * radius * 0.95f, y + msin(a) * radius * 0.95f, color};
    }
    stroke_draw(f->stroker, rim, 48, 1);
}

void bokeh(Canvas *c, float x, float y, float radius, float level, Rgbf color) {
    int i0 = (int)((x - radius + c->aspect) / c->sx), i1 = (int)((x + radius + c->aspect) / c->sx) + 1;
    int j0 = (int)((1 - (y + radius)) / c->sy), j1 = (int)((1 - (y - radius)) / c->sy) + 1;
    for (int j = j0 < 0 ? 0 : j0; j <= j1 && j < c->h; j++)
        for (int i = i0 < 0 ? 0 : i0; i <= i1 && i < c->w; i++) {
            float d = mhypot(canvas_x(c, i) - x, canvas_y(c, j) - y) / radius;
            if (d < 1) canvas_max(c, i, j, level * (1 - d * d), color);
        }
}

// One drop on the glass: a bead with its wet trail above it.
static void rain_drop(const Frame *f, float x, float y, float trail, Rgbf color) {
    Grid *g = f->grid;
    int col = cell_col(g, x), row = cell_row(g, y), length = (int)(trail * (float)g->rows * 0.5f);
    put_cell(g, col, row, 'o', color);
    for (int k = 1; k <= length; k++) {
        float fade = 1 - (float)k / (float)(length + 1);
        put_cell(g, col, row - k, k == 1 ? '|' : k < length ? ':' : '.', frame_dim(f, color, 0.25f + 0.6f * fade));
    }
}

static void rain_draw(void *state, const Frame *f) {
    Rain *w = state;
    Grid *g = f->grid;
    Canvas *c = f->canvas;
    canvas_clear(c);
    // City lights out of focus behind the glass: they bloom on the kick and turn color with each chord.
    const ChordMark *chord = score_chord(f->score, f->t);
    float turn = chord ? (float)chord->chord.root_pc / 12.0f : 0, bloom = frame_pulse(f, ROLE_KICK, 0.25f, 0.35f);
    for (int i = 0; i < w->lights; i++) {
        float hue = mfract(w->hue[i] + turn * 0.5f);
        Rgbf color = hue < 0.5f ? palette_accent(f->palette, (int)(hue * 6)) : palette_ramp(f->palette, 0.45f + hue * 0.5f);
        float level = 0.35f + 0.15f * msin(f->t * 0.4f + w->twinkle[i]) + bloom;
        bokeh(c, w->x[i], w->y[i], w->radius[i] * (1.4f + bloom * 0.3f), level, color);
    }
    frame_resolve(f);
    // Drops bead and slide in a steady rain, and every hi-hat adds one more. Each drop keeps its own moment; the
    // music's energy only decides how many of them fall.
    float spacing = 0.12f / w->pour, life = 6.0f;
    Rgbf wet = palette_ramp(f->palette, 0.95f);
    for (int k = (int)((f->t - life) / spacing); k <= (int)(f->t / spacing); k++) {
        if (k < 0) continue;
        uint32_t id = w->salt + (uint32_t)k * 2654435761u;
        if (hash01(id + 5) > 0.25f + 0.6f * f->energy) continue;
        float age = f->t - (float)k * spacing, speed = 0.12f + 0.3f * hash01(id + 2);
        float x = (hash01(id) * 2 - 1) * g->aspect, y = 1.05f - hash01(id + 1) * 0.5f - speed * age;
        if (y > -1.1f) rain_drop(f, x, y, mmin(age * speed, 0.35f), wet);
    }
    const ScoreEvent *hats[32];
    int n = recent_notes(f->score, ROLE_HAT, f->t, 4.0f, hats, 32);
    for (int i = 0; i < n; i++) {
        uint32_t id = (uint32_t)(hats[i]->time * 1000.0f) * 2246822519u + w->salt;
        float age = f->t - hats[i]->time, speed = 0.25f + 0.25f * hash01(id + 1);
        float x = (hash01(id) * 2 - 1) * g->aspect, y = 0.95f - hash01(id + 2) * 0.4f - speed * age;
        rain_drop(f, x, y, mmin(age * speed, 0.3f), rgbf_mix(wet, palette_accent(f->palette, 1), 0.3f));
    }
    // The window's cross bars stand in front of everything.
    if (w->pane) {
        Rgbf frame = palette_ramp(f->palette, 0.3f);
        int col = cell_col(g, w->pane_x), row = cell_row(g, w->pane_y);
        for (int y = 0; y < g->rows; y++) put_cell(g, col, y, '|', frame);
        for (int x = 0; x < g->cols; x++) put_cell(g, x, row, '=', frame);
        put_cell(g, col, row, '+', frame);
    }
    f->fx->glow = 0.55f;
}

/* ---------- Snowfall over a little village ---------- */

typedef struct {
    int houses, x[8], w[8], h[8], ground;
    float wind, moon_x, moon_y, hills;
    uint32_t salt;
} Snow;

static void snow_setup(void *state, Rng *r, const Frame *f) {
    Snow *s = state;
    Grid *g = f->grid;
    s->ground = g->rows - rng_range(r, 4, 8);
    s->houses = 0;
    for (int x = rng_range(r, 1, 8); x < g->cols - 13 && s->houses < 8; x += rng_range(r, 14, 26)) {
        s->x[s->houses] = x;
        s->w[s->houses] = rng_range(r, 8, 13);
        s->h[s->houses] = rng_range(r, 3, 5);
        s->houses++;
    }
    s->wind = rng_between(r, -0.05f, 0.05f);
    s->moon_x = rng_between(r, -0.7f, 0.7f) * f->grid->aspect;
    s->moon_y = rng_between(r, 0.45f, 0.75f);
    s->hills = rng_between(r, 0, 50);
    s->salt = rng_u32(r);
}

// A cottage: a snow-capped roof of slashes over plain walls, with two windows the caller lights.
static void cottage(const Frame *f, int x, int w, int h, int ground, const float lit[2], Rgbf wall, Rgbf warm, Rgbf snow) {
    Grid *g = f->grid;
    int top = ground - h;
    for (int k = 0;; k++) {
        int y = top - 1 - k, l = x + k, r = x + w - 1 - k;
        if (l >= r) {
            if (l == r) put_cell(g, l, y, '^', snow);
            break;
        }
        put_cell(g, l, y, '/', snow);
        put_cell(g, r, y, '\\', snow);
        for (int i = l + 1; i < r; i++) put_cell(g, i, y, k ? ' ' : '_', wall);
    }
    for (int y = top; y < ground; y++) {
        put_cell(g, x, y, '|', wall);
        put_cell(g, x + w - 1, y, '|', wall);
        for (int i = x + 1; i < x + w - 1; i++) put_cell(g, i, y, ' ', wall);
    }
    int row = top + (h - 1) / 2;
    for (int k = 0; k < 2; k++)
        put_cell(g, k ? x + w - 3 : x + 2, row, '#', rgbf_mix(frame_dim(f, wall, 0.5f), warm, lit[k]));
}

static void snow_draw(void *state, const Frame *f) {
    Snow *s = state;
    Grid *g = f->grid;
    Canvas *c = f->canvas;
    canvas_clear(c);
    // A soft winter moon, and a line of hills behind the village.
    bokeh(c, s->moon_x, s->moon_y, 0.2f, 0.8f, palette_ramp(f->palette, 0.9f));
    bokeh(c, s->moon_x, s->moon_y, 0.42f, 0.14f, palette_ramp(f->palette, 0.5f));
    frame_resolve(f);
    dust_stars(f, 0x5a0f, 0.0f, 0.6f);
    StrokePoint ridge[64];
    float ground_y = 1 - ((float)s->ground + 0.5f) / (float)g->rows * 2;
    for (int i = 0; i < 64; i++) {
        float x = -g->aspect + (float)i / 63.0f * 2 * g->aspect;
        float rise = smoothstep(0.3f, 0.7f, noise_fbm(x * 0.5f + s->hills, 3));
        ridge[i] = (StrokePoint){x, ground_y + 0.1f + 0.4f * rise, palette_ramp(f->palette, 0.35f)};
    }
    stroke_draw(f->stroker, ridge, 64, 0);
    // The melody lights the village window by window: each note warms a window, which cools again.
    float lit[16] = {0};
    int windows = s->houses * 2;
    static const Role singers[3] = {ROLE_LEAD, ROLE_BELL, ROLE_PLUCK};
    for (int v = 0; v < 3 && windows; v++) {
        const ScoreEvent *notes[24];
        int n = recent_notes(f->score, singers[v], f->t, 3.0f, notes, 24);
        for (int i = 0; i < n; i++) {
            float glow = notes[i]->velocity * mexp(-(f->t - notes[i]->time) / 1.2f);
            int w = notes[i]->note % windows;
            if (glow > lit[w]) lit[w] = glow;
        }
    }
    Rgbf wall = palette_ramp(f->palette, 0.6f), warm = palette_accent(f->palette, 1), snow = palette_ramp(f->palette, 0.95f);
    for (int i = 0; i < s->houses; i++) cottage(f, s->x[i], s->w[i], s->h[i], s->ground, lit + 2 * i, wall, warm, snow);
    for (int x = 0; x < g->cols; x++) {
        put_cell(g, x, s->ground, hash01((uint32_t)x * 977u + s->salt) < 0.3f ? '~' : '_', snow);
        for (int y = s->ground + 1; y < g->rows; y++)
            if (hash01((uint32_t)(y * 131 + x) ^ s->salt) < 0.08f) put_cell(g, x, y, '.', frame_dim(f, snow, 0.6f));
    }
    // Three depths of flakes drift down and sway; a kick sends a little gust through them.
    float travel = frame_travel(f, 0.6f, 0.6f), gust = frame_pulse(f, ROLE_KICK, 0.4f, 0.04f);
    for (int layer = 0; layer < 3; layer++) {
        int count = g->cols * g->rows / (60 - layer * 15);
        float speed = 0.05f + 0.07f * (float)layer;
        Rgbf flake = frame_dim(f, snow, 0.45f + 0.25f * (float)layer);
        for (int k = 0; k < count; k++) {
            uint32_t id = s->salt + (uint32_t)(layer * 100003 + k) * 2654435761u;
            float y = 1.05f - mfract(hash01(id + 1) + travel * speed / 2.2f) * 2.2f;
            float x = (hash01(id) * 2 - 1) * g->aspect + s->wind * f->t + gust * (float)(layer + 1) +
                      0.03f * (float)(layer + 1) * msin(f->t * (0.5f + hash01(id + 2)) + hash01(id + 3) * TAU_F);
            x -= mfloor((x + g->aspect) / (2 * g->aspect)) * 2 * g->aspect;
            int glyph = layer == 0 ? '.' : layer == 1 ? (hash01(id + 4) < 0.5f ? '*' : '+') : '*';
            if (layer == 2)
                put_cell(g, cell_col(g, x), cell_row(g, y), glyph, flake);
            else
                put_cell_under(g, cell_col(g, x), cell_row(g, y), glyph, flake);
        }
    }
    f->fx->glow = 0.5f;
}

// Weights by mood, in the order keygen, ambient, synthwave, lo-fi, trance, jungle, music box, dub, dubstep.
#define W(k, a, s, l, t, j, m, d, x) {k, a, s, l, t, j, m, d, x}

const SceneKind scene_kinds[] = {
    {"warp", W(2, 1, 2, 0, 3, 3, 0, 0, 1), 0.75f, 1, 0, warp_setup, warp_draw},
    {"tunnel", W(2, 0, 2, 0, 3, 3, 0, 1, 2), 0.85f, 1, 1, tunnel_setup, tunnel_draw},
    {"plasma", W(3, 0, 1, 1, 1, 1, 0, 2, 1), 0.6f, 1, 1, plasma_setup, plasma_draw},
    {"sunset", W(0, 0, 3, 1, 0, 0, 0, 0, 0), 0.5f, 0, 0, sunset_setup, sunset_draw},
    {"solids", W(2, 0, 2, 0, 2, 0, 0, 2, 2), 0.6f, 1, 0, solids_setup, solids_draw},
    {"aurora", W(0, 3, 0, 1, 1, 0, 2, 1, 0), 0.25f, 0, 0, aurora_setup, aurora_draw},
    {"sea", W(0, 0, 0, 0, 0, 0, 0, 0, 0), 0.3f, 0, 0, sea_setup, sea_draw},
    {"nebula", W(0, 3, 0, 0, 2, 1, 1, 2, 1), 0.3f, 0, 0, nebula_setup, nebula_draw},
    {"constellations", W(0, 2, 0, 1, 0, 0, 3, 1, 0), 0.35f, 1, 0, constellation_setup, constellation_draw},
    {"cracktro", W(3, 0, 0, 0, 0, 0, 0, 0, 0), 0.8f, 1, 0, cracktro_setup, cracktro_draw},
    {"rain", W(0, 1, 0, 3, 0, 2, 0, 2, 0), 0.4f, 1, 0, rain_setup, rain_draw},
    {"snow", W(0, 2, 0, 2, 0, 0, 3, 0, 0), 0.2f, 1, 0, snow_setup, snow_draw},
    {"kaleidoscope", W(1, 1, 1, 0, 3, 0, 1, 1, 1), 0.7f, 1, 1, kaleidoscope_setup, kaleidoscope_draw},
    {"spectrum", W(2, 0, 2, 1, 1, 1, 0, 1, 3), 0.7f, 1, 0, spectrum_setup, spectrum_draw},
    {"ripples", W(0, 2, 0, 1, 1, 0, 1, 2, 0), 0.45f, 1, 0, ripples_setup, ripples_draw},
    {"fireworks", W(1, 0, 2, 0, 2, 1, 2, 0, 1), 0.75f, 1, 0, fireworks_setup, fireworks_draw},
    {"lasers", W(1, 0, 2, 0, 3, 1, 0, 0, 3), 0.9f, 1, 0, lasers_setup, lasers_draw},
    {"lissajous", W(2, 1, 1, 0, 1, 0, 0, 2, 1), 0.5f, 1, 0, lissajous_setup, lissajous_draw},
    {"skyline", W(0, 0, 2, 3, 0, 2, 0, 2, 1), 0.45f, 1, 0, skyline_setup, skyline_draw},
    {"digital rain", W(3, 0, 1, 0, 1, 2, 0, 1, 2), 0.7f, 1, 0, digital_rain_setup, digital_rain_draw},
    {"checker", W(2, 0, 3, 0, 2, 1, 0, 0, 2), 0.85f, 1, 0, checker_setup, checker_draw},
    {"petals", W(0, 2, 0, 2, 0, 0, 2, 0, 0), 0.25f, 0, 0, petals_setup, petals_draw},
    {"lava", W(1, 1, 1, 2, 0, 0, 0, 3, 1), 0.35f, 0, 1, lava_setup, lava_draw},
    {"clouds", W(0, 3, 0, 1, 0, 0, 2, 1, 0), 0.15f, 0, 1, clouds_setup, clouds_draw},
    {"lanterns", W(0, 2, 0, 2, 0, 0, 3, 0, 0), 0.2f, 0, 0, lanterns_setup, lanterns_draw},
    {"jellyfish", W(0, 2, 0, 1, 1, 0, 0, 3, 1), 0.3f, 0, 0, jellyfish_setup, jellyfish_draw},
    {"terrain", W(1, 1, 3, 0, 2, 0, 0, 1, 1), 0.45f, 0, 0, terrain_setup, terrain_draw},
    {"galaxy", W(1, 2, 0, 0, 2, 1, 1, 2, 1), 0.35f, 0, 0, galaxy_setup, galaxy_draw},
    {"fireflies", W(0, 2, 0, 2, 0, 0, 3, 0, 0), 0.2f, 0, 0, fireflies_setup, fireflies_draw},
    {"ocean", W(0, 3, 1, 2, 0, 0, 1, 2, 0), 0.25f, 0, 0, ocean_setup, ocean_draw},
    {"fire", W(2, 0, 1, 0, 1, 2, 0, 0, 3), 0.85f, 1, 0, fire_setup, fire_draw},
    {"twister", W(3, 0, 1, 0, 2, 0, 0, 0, 1), 0.7f, 1, 0, twister_setup, twister_draw},
    {"rotozoom", W(3, 0, 2, 0, 2, 1, 0, 0, 1), 0.75f, 1, 0, rotozoom_setup, rotozoom_draw},
    {"vector balls", W(2, 0, 1, 0, 3, 0, 0, 1, 1), 0.6f, 1, 0, balls_setup, balls_draw},
    {"torus", W(1, 0, 2, 0, 2, 0, 0, 1, 2), 0.6f, 1, 0, torus_setup, torus_draw},
    {"spirograph", W(1, 1, 0, 0, 2, 0, 3, 1, 0), 0.45f, 1, 0, spirograph_setup, spirograph_draw},
    {"turntable", W(0, 0, 1, 3, 0, 0, 0, 3, 1), 0.3f, 0, 0, turntable_setup, turntable_draw},
    {"cassette", W(1, 0, 2, 3, 0, 0, 0, 1, 0), 0.3f, 0, 0, cassette_setup, cassette_draw},
    {"music box comb", W(0, 1, 0, 1, 0, 0, 3, 0, 0), 0.4f, 1, 0, cylinder_setup, cylinder_draw},
    {"mobile", W(0, 2, 0, 1, 0, 0, 3, 0, 0), 0.2f, 0, 0, mobile_setup, mobile_draw},
    {"gears", W(1, 0, 0, 1, 0, 0, 2, 1, 0), 0.35f, 0, 0, gears_setup, gears_draw},
    {"canopy", W(0, 2, 0, 1, 0, 3, 0, 1, 0), 0.3f, 0, 0, canopy_setup, canopy_draw},
    {"light trails", W(0, 0, 3, 0, 1, 2, 0, 1, 2), 0.8f, 1, 0, trails_setup, trails_draw},
    {"speakers", W(0, 0, 0, 1, 0, 2, 0, 3, 3), 0.75f, 1, 0, speakers_setup, speakers_draw},
    {"lightning", W(0, 0, 0, 0, 1, 2, 0, 1, 3), 0.85f, 1, 1, lightning_setup, lightning_draw},
    {"dunes", W(0, 2, 1, 1, 0, 0, 0, 2, 0), 0.25f, 0, 0, dunes_setup, dunes_draw},
    {"kelp", W(0, 2, 0, 1, 0, 0, 1, 2, 0), 0.25f, 0, 0, kelp_setup, kelp_draw},
    {"meteors", W(0, 2, 1, 1, 1, 0, 2, 0, 0), 0.3f, 0, 0, meteors_setup, meteors_draw},
};
const int scene_kind_count = (int)(sizeof scene_kinds / sizeof scene_kinds[0]);

int scene_in_use(const SceneKind *k) {
    for (int m = 0; m < MOOD_COUNT; m++)
        if (k->weights[m]) return 1;
    return 0;
}
