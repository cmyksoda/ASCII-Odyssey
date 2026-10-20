// SPDX-License-Identifier: GPL-3.0-only
// Op art at full tilt: a checkerboard vortex zooming forever inward, a hypnotic spiral, a rubber checkerboard, a
// kaleidoscope tiled across the whole screen, moire rings, flipping Truchet tiles, a twisting tower of squares, a
// checkered box tunnel, a honeycomb, the cafe wall illusion, the Amiga's bouncing ball, a kaleidoscope of gems, a grid
// of twisting tiles, Escher's circle limit, a Sierpinski carpet, a quasicrystal, a kaleidoscope tunnel, xor checkers
// and tumbling blocks. Kicks drive their motion and chords turn their colors.
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

// The glyph for a line running in direction (dx, dy), with y up the screen.
static int slope_glyph(float dx, float dy) {
    if (mabs(dy) < 0.4f * mabs(dx)) return '-';
    if (mabs(dx) < 0.4f * mabs(dy)) return '|';
    return (dx > 0) == (dy > 0) ? '/' : '\\';
}

/* ---------- Vortex ---------- */

typedef struct {
    int sectors, rays, dart;
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
    // Sometimes a sunburst of rays crossed by thin zooming rings, in place of the checkerboard.
    v->rays = rng_chance(r, 35);
    // Sometimes a dartboard, every ring turning the other way to the rings either side of it as they stream out.
    v->dart = rng_chance(r, 30);
    (void)f;
}

// A checkerboard wrapped round a point and zooming into it forever: tiles swell as they reach the edge and dwindle to
// nothing in the dark eye at the heart, spiraling when the board is twisted. Kicks lunge it forward, and snares spin a
// dartboard's rings.
void vortex_draw(void *state, const Frame *f) {
    Vortex *v = state;
    Grid *g = f->grid;
    Rgbf inks[3];
    chord_inks(f, inks);
    float pace = frame_pace(f), contrast = frame_contrast(f);
    float zoom = (frame_travel(f, v->speed * 0.5f, v->speed) + frame_surge(f, ROLE_KICK, 0.2f, 0.8f)) * pace;
    float turn = f->local * v->spin * pace, kick = frame_pulse(f, ROLE_KICK, 0.2f, 1.0f);
    float wheel = (f->local * 0.12f + frame_surge(f, ROLE_SNARE, 0.25f, 0.3f)) * pace * (float)v->sectors;
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), r = mhypot(x, y);
            if (r < 0.04f) continue;
            float around = (matan2(y, x) + turn) / TAU_F * (float)v->sectors, out = mlog2(r) * v->rings - zoom;
            // A dartboard's rings can't spiral, so each is found untwisted before its own board is turned.
            if (v->dart)
                around += mfloor_i(out) & 1 ? wheel : -wheel;
            else
                out += around * v->twist / (float)v->sectors;
            int parity = v->rays ? (mfloor_i(around) & 1) && (mfloor_i(out) & 3) : (mfloor_i(out) + mfloor_i(around)) & 1;
            int edge = mfract(out) < 0.12f || mfract(around) < 0.12f;
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

// An even spiral, a logarithmic one that looks the same at every size, or a rose of rings rippled into petals.
enum { SPIRAL_EVEN, SPIRAL_LOG, SPIRAL_ROSE, SPIRAL_STYLES };

typedef struct {
    int arms, colors, style;
    float tight, speed;
} Spiral;

void spiral_setup(void *state, Rng *r, const Frame *f) {
    Spiral *s = state;
    s->arms = rng_range(r, 1, 5);
    s->colors = rng_chance(r, 50) ? 2 : 3;
    s->tight = rng_between(r, 3.0f, 6.0f);
    s->speed = rng_between(r, 0.5f, 1.0f) * (rng_chance(r, 50) ? 1 : -1);
    s->style = rng_range(r, 0, SPIRAL_STYLES);
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
    float flow = (frame_travel(f, s->speed * 0.6f, s->speed) + sign * frame_surge(f, ROLE_KICK, 0.2f, 0.6f)) * frame_pace(f);
    float contrast = frame_contrast(f);
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), r = mhypot(x, y);
            float a = matan2(y, x), band;
            if (s->style == SPIRAL_LOG)
                band = mlog2(r + 0.01f) * s->tight * 0.6f + a / TAU_F * (float)s->arms - flow;
            else if (s->style == SPIRAL_ROSE)
                band = r * s->tight + 0.35f * msin(a * (float)(s->arms + 3)) - flow;
            else
                band = r * s->tight + a / TAU_F * (float)s->arms - flow;
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
    float tile, wave, freq, lens, phase, whirl;
} OpArt;

void opart_setup(void *state, Rng *r, const Frame *f) {
    OpArt *o = state;
    o->tile = rng_between(r, 0.2f, 0.3f);
    o->wave = rng_between(r, 0.03f, 0.06f);
    o->freq = rng_between(r, 2.5f, 4.0f);
    o->lens = rng_between(r, 0.35f, 0.55f);
    o->phase = rng_between(r, 0, TAU_F);
    // Sometimes the lens twists the board into a whirlpool as well as swelling it.
    o->whirl = rng_chance(r, 40) ? rng_between(r, 1.5f, 3.0f) * (rng_chance(r, 50) ? 1 : -1) : 0;
    (void)f;
}

