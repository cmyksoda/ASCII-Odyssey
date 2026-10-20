// SPDX-License-Identifier: GPL-3.0-only
// Op art at full tilt: a checkerboard vortex zooming forever inward, a hypnotic spiral, a rubber checkerboard, a
// kaleidoscope tiled across the whole screen, moire rings, flipping Truchet tiles and a twisting tower of squares.
// Kicks drive their motion and chords turn their colors.
#include "mathx.h"
#include "noise.h"
#include "scene.h"

static float cell_x(const Grid *g, int col) {
    return ((float)col + 0.5f) / (float)g->cols * 2 * g->aspect - g->aspect;
}

static float cell_y(const Grid *g, int row) {
    return 1 - ((float)row + 0.5f) / (float)g->rows * 2;
}

// The palette's accents turned by the chord, easing from the last chord's over a quarter of a second (a whole second
// for viewers who asked for calmer pictures), so the screen never snaps from one set of colors to the next.
static void chord_inks(const Frame *f, Rgbf inks[3]) {
    const ChordMark *now = score_chord(f->score, f->t);
    for (int k = 0; k < 3; k++) inks[k] = palette_accent(f->palette, k);
    if (!now) return;
    int before = now > f->score->info->chords ? now[-1].chord.root_pc : now->chord.root_pc;
    float ease = smoothstep(0.0f, f->calm ? 1.0f : 0.25f, f->t - (float)now->time);
    for (int k = 0; k < 3; k++)
        inks[k] = rgbf_mix(palette_accent(f->palette, before + k), palette_accent(f->palette, now->chord.root_pc + k), ease);
}

// Pictures made of nothing but stripes and checks are the kind that can trouble light-sensitive eyes, so viewers who
// asked for calmer pictures get them at lower contrast, and viewers who asked for less motion get them at half speed.
static float op_contrast(const Frame *f) {
    return f->calm ? 0.6f : 1.0f;
}

static float op_pace(const Frame *f) {
    return f->still ? 0.5f : 1.0f;
}

// The glyph for a line running in direction (dx, dy), with y up the screen.
static int slope_glyph(float dx, float dy) {
    if (mabs(dy) < 0.4f * mabs(dx)) return '-';
    if (mabs(dx) < 0.4f * mabs(dy)) return '|';
    return (dx > 0) == (dy > 0) ? '/' : '\\';
}

/* ---------- Vortex ---------- */

typedef struct {
    int sectors;
    float rings, twist, spin, speed;
} Vortex;

void vortex_setup(void *state, Rng *r, const Frame *f) {
    Vortex *v = state;
    static const int sectors[3] = {8, 12, 16};
    v->sectors = sectors[rng_range(r, 0, 3)];
    v->rings = rng_between(r, 2.5f, 4.0f);
    // A whole number of tiles' shear per turn keeps the board seamless: even, since the sectors are.
    v->twist = 2.0f * (float)rng_range(r, 0, 3);
    v->spin = rng_between(r, 0.08f, 0.2f) * (rng_chance(r, 50) ? 1 : -1);
    v->speed = rng_between(r, 0.6f, 1.1f);
    (void)f;
}

// A checkerboard wrapped round a point and zooming into it forever: tiles swell as they reach the edge and dwindle to
// nothing in the dark eye at the heart, spiralling when the board is twisted. Kicks lunge it forward.
void vortex_draw(void *state, const Frame *f) {
    Vortex *v = state;
    Grid *g = f->grid;
    Rgbf inks[3];
    chord_inks(f, inks);
    float pace = op_pace(f), contrast = op_contrast(f);
    float zoom = (frame_travel(f, v->speed * 0.5f, v->speed) + frame_surge(f, ROLE_KICK, 0.2f, 0.8f)) * pace;
    float turn = f->local * v->spin * pace, kick = frame_pulse(f, ROLE_KICK, 0.2f, 1.0f);
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), r = mhypot(x, y);
            if (r < 0.04f) continue;
            float around = (matan2(y, x) + turn) / TAU_F * (float)v->sectors;
            float out = mlog2(r) * v->rings - zoom + around * v->twist / (float)v->sectors;
            int parity = (mfloor_i(out) + mfloor_i(around)) & 1, edge = mfract(out) < 0.12f || mfract(around) < 0.12f;
            float fade = smoothstep(0.04f, 0.3f, r) * contrast;
            if (parity)
                put_cell(g, col, row, edge ? '+' : '#', frame_dim(f, inks[0], fade * (0.85f + 0.3f * kick)));
            else
                put_cell(g, col, row, edge ? '.' : ':', frame_dim(f, inks[1], fade * 0.75f));
        }
    }
    f->fx->glow = 0.55f + 0.3f * kick;
}