// A checkerboard in the manner of the sixties op artists: waves roll through it and a lens of swollen tiles wanders
// across, so the flat grid seems to breathe and bulge, sometimes twisting into a whirlpool. Each kick swells the lens.
void opart_draw(void *state, const Frame *f) {
    OpArt *o = state;
    Grid *g = f->grid;
    Rgbf inks[3];
    chord_inks(f, inks);
    float t = f->local * frame_pace(f), kick = frame_pulse(f, ROLE_KICK, 0.25f, 1.0f), contrast = frame_contrast(f);
    float lx = 0.55f * g->aspect * msin(t * 0.37f + o->phase), ly = 0.45f * msin(t * 0.29f + o->phase * 1.3f);
    float swell = 0.3f + 0.35f * kick, lens2 = o->lens * o->lens;
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float dx = cell_x(g, col) - lx, dy = y - ly, bulge = mexp(-(dx * dx + dy * dy) / lens2);
            // Inside the lens the board is sampled closer together, so its tiles look bigger.
            float push = 1 - swell * bulge, u = lx + dx * push, v = ly + dy * push;
            if (o->whirl != 0 && bulge > 0.01f) {
                // The twist tightens and loosens slowly, strongest at the lens's heart.
                float a = o->whirl * bulge * (0.6f + 0.4f * msin(t * 0.5f)), c = mcos(a), s = msin(a), wx = u - lx, wy = v - ly;
                u = lx + wx * c - wy * s, v = ly + wx * s + wy * c;
            }
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
    float t = f->local * frame_pace(f), kick = frame_pulse(f, ROLE_KICK, 0.22f, 1.0f), contrast = frame_contrast(f);
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

// Two rippling sets of rings, two gratings of straight lines slowly turning across each other, or one of each.
enum { MOIRE_RINGS, MOIRE_GRATINGS, MOIRE_MIXED, MOIRE_STYLES };

typedef struct {
    float density, speed, phase[4];
    int style;
} Moire;

void moire_setup(void *state, Rng *r, const Frame *f) {
    Moire *m = state;
    m->density = rng_between(r, 7.0f, 11.0f);
    m->speed = rng_between(r, 0.6f, 1.2f);
    for (int k = 0; k < 4; k++) m->phase[k] = rng_between(r, 0, TAU_F);
    m->style = rng_range(r, 0, MOIRE_STYLES);
    (void)f;
}

// How many rings (or lines) out from its origin the point lies, for one of the two overlapping sets.
static float moire_field(const Moire *m, int second, float x, float y, float cx, float cy, float t) {
    int ring = m->style == MOIRE_RINGS || (m->style == MOIRE_MIXED && !second);
    if (ring) return mhypot(x - cx, y - cy) * m->density;
    float a = second ? -t * 0.07f + m->phase[2] : t * 0.1f + m->phase[0];
    return (x * mcos(a) + y * msin(a)) * m->density;
}

// Two overlapping sets of rings or lines: where they cross, interference fringes swing and shimmer the way op art
// does. Kicks send both sets racing outward.
void moire_draw(void *state, const Frame *f) {
    Moire *m = state;
    Grid *g = f->grid;
    Rgbf inks[3];
    chord_inks(f, inks);
    float pace = frame_pace(f), contrast = frame_contrast(f), t = f->local * pace;
    float flow = (frame_travel(f, m->speed * 0.5f, m->speed) + frame_surge(f, ROLE_KICK, 0.2f, 0.7f)) * pace;
    float ax = 0.6f * g->aspect * msin(t * 0.23f + m->phase[0]), ay = 0.5f * msin(t * 0.31f + m->phase[1]);
    float bx = 0.6f * g->aspect * msin(t * 0.19f + m->phase[2]), by = 0.5f * msin(t * 0.27f + m->phase[3]);
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col);
            int a = mfloor_i(moire_field(m, 0, x, y, ax, ay, t) - flow), b = mfloor_i(moire_field(m, 1, x, y, bx, by, t) - flow);
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

// Rows that make a tile `cols` columns wide look square, given the cells' shape on screen.
static int square_rows(const Grid *g, int cols) {
    int rows = (int)((float)cols * (float)g->cell_w * g->pixel_aspect / (float)g->cell_h + 0.5f);
    return rows < 1 ? 1 : rows;
}

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
    float pace = frame_pace(f), contrast = frame_contrast(f), flow = f->t * 0.8f * pace;
    int tall = square_rows(g, tr->size);
    for (int ty = 0; ty * tall < g->rows; ty++)
        for (int tx = 0; tx * tr->size < g->cols; tx++) {
            float cx = cell_x(g, tx * tr->size) + (float)tr->size / (float)g->cols * g->aspect;
            float cy = cell_y(g, ty * tall) - (float)tall / (float)g->rows;
            float reach = mhypot(cx, cy) / tr->wave;
            int flips = mfloor_i((f->local - reach) / (3.0f / pace));
            for (int k = 0; k < n; k++) flips += f->t - kicks[k]->time > reach;
            int turned = ((int)(hash_u32(tr->salt + (uint32_t)(ty * 131 + tx)) & 1) + flips) & 1;
            for (int r = 0; r < tall; r++)
                for (int c = 0; c < tr->size; c++) {
                    int col = tx * tr->size + c, row = ty * tall + r;
                    if (col >= g->cols || row >= g->rows) continue;
                    // Within the tile, u runs right and v runs up; the arcs are centered on two opposite corners.
                    float u = ((float)c + 0.5f) / (float)tr->size, v = 1 - ((float)r + 0.5f) / (float)tall;
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
    float pace = frame_pace(f), contrast = frame_contrast(f);
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

/* ---------- Box tunnel ---------- */

typedef struct {
    float speed, twist, spin;
    int tiles;
} BoxTunnel;

void boxtunnel_setup(void *state, Rng *r, const Frame *f) {
    BoxTunnel *b = state;
    b->speed = rng_between(r, 1.2f, 2.0f);
    b->twist = rng_between(r, 0.1f, 0.3f) * (rng_chance(r, 50) ? 1 : -1);
    b->spin = rng_between(r, 0.1f, 0.25f) * (rng_chance(r, 50) ? 1 : -1);
    b->tiles = rng_range(r, 2, 4) * 2;
    (void)f;
}

// Flying down a square tunnel lined with checkerboard: the walls rush past, the far end twists ahead of the near,
// and every kick lunges deeper; far away the walls fade into a dark square.
void boxtunnel_draw(void *state, const Frame *f) {
    BoxTunnel *b = state;
    Grid *g = f->grid;
    Rgbf inks[3];
    chord_inks(f, inks);
    float pace = frame_pace(f), contrast = frame_contrast(f), kick = frame_pulse(f, ROLE_KICK, 0.2f, 1.0f);
    float travel = (frame_travel(f, b->speed * 0.5f, b->speed) + frame_surge(f, ROLE_KICK, 0.2f, 0.9f)) * pace;
    float base = f->local * b->spin * pace;
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), r = mhypot(x, y);
            // The twist grows with depth, and depth grows as the radius shrinks, which turning doesn't change.
            float a = base + b->twist * 0.4f / (r + 0.08f), c = mcos(a), s = msin(a);
            float px = x * c - y * s, py = x * s + y * c, m = mmax(mabs(px), mabs(py));
            if (m < 0.06f) continue;
            int side = mabs(px) > mabs(py), wall = side ? (px > 0 ? 0 : 2) : (py > 0 ? 1 : 3);
            float along = (side ? py : px) / m * (float)b->tiles * 0.5f, deep = 0.4f / m * 2 + travel;
            int parity = (mfloor_i(along) + mfloor_i(deep) + wall) & 1;
            int edge = mfract(along) < 0.12f || mfract(deep) < 0.12f;
            float fog = smoothstep(0.06f, 0.5f, m) * contrast;
            if (parity)
                put_cell(g, col, row, edge ? '+' : '#', frame_dim(f, inks[wall % 3], fog * (0.9f + 0.3f * kick)));
            else
                put_cell(g, col, row, edge ? '.' : ':', frame_dim(f, inks[(wall + 1) % 3], fog * 0.6f));
        }
    }
    f->fx->glow = 0.55f + 0.3f * kick;
}