/* ---------- Hypnotic spiral ---------- */

typedef struct {
    int arms, colors;
    float tight, speed;
} Spiral;

void spiral_setup(void *state, Rng *r, const Frame *f) {
    Spiral *s = state;
    s->arms = rng_range(r, 1, 5);
    s->colors = rng_chance(r, 50) ? 2 : 3;
    s->tight = rng_between(r, 3.0f, 6.0f);
    s->speed = rng_between(r, 0.5f, 1.0f) * (rng_chance(r, 50) ? 1 : -1);
    (void)f;
}

// A hypnotic spiral: arms of color winding out from the middle and streaming endlessly outward (or in), each band
// shading from dark edges to a bright middle. Kicks hurl the stream along.
void spiral_draw(void *state, const Frame *f) {
    Spiral *s = state;
    Grid *g = f->grid;
    static const char ramp[] = " .:-=+*#";
    Rgbf inks[3];
    chord_inks(f, inks);
    float sign = s->speed > 0 ? 1.0f : -1.0f;
    float flow = (frame_travel(f, s->speed * 0.6f, s->speed) + sign * frame_surge(f, ROLE_KICK, 0.2f, 0.6f)) * op_pace(f);
    float contrast = op_contrast(f);
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), r = mhypot(x, y);
            float band = r * s->tight + matan2(y, x) / TAU_F * (float)s->arms - flow;
            int k = mfloor_i(band);
            float level = msin(mfract(band) * PI_F) * smoothstep(0.0f, 0.12f, r);
            int i = (int)(level * 7.99f);
            if (i <= 0) continue;
            put_cell(g, col, row, ramp[i], frame_dim(f, inks[((k % s->colors) + s->colors) % s->colors], (0.3f + 0.8f * level) * contrast));
        }
    }
    f->fx->glow = 0.6f;
}

/* ---------- Rubber checkerboard ---------- */

typedef struct {
    float tile, wave, freq, lens, phase;
} OpArt;

void opart_setup(void *state, Rng *r, const Frame *f) {
    OpArt *o = state;
    o->tile = rng_between(r, 0.2f, 0.3f);
    o->wave = rng_between(r, 0.03f, 0.06f);
    o->freq = rng_between(r, 2.5f, 4.0f);
    o->lens = rng_between(r, 0.35f, 0.55f);
    o->phase = rng_between(r, 0, TAU_F);
    (void)f;
}

// A checkerboard in the manner of the sixties op artists: waves roll through it and a lens of swollen tiles wanders
// across, so the flat grid seems to breathe and bulge. Each kick swells the lens.
void opart_draw(void *state, const Frame *f) {
    OpArt *o = state;
    Grid *g = f->grid;
    Rgbf inks[3];
    chord_inks(f, inks);
    float t = f->local * op_pace(f), kick = frame_pulse(f, ROLE_KICK, 0.25f, 1.0f), contrast = op_contrast(f);
    float lx = 0.55f * g->aspect * msin(t * 0.37f + o->phase), ly = 0.45f * msin(t * 0.29f + o->phase * 1.3f);
    float swell = 0.3f + 0.35f * kick, lens2 = o->lens * o->lens;
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float dx = cell_x(g, col) - lx, dy = y - ly, bulge = mexp(-(dx * dx + dy * dy) / lens2);
            // Inside the lens the board is sampled closer together, so its tiles look bigger.
            float push = 1 - swell * bulge, u = lx + dx * push, v = ly + dy * push;
            u += o->wave * msin(v * o->freq + t * 1.3f);
            v += o->wave * msin(u * o->freq * 0.8f - t * 1.1f);
            int tu = mfloor_i(u / o->tile), tv = mfloor_i(v / o->tile);
            if ((tu + tv) & 1) continue;
            int pair = (mfloor_i(u / o->tile * 0.5f) + mfloor_i(v / o->tile * 0.5f)) & 1;
            put_cell(g, col, row, '#', frame_dim(f, inks[pair], (0.75f + 0.45f * bulge) * contrast));
        }
    }
    f->fx->glow = 0.5f + 0.25f * kick;
}