// The hexagon of a pointy-topped honeycomb of size s holding (x, y): its center and its cube coordinates, found by
// rounding through cube coordinates so the nearest center always wins.
static void hex_cell(float x, float y, float s, float *cx, float *cy, float *qx, float *qz) {
    float q = (x * 0.5773503f - y / 3) / s, rr = y * 2 / 3 / s, ax = q, az = rr, ay = -q - rr;
    float rx = mfloor(ax + 0.5f), ry = mfloor(ay + 0.5f), rz = mfloor(az + 0.5f);
    float ex = mabs(rx - ax), ey = mabs(ry - ay), ez = mabs(rz - az);
    if (ex > ey && ex > ez)
        rx = -ry - rz;
    else if (ey > ez)
        ry = -rx - rz;
    else
        rz = -rx - ry;
    *cx = s * 1.7320508f * (rx + rz * 0.5f), *cy = s * 1.5f * rz, *qx = rx, *qz = rz;
}

/* ---------- Honeycomb ---------- */

#define HONEY_KICKS 16

typedef struct {
    float size, drift;
    uint32_t salt;
} Honeycomb;

void honeycomb_setup(void *state, Rng *r, const Frame *f) {
    Honeycomb *h = state;
    h->size = rng_between(r, 0.11f, 0.17f);
    h->drift = rng_between(r, -0.05f, 0.05f);
    h->salt = rng_u32(r);
    (void)f;
}

// A honeycomb of hexagons drawn edge by edge: each kick sends a ring of light rippling out across the cells from the
// middle, and between kicks a slow tide of color washes over them.
void honeycomb_draw(void *state, const Frame *f) {
    Honeycomb *h = state;
    Grid *g = f->grid;
    Rgbf inks[3];
    chord_inks(f, inks);
    float contrast = frame_contrast(f), t = f->local * frame_pace(f), s = h->size;
    const ScoreEvent *kicks[HONEY_KICKS];
    int n = f->react > 0 ? recent_notes(f->score, ROLE_KICK, f->t, 1.6f, kicks, HONEY_KICKS) : 0;
    // The three directions an edge of a pointy-topped hexagon faces, and the glyph that draws each edge.
    static const float NX[3] = {1.0f, 0.5f, -0.5f}, NY[3] = {0.0f, 0.8660254f, 0.8660254f};
    static const char EDGE[3] = {'|', '\\', '/'};
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row) + h->drift * t;
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), hx, hy, rx, rz;
            hex_cell(x, y, s, &hx, &hy, &rx, &rz);
            float lx = x - hx, ly = y - hy;
            int facing = 0;
            float reach = 0;
            for (int k = 0; k < 3; k++) {
                float p = mabs(lx * NX[k] + ly * NY[k]);
                if (p > reach) reach = p, facing = k;
            }
            float depth = reach / (s * 0.8660254f), dist = mhypot(hx, hy - h->drift * t);
            float wave = 0;
            for (int k = 0; k < n; k++) {
                float front = (f->t - kicks[k]->time) * 1.4f - dist;
                wave = mmax(wave, mexp(-front * front * 40) * kicks[k]->velocity * frame_bright(f));
            }
            float tide = 0.5f + 0.5f * msin(dist * 3 - t * 1.2f + hash01((uint32_t)(rx * 31 + rz * 977) ^ h->salt) * 2);
            Rgbf ink = inks[(int)(tide * 2.99f)];
            if (depth > 0.86f)
                put_cell(g, col, row, EDGE[facing], frame_dim(f, ink, (0.7f + 0.6f * wave) * contrast));
            else if (tide + wave > 0.75f)
                put_cell(g, col, row, wave > 0.3f ? '#' : ':', frame_dim(f, ink, (0.25f + 0.5f * tide + wave) * contrast));
        }
    }
    f->fx->glow = 0.55f;
}

/* ---------- Cafe wall ---------- */

typedef struct {
    int tile, course;
    float sway, phase;
} CafeWall;

void cafewall_setup(void *state, Rng *r, const Frame *f) {
    CafeWall *w = state;
    w->tile = rng_range(r, 5, 8);
    w->course = rng_range(r, 3, 5);
    w->sway = rng_between(r, 0.6f, 1.2f);
    w->phase = rng_between(r, 0.4f, 0.9f);
    (void)f;
}

// The cafe wall illusion: courses of light and dark tiles with gray mortar between, each course shifted against the
// next, so perfectly level rows seem to slope. The courses slide to and fro in a wave, so the slopes seem to breathe
// and flip; each kick nudges them along.
void cafewall_draw(void *state, const Frame *f) {
    CafeWall *w = state;
    Grid *g = f->grid;
    Rgbf inks[3];
    chord_inks(f, inks);
    float contrast = frame_contrast(f), t = f->local * frame_pace(f), nudge = frame_surge(f, ROLE_KICK, 0.2f, 0.5f) * frame_pace(f);
    Rgbf mortar = palette_ramp(f->palette, 0.45f);
    int band = w->course + 1;
    for (int row = 0; row < g->rows; row++) {
        int k = row / band;
        if (row % band == w->course) {
            for (int col = 0; col < g->cols; col++) put_cell(g, col, row, '-', frame_dim(f, mortar, contrast));
            continue;
        }
        float shift = ((float)(k & 1) * 0.5f + w->sway * 0.5f * msin(t * 0.9f + (float)k * w->phase) + nudge * 0.25f) * (float)w->tile;
        for (int col = 0; col < g->cols; col++) {
            int light = mfloor_i(((float)col + shift) / (float)w->tile) & 1;
            if (light) put_cell(g, col, row, '#', frame_dim(f, inks[k % 3 == 2 ? 1 : 0], 0.95f * contrast));
        }
    }
    f->fx->glow = 0.45f;
}

/* ---------- Boing ball ---------- */

typedef struct {
    float tilt, radius, cross;
    int bands;
} Boing;

void boing_setup(void *state, Rng *r, const Frame *f) {
    Boing *b = state;
    b->tilt = rng_between(r, 0.25f, 0.4f) * (rng_chance(r, 50) ? 1 : -1);
    b->radius = rng_between(r, 0.28f, 0.36f);
    b->cross = rng_between(r, 0.25f, 0.4f);
    b->bands = rng_chance(r, 50) ? 8 : 6;
    (void)f;
}

// The Amiga's bouncing ball: a checkered sphere on a tilted axis, rolling as it crosses a room of grid lines and
// landing on every other beat, squashing a little as it lands, its shadow sliding along the wall behind.
void boing_draw(void *state, const Frame *f) {
    Boing *b = state;
    Grid *g = f->grid;
    Rgbf inks[3];
    chord_inks(f, inks);
    float contrast = frame_contrast(f), pace = frame_pace(f), rad = b->radius, floor_y = -0.72f;
    Rgbf line = frame_dim(f, palette_ramp(f->palette, 0.4f), contrast), white = palette_ramp(f->palette, 1.0f);
    // The room: a wall of grid squares, each line on the row or column nearest it, and a floor whose lines run off
    // toward the horizon.
    int wall_bottom = cell_row(g, floor_y);
    for (float gy = 0.2f * mfloor(floor_y / 0.2f + 1); gy < 1; gy += 0.2f) {
        int row = cell_row(g, gy);
        for (int col = 0; col < g->cols && row < wall_bottom; col++) put_cell(g, col, row, '-', line);
    }
    for (float gx = 0.2f * mfloor(-g->aspect / 0.2f); gx < g->aspect; gx += 0.2f) {
        int col = cell_col(g, gx);
        if (col < 0 || col >= g->cols) continue;
        for (int row = 0; row < wall_bottom && row < g->rows; row++)
            put_cell(g, col, row, grid_at(g, col, row)->glyph == '-' ? '+' : '|', line);
    }
    for (int row = wall_bottom; row < g->rows; row++) {
        // Only rows truly below the floor line, so the depth stays bounded whatever the cells' size.
        float y = cell_y(g, row), below = floor_y - y;
        if (below < 0) continue;
        float depth = 0.3f / (below + 0.02f);
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), across = x * depth;
            int run = mabs(mfract(across / 0.2f * 0.25f + 0.5f) - 0.5f) < 0.05f * depth + 0.03f;
            int rung = mfract(depth * 1.5f) < 0.18f;
            if (run || rung) put_cell(g, col, row, run ? (x * depth > 0 ? '\\' : '/') : '-', frame_dim(f, line, 0.8f));
        }
    }
    // A hop every two beats, landing on the beat; under reduce motion it hops half as high.
    float hop = mabs(msin(PI_F * f->t / (f->score->beat * 2))), height = (1.05f - 2 * rad) * pace;
    float squash = 1 - 0.14f * (1 - smoothstep(0.0f, 0.18f, hop)) * pace;
    float cy = floor_y + rad * squash + hop * height, sweep = mfract(f->local * b->cross * pace * 0.25f);
    float cx = (mabs(sweep * 2 - 1) * 2 - 1) * (g->aspect - rad - 0.08f);
    float roll = cx / rad, tc = mcos(b->tilt), ts = msin(b->tilt), lx = -0.45f, ly = 0.55f, lz = 0.7f;
    // Its shadow on the wall, down and to the right.
    for (int row = cell_row(g, cy + rad + 0.1f); row <= cell_row(g, cy - rad); row++)
        for (int col = cell_col(g, cx - rad); col <= cell_col(g, cx + rad + 0.2f); col++) {
            float dx = (cell_x(g, col) - cx - 0.16f) / rad, dy = (cell_y(g, row) - cy + 0.06f) / rad;
            if (dx * dx + dy * dy < 1 && cell_y(g, row) > floor_y && col >= 0 && col < g->cols && row >= 0 && row < g->rows)
                grid_at(g, col, row)->glyph = ' ';
        }
    for (int row = cell_row(g, cy + rad / squash); row <= cell_row(g, cy - rad / squash); row++)
        for (int col = cell_col(g, cx - rad / squash); col <= cell_col(g, cx + rad / squash); col++) {
            float u = (cell_x(g, col) - cx) / (rad / squash), v = (cell_y(g, row) - cy) / (rad * squash), d2 = u * u + v * v;
            if (d2 >= 1) continue;
            float nz = msqrt(1 - d2), tu = u * tc - v * ts, tv = u * ts + v * tc;
            float lon = matan2(tu, nz) + roll, lat = matan2(tv, msqrt(mmax(0.0f, 1 - tv * tv)));
            int parity = (mfloor_i(lon / PI_F * (float)b->bands) + mfloor_i(lat / PI_F * (float)b->bands)) & 1;
            float lit = 0.3f + 0.7f * saturate(u * lx + v * ly + nz * lz);
            static const char ramp[] = ".:=+*#@";
            put_cell(g, col, row, ramp[(int)(lit * 6.99f)], frame_dim(f, parity ? inks[0] : white, lit * contrast * 1.1f));
        }
    f->fx->glow = 0.5f;
}

/* ---------- Gems ---------- */

typedef struct {
    float scale, spin, phase[6], size[3];
} Gems;

void gems_setup(void *state, Rng *r, const Frame *f) {
    Gems *m = state;
    m->scale = rng_between(r, 0.32f, 0.5f);
    m->spin = rng_between(r, 0.04f, 0.1f) * (rng_chance(r, 50) ? 1 : -1);
    for (int k = 0; k < 6; k++) m->phase[k] = rng_between(r, 0, TAU_F);
    for (int k = 0; k < 3; k++) m->size[k] = rng_between(r, 0.09f, 0.15f);
    (void)f;
}