/* ---------- Mirror tiles ---------- */

typedef struct {
    float scale, spin, glide_x, glide_y, phase[6];
} Mirror;

void mirror_setup(void *state, Rng *r, const Frame *f) {
    Mirror *m = state;
    m->scale = rng_between(r, 1.6f, 2.6f);
    m->spin = rng_between(r, 0.03f, 0.08f) * (rng_chance(r, 50) ? 1 : -1);
    m->glide_x = rng_between(r, -0.12f, 0.12f);
    m->glide_y = rng_between(r, -0.12f, 0.12f);
    for (int k = 0; k < 6; k++) m->phase[k] = rng_between(r, 0, TAU_F);
    (void)f;
}

// A kaleidoscope's whole field of view: one triangle of drifting lights, a sweeping bar and ripples, reflected again and
// again until it tiles the screen with stars and diamonds, the lattice slowly turning and gliding. Kicks pulse its
// scale.
void mirror_draw(void *state, const Frame *f) {
    Mirror *m = state;
    Grid *g = f->grid;
    static const char ramp[] = " .:-=+*#@";
    Rgbf inks[3];
    chord_inks(f, inks);
    float t = f->local * op_pace(f), kick = frame_pulse(f, ROLE_KICK, 0.22f, 1.0f), contrast = op_contrast(f);
    float a = t * m->spin, ca = mcos(a), sa = msin(a), scale = m->scale * (1 - 0.12f * kick);
    float bx[3], by[3];
    for (int k = 0; k < 3; k++) {
        bx[k] = 0.5f + 0.42f * msin(t * (0.41f + 0.13f * (float)k) + m->phase[k]);
        by[k] = 0.5f + 0.42f * msin(t * (0.33f + 0.11f * (float)k) + m->phase[k + 3]);
    }
    // The bar: a line through the triangle, turning about a wandering point.
    float bar = t * 0.5f + m->phase[0], nx = mcos(bar), ny = msin(bar);
    float bar_at = 0.5f * nx + 0.3f * ny + 0.15f * msin(t * 0.7f);
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col);
            float u = (x * ca - y * sa) * scale + m->glide_x * t, v = (x * sa + y * ca) * scale + m->glide_y * t;
            // Fold into one cell of the mirror lattice, then across its diagonal.
            u = mfract(u * 0.5f) * 2;
            v = mfract(v * 0.5f) * 2;
            if (u > 1) u = 2 - u;
            if (v > 1) v = 2 - v;
            if (v > u) {
                float swap = u;
                u = v;
                v = swap;
            }
            // The brightest light is the nearest one, so only that one's glow is worked out.
            float nearest = 1e9f;
            int which = 0;
            for (int k = 0; k < 3; k++) {
                float dx = u - bx[k], dy = v - by[k], reach = (dx * dx + dy * dy) * 12;
                if (reach < nearest) nearest = reach, which = k;
            }
            float across = u * nx + v * ny - bar_at;
            if (across * across * 90 < nearest) nearest = across * across * 90, which = 2;
            float best = mexp(-nearest);
            float ripple = 0.5f + 0.5f * msin((u + v) * 9 - t * 2.2f);
            float level = saturate(best * 1.1f + ripple * ripple * 0.4f - 0.05f);
            int i = (int)(level * 8.99f);
            if (i <= 0) continue;
            Rgbf ink = rgbf_mix(inks[which], inks[(which + 1) % 3], ripple * 0.3f);
            put_cell(g, col, row, ramp[i], frame_dim(f, ink, (0.35f + 0.75f * level) * contrast));
        }
    }
    f->fx->glow = 0.55f + 0.25f * kick;
}