// A three-mirror kaleidoscope: every hexagon of a honeycomb holds the same twelvefold rosette of tumbling gems and a
// ring of light, reflected in the hexagon's mirrors, so the screen tiles with jewels. The lattice slowly turns, and
// each kick swells it.
void gems_draw(void *state, const Frame *f) {
    Gems *m = state;
    Grid *g = f->grid;
    static const char ramp[] = " .:-=+*#@";
    Rgbf inks[3];
    chord_inks(f, inks);
    float contrast = frame_contrast(f), t = f->local * frame_pace(f), kick = frame_pulse(f, ROLE_KICK, 0.22f, 1.0f);
    float a0 = t * m->spin, ca = mcos(a0), sa = msin(a0), s = m->scale * (1 + 0.1f * kick);
    float gu[3], gv[3];
    for (int k = 0; k < 3; k++) {
        // Each gem tumbles about inside the wedge between the hexagon's middle and its edge.
        gu[k] = 0.12f + 0.32f * (0.5f + 0.5f * msin(t * (0.5f + 0.17f * (float)k) + m->phase[k]));
        gv[k] = gu[k] * 0.5f * (0.5f + 0.5f * msin(t * (0.37f + 0.11f * (float)k) + m->phase[k + 3]));
    }
    float ring = 0.25f + 0.15f * msin(t * 0.9f);
    for (int row = 0; row < g->rows; row++) {
        float y0 = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float x0 = cell_x(g, col), x = x0 * ca - y0 * sa, y = x0 * sa + y0 * ca, hx, hy, qx, qz;
            hex_cell(x, y, s, &hx, &hy, &qx, &qz);
            // Fold into one twelfth of the hexagon by its mirrors: across both axes, then the lines at 60 and 30 degrees.
            float u = mabs(x - hx) / s, v = mabs(y - hy) / s, r = mhypot(u, v), d = v * 0.5f - u * 0.8660254f;
            if (d > 0) u += 2 * d * 0.8660254f, v -= 2 * d * 0.5f;
            d = v * 0.8660254f - u * 0.5f;
            if (d > 0) u += 2 * d * 0.5f, v -= 2 * d * 0.8660254f;
            float best = 0;
            int which = 0;
            for (int k = 0; k < 3; k++) {
                float d = (mabs(u - gu[k]) + mabs(v - gv[k]) * 1.6f) / m->size[k];
                float gem = d < 1 ? 0.55f + 0.45f * (1 - d) + (d > 0.8f ? 0.3f : 0) : 0;
                if (gem > best) best = gem, which = k;
            }
            float off = mabs(r - ring) / 0.06f, halo = off < 1 ? (1 - off * off) * (1 - off * off) : 0;
            if (halo > best) best = halo, which = 2;
            // A faint glass ground fills each hexagon, and neighboring hexagons take the colors in turn.
            best = mmax(best, 0.14f + 0.1f * (1 - r));
            int turn = ((int)(qx - qz) % 3 + 3) % 3, i = (int)(saturate(best) * 8.99f);
            if (i <= 0) continue;
            put_cell(g, col, row, ramp[i], frame_dim(f, inks[(which + turn) % 3], (0.4f + 0.7f * best) * contrast));
        }
    }
    f->fx->glow = 0.55f + 0.25f * kick;
}

/* ---------- Twisting tiles ---------- */

#define TILES_ACROSS 24
#define TILES_DOWN 14

typedef struct {
    float size, speed, wave;
} TileTwist;

void tiletwist_setup(void *state, Rng *r, const Frame *f) {
    TileTwist *w = state;
    w->size = rng_between(r, 0.17f, 0.26f);
    w->speed = rng_between(r, 0.8f, 1.4f);
    w->wave = rng_between(r, 2.0f, 4.0f);
    (void)f;
}

// A grid of squares, each turning the opposite way to its neighbors, so star-shaped gaps open between them and close
// again; the turning spreads outward from the middle in a wave, and every kick gives it a push.
void tiletwist_draw(void *state, const Frame *f) {
    TileTwist *w = state;
    Grid *g = f->grid;
    Rgbf inks[3];
    chord_inks(f, inks);
    float contrast = frame_contrast(f), pace = frame_pace(f), s = w->size;
    float t = (f->local * w->speed + frame_surge(f, ROLE_KICK, 0.25f, 0.6f)) * pace;
    // Every tile's turn, worked out once: the grid is TILES_ACROSS by TILES_DOWN, centered on the screen.
    static float tc[TILES_DOWN][TILES_ACROSS], ts[TILES_DOWN][TILES_ACROSS];
    int half_x = TILES_ACROSS / 2, half_y = TILES_DOWN / 2;
    for (int ty = 0; ty < TILES_DOWN; ty++)
        for (int tx = 0; tx < TILES_ACROSS; tx++) {
            float cx = ((float)(tx - half_x) + 0.5f) * s, cy = ((float)(ty - half_y) + 0.5f) * s;
            float turn = PI_F * 0.25f * msin(t - mhypot(cx, cy) * w->wave) * (((tx + ty) & 1) ? 1.0f : -1.0f);
            tc[ty][tx] = mcos(turn), ts[ty][tx] = msin(turn);
        }
    static const int NEAR[5][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col);
            int bx = mfloor_i(x / s) + half_x, by = mfloor_i(y / s) + half_y, hit = -1;
            float edge = 0;
            for (int k = 0; k < 5 && hit < 0; k++) {
                int tx = bx + NEAR[k][0], ty = by + NEAR[k][1];
                if (tx < 0 || ty < 0 || tx >= TILES_ACROSS || ty >= TILES_DOWN) continue;
                float lx = x / s - ((float)(tx - half_x) + 0.5f), ly = y / s - ((float)(ty - half_y) + 0.5f);
                float rx = lx * tc[ty][tx] + ly * ts[ty][tx], ry = -lx * ts[ty][tx] + ly * tc[ty][tx], m = mmax(mabs(rx), mabs(ry));
                if (m < 0.47f) hit = (tx + ty) & 1, edge = m;
            }
            if (hit < 0) continue;
            put_cell(g, col, row, edge > 0.38f ? '+' : '#', frame_dim(f, inks[hit], (edge > 0.38f ? 1.05f : 0.8f) * contrast));
        }
    }
    f->fx->glow = 0.5f;
}

/* ---------- Circle limit ---------- */

typedef struct {
    float cos_a, sin_a, center, radius, radius2, step, spin, speed;
    int odd, half;
    uint32_t salt;
} CircleLimit;

void limit_setup(void *state, Rng *r, const Frame *f) {
    CircleLimit *c = state;
    // A polygon's sides and how many meet at each corner: too many to lie flat in a plane, and an even number so the
    // polygons take turns in two colors like a checkerboard.
    static const int PQ[7][2] = {{5, 4}, {6, 4}, {8, 4}, {4, 6}, {4, 8}, {5, 6}, {6, 6}};
    const int *pq = PQ[rng_range(r, 0, 7)];
    float a = PI_F / (float)pq[0], cq = mcos(PI_F / (float)pq[1]), sa = msin(a);
    // The tiles' mirrors: the x axis, the line at angle a, and a circle square to the rim meeting that line at pi / q.
    float d2 = cq * cq / (cq * cq - sa * sa), x = 1 / msqrt(d2);
    c->cos_a = mcos(a), c->sin_a = sa, c->center = msqrt(d2), c->radius2 = d2 - 1, c->radius = msqrt(d2 - 1);
    // One hop carries the middle of a polygon onto its neighbor's, 2 atanh(1 / center) away.
    c->step = mlog2((1 + x) / (1 - x)) * 0.6931472f;
    c->odd = pq[0] & 1;
    c->spin = rng_between(r, 0.04f, 0.1f) * (rng_chance(r, 50) ? 1 : -1);
    c->speed = rng_between(r, 0.25f, 0.4f);
    // Sometimes the plane is seen from its edge, the tiles dwindling toward a horizon along the bottom of the screen.
    c->half = rng_chance(r, 30);
    c->salt = rng_u32(r);
    (void)f;
}