/* ---------- Moire ---------- */

typedef struct {
    float density, speed, phase[4];
} Moire;

void moire_setup(void *state, Rng *r, const Frame *f) {
    Moire *m = state;
    m->density = rng_between(r, 7.0f, 11.0f);
    m->speed = rng_between(r, 0.6f, 1.2f);
    for (int k = 0; k < 4; k++) m->phase[k] = rng_between(r, 0, TAU_F);
    (void)f;
}

// Two sets of rings rippling out from wandering centers: where they cross, interference fringes swing and shimmer the
// way op art does. Kicks send both sets of rings racing outward.
void moire_draw(void *state, const Frame *f) {
    Moire *m = state;
    Grid *g = f->grid;
    Rgbf inks[3];
    chord_inks(f, inks);
    float pace = op_pace(f), contrast = op_contrast(f), t = f->local * pace;
    float flow = (frame_travel(f, m->speed * 0.5f, m->speed) + frame_surge(f, ROLE_KICK, 0.2f, 0.7f)) * pace;
    float ax = 0.6f * g->aspect * msin(t * 0.23f + m->phase[0]), ay = 0.5f * msin(t * 0.31f + m->phase[1]);
    float bx = 0.6f * g->aspect * msin(t * 0.19f + m->phase[2]), by = 0.5f * msin(t * 0.27f + m->phase[3]);
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col);
            int a = mfloor_i(mhypot(x - ax, y - ay) * m->density - flow), b = mfloor_i(mhypot(x - bx, y - by) * m->density - flow);
            if ((a + b) & 1)
                put_cell(g, col, row, '#', frame_dim(f, inks[0], 0.9f * contrast));
            else if (a & 1)
                put_cell(g, col, row, ':', frame_dim(f, inks[1], 0.6f * contrast));
        }
    }
    f->fx->glow = 0.5f;
}

/* ---------- Truchet tiles ---------- */

#define TRUCHET_KICKS 64

typedef struct {
    int size;
    float wave;
    uint32_t salt;
} Truchet;

void truchet_setup(void *state, Rng *r, const Frame *f) {
    Truchet *tr = state;
    static const int sizes[3] = {8, 10, 12};
    tr->size = sizes[rng_range(r, 0, 3)];
    tr->wave = rng_between(r, 1.2f, 2.0f);
    tr->salt = rng_u32(r);
    (void)f;
}