// Escher's circle limit: the hyperbolic plane drawn in a disk, its polygons in checkerboard colors shrinking without
// end toward the rim, each one split by its mirrors into a pinwheel of light and shade. The view flies across the plane
// from polygon to polygon, turning slowly, and each kick lunges it on.
void limit_draw(void *state, const Frame *f) {
    CircleLimit *c = state;
    Grid *g = f->grid;
    Rgbf inks[3];
    chord_inks(f, inks);
    float pace = frame_pace(f), contrast = frame_contrast(f), kick = frame_pulse(f, ROLE_KICK, 0.2f, 1.0f);
    float travel = (frame_travel(f, c->speed * 0.5f, c->speed) + frame_surge(f, ROLE_KICK, 0.2f, 0.8f)) * pace;
    int hops = mfloor_i(travel / c->step);
    // An odd polygon's neighbor sits half a turn round from it, so the view turns with each hop and never jumps.
    float turn = f->local * c->spin * pace + (c->odd && (hops & 1) ? PI_F : 0), ct = mcos(turn), st = msin(turn);
    float a = mtanh((travel - (float)hops * c->step) * 0.5f), radius = 0.97f, cw = 2 * g->aspect / (float)g->cols;
    if (!c->half) dust_stars(f, c->salt, 0.003f, 0.35f);
    for (int row = 0; row < g->rows; row++) {
        float y0 = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float x0 = cell_x(g, col), zx, zy, gain;
            if (c->half) {
                // The upper half-plane, its edge just under the screen, carried into the disk.
                float hy = y0 + 1.03f, den = x0 * x0 + (hy + 1) * (hy + 1);
                zx = (x0 * x0 + hy * hy - 1) / den, zy = -2 * x0 / den, gain = 2 / den;
            } else {
                zx = x0 / radius, zy = y0 / radius, gain = 1 / radius;
            }
            float r2 = zx * zx + zy * zy;
            if (r2 >= 1) {
                if (!c->half && (msqrt(r2) - 1) * radius < cw) put_cell(g, col, row, slope_glyph(-zy, zx), frame_dim(f, inks[2], 0.7f * contrast));
                continue;
            }
            // Turned, then carried along the x axis by the flight: z -> (z + a) / (1 + a z).
            float x = zx * ct - zy * st, y = zx * st + zy * ct;
            float nx = x + a, mx = 1 + a * x, my = a * y, m2 = mx * mx + my * my;
            x = (nx * mx + y * my) / m2, y = (y * mx - nx * my) / m2, gain *= (1 - a * a) / m2;
            // Reflected home into the one triangle, counting the mirrors crossed on the way and the polygon walls among them.
            int flips = 0, walls = 0, k;
            for (k = 0; k < 40; k++) {
                int moved = 0;
                if (y < 0) y = -y, moved++;
                float side = y * c->cos_a - x * c->sin_a;
                if (side > 0) x += 2 * side * c->sin_a, y -= 2 * side * c->cos_a, moved++;
                float ex = x - c->center, e2 = ex * ex + y * y;
                if (e2 < c->radius2) {
                    float s = c->radius2 / e2;
                    x = c->center + ex * s, y *= s, gain *= s, moved++, walls++;
                }
                if (!moved) break;
                flips += moved;
            }
            float fade = (0.4f + 0.6f * (1 - r2)) * contrast, size = (c->center - c->radius) / gain;
            // Polygons too small to draw cell by cell blur into a mist of both colors.
            if (k == 40 || size < 1.5f * cw) {
                put_cell(g, col, row, ':', frame_dim(f, rgbf_mix(inks[0], inks[1], 0.5f), fade * 0.6f));
                continue;
            }
            // Each hop crosses one wall, so the colors swap with it and the flight stays seamless.
            float ex = x - c->center, wall = (msqrt(ex * ex + y * y) - c->radius) / gain;
            int edge = wall < 0.7f * cw, pin = size > 4 * cw ? flips & 1 : 1;
            if ((walls + hops) & 1)
                put_cell(g, col, row, edge ? '+' : pin ? '#' : '%', frame_dim(f, inks[0], fade * (pin ? 1.0f : 0.7f) * (0.85f + 0.3f * kick)));
            else
                put_cell(g, col, row, edge ? '.' : ':', frame_dim(f, inks[1], fade * (pin ? 0.8f : 0.5f)));
        }
    }
    f->fx->glow = 0.55f + 0.25f * kick;
}

/* ---------- Carpet ---------- */

typedef struct {
    float speed, spin;
    int cross;
} Carpet;

void carpet_setup(void *state, Rng *r, const Frame *f) {
    Carpet *c = state;
    c->speed = rng_between(r, 0.3f, 0.5f);
    c->spin = rng_chance(r, 50) ? rng_between(r, 0.03f, 0.07f) * (rng_chance(r, 50) ? 1 : -1) : 0;
    // Sometimes the squares open where just one of the two thirds is a middle one, so crosses fill the screen.
    c->cross = rng_chance(r, 35);
    (void)f;
}

// A Sierpinski carpet zooming forever into the corner where four of them meet: every square is cut into nine and its
// middle punched out, again and again, so holes of every size stream out from the middle and off the screen. The holes
// glow in colors that take turns by size, each framed in light, and kicks lunge the zoom.
void carpet_draw(void *state, const Frame *f) {
    Carpet *c = state;
    Grid *g = f->grid;
    Rgbf inks[3];
    chord_inks(f, inks);
    float pace = frame_pace(f), contrast = frame_contrast(f), kick = frame_pulse(f, ROLE_KICK, 0.2f, 1.0f);
    float zoom = (frame_travel(f, c->speed * 0.5f, c->speed) + frame_surge(f, ROLE_KICK, 0.2f, 0.5f)) * pace;
    int whole = mfloor_i(zoom);
    // A zoom by three brings back the same carpet, so holes keep their colors by counting levels from the start.
    float scale = 0.34f * mexp2(-(zoom - (float)whole) * 1.5849625f), turn = f->local * c->spin * pace, ct = mcos(turn), st = msin(turn);
    float cw = 2 * g->aspect / (float)g->cols, ch = 2.0f / (float)g->rows;
    float side_cut = cw * mabs(ct) + ch * mabs(st), flat_cut = ch * mabs(ct) + cw * mabs(st);
    int side_glyph = slope_glyph(st, ct), flat_glyph = slope_glyph(ct, -st);
    for (int row = 0; row < g->rows; row++) {
        float y0 = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float x0 = cell_x(g, col), u = mabs(x0 * ct - y0 * st) * scale, v = mabs(x0 * st + y0 * ct) * scale;
            float size = 1 / scale, fu = 0, fv = 0;
            int level = -1;
            for (int n = 0; n < 6; n++) {
                int du = mfloor_i(u) % 3, dv = mfloor_i(v) % 3;
                if (c->cross ? (du == 1) != (dv == 1) : du == 1 && dv == 1) {
                    level = n, fu = mfract(u), fv = mfract(v);
                    break;
                }
                u *= 3, v *= 3, size /= 3;
            }
            // Holes fade in as they grow from a speck, so the zoom never pops.
            float grown = level < 0 ? 0 : smoothstep(1.5f * ch, 4 * ch, size);
            if (grown <= 0) {
                put_cell(g, col, row, ':', frame_dim(f, inks[0], 0.3f * contrast));
                continue;
            }
            Rgbf ink = rgbf_mix(inks[0], inks[1 + (level + whole) % 2], grown);
            int side = mmin(fu, 1 - fu) * size < side_cut, flat = mmin(fv, 1 - fv) * size < flat_cut;
            if (side || flat)
                put_cell(g, col, row, side && flat ? '+' : side ? side_glyph : flat_glyph, frame_dim(f, ink, (0.3f + grown * (0.7f + 0.3f * kick)) * contrast));
            else
                put_cell(g, col, row, grown > 0.5f ? '#' : ':', frame_dim(f, ink, (0.3f + grown * (0.3f + 0.2f * kick)) * contrast));
        }
    }
    f->fx->glow = 0.5f + 0.25f * kick;
}