// Truchet tiles: every square holds two quarter-circle ribbons, and together they wind into endless paths. Each kick
// sends a wave of flips rippling out from the middle that rewires them (a slow wave of its own keeps them changing
// between beats), and bands of color run outward along the ribbons.
void truchet_draw(void *state, const Frame *f) {
    Truchet *tr = state;
    Grid *g = f->grid;
    Rgbf inks[3];
    chord_inks(f, inks);
    const ScoreEvent *kicks[TRUCHET_KICKS];
    int n = f->react > 0 && !f->still ? recent_notes(f->score, ROLE_KICK, f->t, f->local, kicks, TRUCHET_KICKS) : 0;
    float pace = op_pace(f), contrast = op_contrast(f), flow = f->t * 0.8f * pace;
    for (int ty = 0; ty * tr->size < g->rows; ty++)
        for (int tx = 0; tx * tr->size < g->cols; tx++) {
            float cx = cell_x(g, tx * tr->size) + (float)tr->size / (float)g->cols * g->aspect;
            float cy = cell_y(g, ty * tr->size) - (float)tr->size / (float)g->rows;
            float reach = mhypot(cx, cy) / tr->wave;
            int flips = mfloor_i((f->local - reach) / (3.0f / pace));
            for (int k = 0; k < n; k++) flips += f->t - kicks[k]->time > reach;
            int turned = ((int)(hash_u32(tr->salt + (uint32_t)(ty * 131 + tx)) & 1) + flips) & 1;
            for (int r = 0; r < tr->size; r++)
                for (int c = 0; c < tr->size; c++) {
                    int col = tx * tr->size + c, row = ty * tr->size + r;
                    if (col >= g->cols || row >= g->rows) continue;
                    // Within the tile, u runs right and v runs up; the arcs are centred on two opposite corners.
                    float u = ((float)c + 0.5f) / (float)tr->size, v = 1 - ((float)r + 0.5f) / (float)tr->size;
                    float ax = turned ? 1.0f : 0.0f, du1 = u - ax, dv1 = v, du2 = u - (1 - ax), dv2 = v - 1;
                    float d1 = mhypot(du1, dv1), d2 = mhypot(du2, dv2);
                    float off1 = mabs(d1 - 0.5f), off2 = mabs(d2 - 0.5f);
                    float du = off1 < off2 ? du1 : du2, dv = off1 < off2 ? dv1 : dv2, off = mmin(off1, off2);
                    if (off > 0.17f) continue;
                    float x = cell_x(g, col), y = cell_y(g, row), band = mfract(mhypot(x, y) * 1.5f - flow);
                    Rgbf ink = inks[(int)(band * 3) % 3];
                    if (off < 0.07f)
                        put_cell(g, col, row, '#', frame_dim(f, ink, contrast));
                    else
                        put_cell(g, col, row, slope_glyph(-dv, du), frame_dim(f, ink, 0.6f * contrast));
                }
        }
    f->fx->glow = 0.6f;
}

/* ---------- Squares ---------- */

typedef struct {
    float bands, twist, spin, speed;
} Squares;

void squares_setup(void *state, Rng *r, const Frame *f) {
    Squares *s = state;
    s->bands = rng_between(r, 5.0f, 8.0f);
    s->twist = rng_between(r, 0.08f, 0.18f) * (rng_chance(r, 50) ? 1 : -1);
    s->spin = rng_between(r, 0.15f, 0.35f) * (rng_chance(r, 50) ? 1 : -1);
    s->speed = rng_between(r, 0.4f, 0.8f);
    (void)f;
}

// Nested squares, each turned a little further than the one outside it, twisting together into a vortex: kicks wind
// the twist tighter before it eases back, and the bands of color stream outward.
void squares_draw(void *state, const Frame *f) {
    Squares *s = state;
    Grid *g = f->grid;
    Rgbf inks[3];
    chord_inks(f, inks);
    float kick = frame_pulse(f, ROLE_KICK, 0.3f, 1.0f), twist = s->twist * (1 + 1.2f * kick);
    float pace = op_pace(f), contrast = op_contrast(f);
    float flow = frame_travel(f, s->speed * 0.5f, s->speed) * pace, base = f->local * s->spin * pace;
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), depth = mhypot(x, y) * s->bands;
            float a = base + depth * twist, c = mcos(a), sn = msin(a);
            float px = x * c + y * sn, py = -x * sn + y * c, side = mmax(mabs(px), mabs(py));
            float ring = side * s->bands * 1.2f - flow;
            int band = mfloor_i(ring);
            Rgbf ink = inks[((band % 3) + 3) % 3];
            if (mfract(ring) < 0.24f) {
                // The square's edge, drawn along its true direction on screen.
                int upright = mabs(px) > mabs(py);
                put_cell(g, col, row, upright ? slope_glyph(-sn, c) : slope_glyph(c, sn), frame_dim(f, ink, (1.0f + 0.3f * kick) * contrast));
            } else if (band & 1) {
                put_cell(g, col, row, '#', frame_dim(f, ink, 0.5f * contrast));
            }
        }
    }
    f->fx->glow = 0.55f + 0.3f * kick;
}