/* ---------- Quasicrystal ---------- */

typedef struct {
    int waves;
    float freq, speed, way, spin;
} Quasi;

void quasi_setup(void *state, Rng *r, const Frame *f) {
    Quasi *q = state;
    static const int waves[3] = {5, 7, 9};
    q->waves = waves[rng_range(r, 0, 3)];
    q->freq = rng_between(r, 16.0f, 20.0f);
    q->speed = rng_between(r, 1.2f, 2.0f);
    q->way = rng_chance(r, 50) ? 1 : -1;
    q->spin = rng_between(r, 0.02f, 0.05f) * (rng_chance(r, 50) ? 1 : -1);
    (void)f;
}

// A quasicrystal: five, seven or nine waves crossing at even angles add up to a rosette that never repeats, its crests
// and troughs in two colors like a checkerboard, and as all the waves march together the petals bloom and melt into
// one another without end. Kicks push the march along.
void quasi_draw(void *state, const Frame *f) {
    Quasi *q = state;
    Grid *g = f->grid;
    Rgbf inks[3];
    chord_inks(f, inks);
    float pace = frame_pace(f), contrast = frame_contrast(f), kick = frame_pulse(f, ROLE_KICK, 0.2f, 1.0f);
    float phase = (frame_travel(f, q->speed * 0.6f, q->speed) + frame_surge(f, ROLE_KICK, 0.2f, 3.0f)) * q->way * pace;
    float turn = f->local * q->spin * pace, k = q->freq * (1 + 0.06f * kick), dx[9], dy[9];
    for (int j = 0; j < q->waves; j++) {
        float a = PI_F * (float)j / (float)q->waves + turn;
        dx[j] = mcos(a) * k, dy[j] = msin(a) * k;
    }
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), sum = 0;
            for (int j = 0; j < q->waves; j++) sum += mcos(x * dx[j] + y * dy[j] + phase);
            float v = sum / (float)q->waves;
            if (v > 0) {
                put_cell(g, col, row, v < 0.08f ? '+' : '#', frame_dim(f, v > 0.5f ? inks[2] : inks[0], (0.75f + 0.3f * kick) * contrast));
            } else {
                put_cell(g, col, row, v > -0.08f ? '.' : ':', frame_dim(f, inks[1], 0.55f * contrast));
            }
        }
    }
    f->fx->glow = 0.5f + 0.25f * kick;
}

/* ---------- Kaleidoscope tunnel ---------- */

typedef struct {
    int mirrors, style;
    float depth, speed, spin, twist;
} KaleidoTunnel;

void ktunnel_setup(void *state, Rng *r, const Frame *f) {
    KaleidoTunnel *k = state;
    k->mirrors = rng_range(r, 3, 9);
    // Walls of checks, of ribbons winding into stars, or of both.
    k->style = rng_range(r, 0, 3);
    k->depth = rng_between(r, 0.35f, 0.5f);
    k->speed = rng_between(r, 0.8f, 1.4f);
    k->spin = rng_between(r, 0.05f, 0.15f) * (rng_chance(r, 50) ? 1 : -1);
    k->twist = rng_chance(r, 50) ? rng_between(r, 0.1f, 0.3f) * (rng_chance(r, 50) ? 1 : -1) : 0;
    (void)f;
}

// Flying down a tunnel seen through a kaleidoscope: its walls are folded between mirrors into a ring of wedges, so
// the checks and ribbons racing past meet themselves in stars and diamonds. The far end glows, and kicks lunge on.
void ktunnel_draw(void *state, const Frame *f) {
    KaleidoTunnel *k = state;
    Grid *g = f->grid;
    static const char ramp[] = " .:-=+*#@";
    Rgbf inks[3];
    chord_inks(f, inks);
    float pace = frame_pace(f), contrast = frame_contrast(f), kick = frame_pulse(f, ROLE_KICK, 0.2f, 1.0f);
    float travel = (frame_travel(f, k->speed * 0.5f, k->speed) + frame_surge(f, ROLE_KICK, 0.2f, 0.8f)) * pace;
    float turn = f->local * k->spin * pace, wedge = TAU_F / (float)k->mirrors;
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), r = mhypot(x, y) + 0.001f, depth = k->depth / r + travel;
            // Folded between two mirrors: across each wedge and back, so every wedge mirrors its neighbors.
            float s = (matan2(y, x) + turn + depth * k->twist) / wedge;
            s -= 2 * mfloor(s * 0.5f);
            if (s > 1) s = 2 - s;
            int check = (mfloor_i(s * 3) + mfloor_i(depth * 2)) & 1;
            float ribbon = mfract(depth * 1.5f + s * 1.2f), band = ribbon < 0.18f ? 1 - ribbon / 0.18f : 0;
            float fog = smoothstep(0.03f, 0.6f, r), level;
            Rgbf ink;
            if (k->style != 0 && band > 0)
                level = 0.6f + 0.4f * band, ink = inks[2];
            else if (k->style != 1)
                level = check ? 0.75f : 0.3f, ink = inks[check];
            else
                level = 0.2f + 0.15f * (mfract(depth * 2) < 0.15f), ink = inks[1];
            level *= fog * (0.85f + 0.3f * kick);
            int i = (int)(saturate(level) * 8.99f);
            if (i > 0) put_cell(g, col, row, ramp[i], frame_dim(f, ink, (0.3f + 0.8f * level) * contrast));
        }
    }
    // The light at the far end of the tunnel.
    float glow = 0.6f + 0.4f * kick;
    put_cell(g, cell_col(g, 0), cell_row(g, 0), '@', frame_dim(f, palette_ramp(f->palette, 1.0f), glow * contrast));
    f->fx->glow = 0.55f + 0.3f * kick;
}

/* ---------- XOR checkers ---------- */

typedef struct {
    float size[2], spin[2], orbit[2], phase[2];
} XorCheckers;

void xor_setup(void *state, Rng *r, const Frame *f) {
    XorCheckers *x = state;
    for (int k = 0; k < 2; k++) {
        x->size[k] = rng_between(r, 0.28f, 0.45f);
        x->spin[k] = rng_between(r, 0.1f, 0.25f) * (k ? -1 : 1);
        x->orbit[k] = rng_between(r, 0.15f, 0.4f);
        x->phase[k] = rng_between(r, 0, TAU_F);
    }
    (void)f;
}

// Two checkerboards turning opposite ways over each other, each round its own wandering middle, a square lit wherever
// exactly one of them is light: the demoscene's xor pattern, endlessly breaking into new mosaics. Kicks zoom both.
void xor_draw(void *state, const Frame *f) {
    XorCheckers *x = state;
    Grid *g = f->grid;
    Rgbf inks[3];
    chord_inks(f, inks);
    float pace = frame_pace(f), contrast = frame_contrast(f), kick = frame_pulse(f, ROLE_KICK, 0.2f, 1.0f);
    float t = f->local * pace, zoom = 1 + 0.12f * kick;
    float cs[2], sn[2], ox[2], oy[2], inv[2];
    for (int k = 0; k < 2; k++) {
        float a = t * x->spin[k] * TAU_F * 0.25f + x->phase[k];
        cs[k] = mcos(a), sn[k] = msin(a), inv[k] = 1 / (x->size[k] * zoom);
        ox[k] = x->orbit[k] * g->aspect * msin(t * 0.21f + x->phase[k]), oy[k] = x->orbit[k] * msin(t * 0.17f + x->phase[k] * 1.7f);
    }
    for (int row = 0; row < g->rows; row++) {
        float y0 = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float x0 = cell_x(g, col);
            int lit[2];
            float edge = 1;
            for (int k = 0; k < 2; k++) {
                float dx = x0 - ox[k], dy = y0 - oy[k], u = (dx * cs[k] - dy * sn[k]) * inv[k], v = (dx * sn[k] + dy * cs[k]) * inv[k];
                lit[k] = (mfloor_i(u) + mfloor_i(v)) & 1;
                float fu = mfract(u), fv = mfract(v);
                edge = mmin(edge, mmin(mmin(fu, 1 - fu), mmin(fv, 1 - fv)) / inv[k]);
            }
            if (!(lit[0] ^ lit[1])) continue;
            int rim = edge < 0.35f * 2 * g->aspect / (float)g->cols;
            put_cell(g, col, row, rim ? '+' : '#', frame_dim(f, inks[lit[1]], (rim ? 1.0f : 0.8f + 0.25f * kick) * contrast));
        }
    }
    f->fx->glow = 0.5f + 0.25f * kick;
}

/* ---------- Tumbling blocks ---------- */

typedef struct {
    float size, speed, wave;
} Blocks;

void blocks_setup(void *state, Rng *r, const Frame *f) {
    Blocks *b = state;
    b->size = rng_between(r, 0.2f, 0.28f);
    b->speed = rng_between(r, 0.3f, 0.5f) * (rng_chance(r, 50) ? 1 : -1);
    b->wave = rng_between(r, 1.5f, 3.5f);
    (void)f;
}

// The tumbling blocks quilt: a honeycomb read as a stack of cubes, each a light top and two shaded sides. The light
// circles round, and when it falls from below every cube seems to turn inside out; the turn sweeps outward from the
// middle in a wave, and each kick swings the light up to a third of the way round.
void blocks_draw(void *state, const Frame *f) {
    Blocks *b = state;
    Grid *g = f->grid;
    static const char ramp[] = " .:-=+*#@";
    Rgbf inks[3];
    chord_inks(f, inks);
    float pace = frame_pace(f), contrast = frame_contrast(f), kick = frame_pulse(f, ROLE_KICK, 0.25f, 1.0f);
    // A kick's surge is its velocity times the time constant, so 4 / 3 makes a full-velocity kick a third of a turn.
    float swing = (f->local * b->speed + frame_surge(f, ROLE_KICK, 0.25f, 4.0f / 3.0f) * (b->speed > 0 ? 1 : -1)) * pace;
    float s = b->size;
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), fq = (0.57735f * x - 0.33333f * y) / s, fr = 0.66667f * y / s, fs = -fq - fr;
            float q = mfloor(fq + 0.5f), r = mfloor(fr + 0.5f), t = mfloor(fs + 0.5f);
            float eq = mabs(q - fq), er = mabs(r - fr), et = mabs(t - fs);
            if (eq > er && eq > et)
                q = -r - t;
            else if (er > et)
                r = -q - t;
            float hx = s * 1.7320508f * (q + r * 0.5f), hy = s * 1.5f * r, dx = x - hx, dy = y - hy;
            // Which face: the top between the corners at 30 and 150 degrees, then the left side, then the right.
            int face = dy >= 0.57735f * mabs(dx) ? 0 : dx < 0 ? 1 : 2;
            // Thin dark seams along the cube's edges keep the blocks apart: round its six sides, and from its middle out to
            // the corners at 30, 150 and 270 degrees where its faces meet.
            float side = mmax(mabs(dx), mmax(mabs(0.5f * dx + 0.866f * dy), mabs(-0.5f * dx + 0.866f * dy))) / (0.866f * s);
            float seam = mmin(0.866f * dx + 0.5f * dy > 0 ? mabs(0.866f * dy - 0.5f * dx) : 1,
                              mmin(-0.866f * dx + 0.5f * dy > 0 ? mabs(0.866f * dy + 0.5f * dx) : 1, dy < 0 ? mabs(dx) : 1)) / s;
            if (side > 0.93f || seam < 0.05f) continue;
            float light = swing - mhypot(hx, hy) * b->wave * 0.15f, lit = 0.5f + 0.5f * mcos(TAU_F * light - (float)face * 2.0944f);
            float level = (0.2f + 0.8f * lit) * (0.9f + 0.2f * kick);
            int i = (int)(saturate(level) * 8.99f);
            if (i > 0) put_cell(g, col, row, ramp[i], frame_dim(f, inks[face], (0.35f + 0.7f * level) * contrast));
        }
    }
    f->fx->glow = 0.5f + 0.2f * kick;
}
