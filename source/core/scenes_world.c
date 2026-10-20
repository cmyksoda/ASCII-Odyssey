// SPDX-License-Identifier: GPL-3.0-only
// Small worlds: a turntable, a cassette, a music box's comb, a mobile, clockwork, a jungle canopy, a highway at
// night, a sound system, a storm, desert dunes, a kelp forest, a meteor shower, a reel-to-reel deck and a carousel.
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

static void circle(const Frame *f, float x, float y, float radius, Rgbf color, int points) {
    StrokePoint pts[64];
    if (points > 64) points = 64;
    for (int i = 0; i < points; i++) {
        float a = (float)i / (float)points * TAU_F;
        pts[i] = (StrokePoint){x + mcos(a) * radius, y + msin(a) * radius, color};
    }
    stroke_draw(f->stroker, pts, points, 1);
}

static void line(const Frame *f, float x0, float y0, float x1, float y1, Rgbf color) {
    StrokePoint pts[2] = {{x0, y0, color}, {x1, y1, color}};
    stroke_draw(f->stroker, pts, 2, 0);
}

/* ---------- Turntable ---------- */

typedef struct {
    float cx, radius, light;
    uint32_t salt;
} Turntable;

void turntable_setup(void *state, Rng *r, const Frame *f) {
    Turntable *t = state;
    t->cx = rng_between(r, -0.35f, -0.1f) * f->grid->aspect;
    t->radius = rng_between(r, 0.78f, 0.88f);
    t->light = rng_between(r, 0.3f, 1.2f);
    t->salt = rng_u32(r);
}

// A record turning at thirty-three and a third: grooves catching a fixed light, the label spinning, the platter's
// strobe dots round the rim, and the tonearm creeping inward as the song plays.
void turntable_draw(void *state, const Frame *f) {
    Turntable *t = state;
    Grid *g = f->grid;
    static const char ramp[] = " .:-=+*";
    float turn = f->t * 0.5556f * TAU_F, cy = -0.02f;
    Rgbf vinyl = palette_ramp(f->palette, 0.3f), shine = palette_ramp(f->palette, 0.95f),
         label = palette_accent(f->palette, 0);
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row) - cy;
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col) - t->cx, d = mhypot(x, y) / t->radius;
            if (d > 1.0f) continue;
            float a = matan2(y, x);
            if (d < 0.3f) {
                // The label, with a printed band and a dot that turn with the record.
                float spin = mfract((a - turn) / TAU_F);
                int band = d > 0.12f && d < 0.22f && spin < 0.45f;
                Rgbf c = band ? palette_accent(f->palette, 1) : label;
                put_cell(g, col, row, d < 0.04f ? 'o' : band ? '=' : '#', frame_dim(f, c, band ? 0.9f : 0.7f));
                continue;
            }
            // Grooves: a fine ring texture, dark between tracks, and a sheen where they face the light.
            float groove = 0.5f + 0.5f * msin(d * 140.0f), sheen = mcos(2 * (a - t->light));
            sheen = sheen > 0 ? sheen * sheen * sheen * sheen : 0;
            float gap = smoothstep(0.012f, 0.0f, mabs(mfract(d * 7.0f) - 0.5f) - 0.47f);
            float level = saturate(0.18f + 0.12f * groove + 0.6f * sheen - 0.25f * gap);
            int i = (int)(level * 6.99f);
            if (i <= 0) i = 1;
            put_cell(g, col, row, ramp[i], rgbf_mix(vinyl, shine, sheen * 0.8f));
        }
    }
    // The platter's strobe dots.
    for (int k = 0; k < 72; k++) {
        float a = (float)k / 72 * TAU_F + f->t * 0.05f;
        put_cell_under(g, cell_col(g, t->cx + mcos(a) * t->radius * 1.07f),
                       cell_row(g, cy + msin(a) * t->radius * 1.07f), k % 2 ? '.' : ':', frame_dim(f, shine, 0.5f));
    }
    // A speck of dust riding the record round.
    float da = turn * 1.0f + (float)(t->salt % 100);
    put_cell(g, cell_col(g, t->cx + mcos(da) * t->radius * 0.62f), cell_row(g, cy + msin(da) * t->radius * 0.62f), '*',
             shine);
    // The tonearm: from its pivot beside the platter to a needle that moves toward the label over the piece.
    float progress = saturate(f->t / mmax(f->score->length, 1)), reach = t->radius * mixf(0.95f, 0.36f, progress);
    float px = t->cx + t->radius * 1.25f, py = cy + t->radius * 0.85f, na = -0.25f;
    float nx = t->cx + mcos(na) * reach, ny = cy + msin(na) * reach;
    Rgbf metal = palette_ramp(f->palette, 0.85f);
    circle(f, px, py, 0.08f, metal, 16);
    line(f, px, py, nx + 0.06f, ny + 0.08f, metal);
    line(f, nx + 0.06f, ny + 0.08f, nx, ny, metal);
    put_cell(g, cell_col(g, nx), cell_row(g, ny), '#', palette_accent(f->palette, 2));
    f->fx->glow = 0.45f;
}

/* ---------- Cassette ---------- */

typedef struct {
    uint32_t salt;
} Cassette;

void cassette_setup(void *state, Rng *r, const Frame *f) {
    Cassette *c = state;
    c->salt = rng_u32(r);
    (void)f;
}

static void rounded_box(const Frame *f, float x0, float y0, float x1, float y1, float r, Rgbf color) {
    StrokePoint pts[40];
    int n = 0;
    const float cx[4] = {x1 - r, x0 + r, x0 + r, x1 - r}, cy[4] = {y1 - r, y1 - r, y0 + r, y0 + r};
    for (int k = 0; k < 4; k++)
        for (int s = 0; s <= 9; s++) {
            float a = ((float)k + (float)s / 9) * PI_F * 0.5f;
            pts[n++] = (StrokePoint){cx[k] + mcos(a) * r, cy[k] + msin(a) * r, color};
        }
    stroke_draw(f->stroker, pts, n, 1);
}

// A cassette playing: the piece's name on its label, and through the window the reels turning while the tape
// winds from the left spool onto the right as the song goes by, running past the head along the bottom.
void cassette_draw(void *state, const Frame *f) {
    Cassette *c = state;
    Grid *g = f->grid;
    float w = mmin(1.25f, g->aspect * 0.92f), h = w * 0.62f;
    Rgbf shell = palette_ramp(f->palette, 0.7f), paper = palette_accent(f->palette, 1),
         ink = palette_ramp(f->palette, 0.25f);
    Rgbf tape = palette_ramp(f->palette, 0.35f), hub = palette_ramp(f->palette, 0.95f);
    // The tape packs go down first: resolving the canvas rewrites the whole grid.
    float wy = -h * 0.25f, wh = h * 0.28f, ww = w * 0.62f, length = mmax(f->score->length, 1),
          progress = saturate(f->t / length);
    Canvas *cv = f->canvas;
    canvas_clear(cv);
    for (int side = 0; side < 2; side++)
        canvas_disc(cv, (side ? 1 : -1) * w * 0.48f, wy, 0.09f + 0.17f * (side ? progress : 1 - progress), 0.85f, tape);
    frame_resolve(f);
    dust_stars(f, c->salt, 0.08f, 0.35f);
    rounded_box(f, -w, -h, w, h, 0.08f, shell);
    // The label: a band of paper with ruled lines and the title written on it.
    int top = cell_row(g, h * 0.86f), bottom = cell_row(g, h * 0.18f);
    int left = cell_col(g, -w * 0.88f), right = cell_col(g, w * 0.88f);
    for (int row = top; row <= bottom; row++)
        for (int col = left; col <= right; col++)
            put_cell(g, col, row,
                     row == top || row == bottom ? '_'
                     : (row - top) % 2           ? ' '
                                                 : '.',
                     frame_dim(f, paper, 0.55f));
    const char *title = f->score->info->title;
    int len = (int)strlen(title), trow = (top + bottom) / 2, tcol = (left + right - len) / 2;
    for (int i = 0; i < len; i++) put_cell(g, tcol + i, trow, title[i], ink);
    put_cell(g, left + 1, trow, 'A', paper);
    // The window and the two reels.
    rounded_box(f, -ww, wy - wh, ww, wy + wh, 0.06f, frame_dim(f, shell, 0.7f));
    for (int side = 0; side < 2; side++) {
        float x = (side ? 1 : -1) * w * 0.48f, start = side ? 0.09f : 0.26f, gain = side ? 0.17f : -0.17f;
        float pack = start + gain * progress;
        // Tape runs at a steady speed, so a reel turns faster the less it holds: the angle is the integral of
        // speed / pack. Both turn anticlockwise, drawing the tape left to right past the head.
        float spin = 0.9f * length / gain * mlog2(pack / start) * 0.6931472f;
        for (int k = 0; k < 3; k++) {
            float a = spin + (float)k * TAU_F / 3, at = 0.07f + (pack - 0.07f) * 0.6f;
            put_cell(g, cell_col(g, x + mcos(a) * at), cell_row(g, wy + msin(a) * at), '.', frame_dim(f, tape, 1.8f));
        }
        circle(f, x, wy, 0.07f, hub, 14);
        for (int k = 0; k < 6; k++) {
            float a = spin + (float)k * TAU_F / 6;
            line(f, x + mcos(a) * 0.025f, wy + msin(a) * 0.025f, x + mcos(a) * 0.065f, wy + msin(a) * 0.065f, hub);
        }
    }
    // The tape's run from the left pack down past the head and up onto the right one, marks on it traveling at the
    // tape's speed.
    StrokePoint run[4] = {{-w * 0.48f - (0.26f - 0.17f * progress), wy, tape}, {-w * 0.8f, -h * 0.7f, tape},
                          {w * 0.8f, -h * 0.7f, tape}, {w * 0.48f + 0.09f + 0.17f * progress, wy, tape}};
    stroke_draw(f->stroker, run, 4, 0);
    float legs[3], total = 0;
    for (int k = 0; k < 3; k++) total += legs[k] = mhypot(run[k + 1].x - run[k].x, run[k + 1].y - run[k].y);
    for (int m = 0; m < 6; m++) {
        float s = mfract((float)m / 6 + f->t * 0.9f / total) * total;
        int k = 0;
        while (k < 2 && s > legs[k]) s -= legs[k++];
        float u = s / legs[k];
        put_cell(g, cell_col(g, mixf(run[k].x, run[k + 1].x, u)), cell_row(g, mixf(run[k].y, run[k + 1].y, u)), 'o',
                 frame_dim(f, tape, 1.8f));
    }
    // The head opening along the bottom, and a screw in each corner.
    for (int k = -2; k <= 2; k++)
        put_cell(g, cell_col(g, (float)k * w * 0.18f), cell_row(g, -h * 0.82f), k ? 'o' : '#',
                 frame_dim(f, shell, 0.8f));
    for (int k = 0; k < 4; k++)
        put_cell(g, cell_col(g, (k & 1 ? 1 : -1) * w * 0.93f), cell_row(g, (k & 2 ? 1 : -1) * h * 0.88f), '+', hub);
    f->fx->glow = 0.5f;
}

/* ---------- Music box comb ---------- */

#define TEETH 26

typedef struct {
    float window;
    uint32_t salt;
} Cylinder;

void cylinder_setup(void *state, Rng *r, const Frame *f) {
    Cylinder *c = state;
    c->window = rng_between(r, 2.2f, 3.2f);
    c->salt = rng_u32(r);
    (void)f;
}

static int tooth_of(int note) {
    return clampi((note - 37) * TEETH / 48, 0, TEETH - 1);
}

// Inside a music box: the pinned cylinder turns toward the comb, its pins are the melody's coming notes, and each
// tooth rings when its pin plucks it.
void cylinder_draw(void *state, const Frame *f) {
    Cylinder *cy = state;
    Grid *g = f->grid;
    float span = g->aspect * 0.82f, center = 0.25f, radius = 0.42f;
    Rgbf brass = palette_accent(f->palette, 1), steel = palette_ramp(f->palette, 0.75f),
         pin = palette_ramp(f->palette, 1.0f);
    // The drum: brushed metal turning toward the comb at the pins' pace, brightest across its middle where it faces us.
    int r0 = cell_row(g, center + radius), r1 = cell_row(g, center - radius), c0 = cell_col(g, -span),
        c1 = cell_col(g, span);
    float turned = f->t * PI_F / cy->window;
    for (int row = r0; row <= r1; row++) {
        float v = (cell_y(g, row) - center) / radius, face = msqrt(saturate(1 - v * v));
        int band = mfract((matan2(face, -v) + turned) * 6 / PI_F) < 0.25f;
        for (int col = c0; col <= c1; col++) {
            int edge = col == c0 || col == c1;
            put_cell(g, col, row,
                     edge   ? (col == c0 ? '(' : ')')
                     : band ? '~'
                            : '-',
                     frame_dim(f, brass, 0.2f + 0.45f * face));
        }
    }
    static const Role sung[4] = {ROLE_LEAD, ROLE_BELL, ROLE_PLUCK, ROLE_ARP};
    float ring[TEETH] = {0};
    for (int v = 0; v < 4; v++) {
        // Coming notes ride the drum as pins, rising into view at the top and turning down to the comb.
        const ScoreEvent *ahead[48];
        int n = recent_notes(f->score, sung[v], f->t + cy->window, cy->window, ahead, 48);
        for (int i = 0; i < n; i++) {
            float phase = (ahead[i]->time - f->t) / cy->window;
            if (phase < 0 || phase > 1) continue;
            float y = center - radius * mcos(phase * PI_F), face = msin(phase * PI_F);
            float x = ((float)tooth_of(ahead[i]->note) + 0.5f) / TEETH * 2 * span - span;
            put_cell(g, cell_col(g, x), cell_row(g, y), face > 0.5f ? 'o' : '.',
                     frame_dim(f, pin, 0.35f + 0.65f * face));
        }
        const ScoreEvent *played[16];
        n = recent_notes(f->score, sung[v], f->t, 0.8f, played, 16);
        for (int i = 0; i < n; i++) {
            int k = tooth_of(played[i]->note);
            ring[k] = mmax(ring[k],
                           played[i]->velocity * mexp(-(f->t - played[i]->time) / 0.3f) * (0.35f + 0.65f * f->react));
        }
    }
    // The comb: a bar under the drum, and teeth that shorten toward the high notes.
    int bar_row = r1 + 1;
    for (int col = c0; col <= c1; col++) put_cell(g, col, bar_row, '=', steel);
    for (int k = 0; k < TEETH; k++) {
        float x = ((float)k + 0.5f) / TEETH * 2 * span - span;
        int length = 3 + (TEETH - k) * 6 / TEETH, col = cell_col(g, x);
        for (int j = 1; j <= length; j++) {
            float shake = ring[k] * (float)j / (float)length;
            int offset = shake > 0.25f ? ((int)(f->t * 40) + j) % 2 : 0;
            Rgbf c = rgbf_mix(frame_dim(f, steel, 0.7f), pin, saturate(ring[k] * 1.5f));
            put_cell(g, col + offset, bar_row + j, offset ? '/' : '|', c);
        }
    }
    // The governor's little fan on the side, spinning steadily.
    float fx = span + 0.18f, fan = f->t * 3.0f;
    for (int k = 0; k < 4; k++)
        line(f, fx, center, fx + mcos(fan + (float)k * PI_F * 0.5f) * 0.12f,
             center + msin(fan + (float)k * PI_F * 0.5f) * 0.12f, brass);
    f->fx->glow = 0.55f;
}

/* ---------- Mobile ---------- */

typedef struct {
    float spin, tilt, kinds[4];
    uint32_t salt;
} Mobile;

void mobile_setup(void *state, Rng *r, const Frame *f) {
    Mobile *m = state;
    m->spin = rng_between(r, 0.15f, 0.3f) * (rng_chance(r, 50) ? 1 : -1);
    m->tilt = rng_between(r, 0.12f, 0.22f);
    for (int k = 0; k < 4; k++) m->kinds[k] = (float)rng_range(r, 0, 4);
    m->salt = rng_u32(r);
    (void)f;
}

static void sprite(Grid *g, int col, int row, const char *const *rows, int count, Rgbf color) {
    for (int r = 0; r < count; r++) {
        int w = (int)strlen(rows[r]);
        for (int c = 0; c < w; c++)
            if (rows[r][c] != ' ') put_cell(g, col + c - w / 2, row + r - count / 2, rows[r][c], color);
    }
}

// A nursery mobile turning slowly over a cot: a moon, stars and clouds on strings, swinging round so the far ones
// pass small and dim behind the near.
void mobile_draw(void *state, const Frame *f) {
    Mobile *m = state;
    Grid *g = f->grid;
    dust_stars(f, m->salt, 0.0f, 0.5f);
    static const char *const moon[5] = {"  _.-.", " /  .'", "|  (  ", " \\  `.", "  `-.'"};
    static const char *const star[5] = {"   .   ", "  /_\\  ", "<_   _>", "  / \\  ", "  '-'  "};
    static const char *const cloud[5] = {"   .--.   ", " .(    ). ", "(___.__)_)", "          ", "          "};
    static const char *const planet[5] = {"  .--.  ", " /    \\ ", "=|====|=", " \\    / ", "  `--'  "};
    static const char *const *const kinds[4] = {moon, star, cloud, planet};
    Rgbf string = palette_ramp(f->palette, 0.4f);
    float top = 0.95f, hub = 0.55f, angle = f->t * m->spin;
    line(f, 0, top, 0, hub, string);
    put_cell(g, cell_col(g, 0), cell_row(g, top), 'o', palette_ramp(f->palette, 0.8f));
    // Ornaments, drawn back to front.
    int order[4] = {0, 1, 2, 3};
    float depth[4];
    for (int k = 0; k < 4; k++) depth[k] = msin(angle + (float)k * PI_F * 0.5f);
    for (int a = 1; a < 4; a++)
        for (int b = a; b > 0 && depth[order[b]] > depth[order[b - 1]]; b--) {
            int t = order[b];
            order[b] = order[b - 1];
            order[b - 1] = t;
        }
    for (int i = 0; i < 4; i++) {
        int k = order[i];
        float a = angle + (float)k * PI_F * 0.5f, near = 0.5f - 0.5f * depth[k];
        float x = mcos(a) * g->aspect * 0.6f, arm_y = hub - m->tilt * depth[k];
        float drop = 0.35f + 0.18f * (float)(k % 2) + 0.03f * msin(f->t * 0.9f + (float)k);
        line(f, 0, hub, x, arm_y, string);
        line(f, x, arm_y, x, arm_y - drop, frame_dim(f, string, 0.6f + 0.4f * near));
        Rgbf color = rgbf_mix(palette_accent(f->palette, k), palette_ramp(f->palette, 1.0f), 0.3f);
        sprite(g, cell_col(g, x), cell_row(g, arm_y - drop - 0.12f), kinds[(int)m->kinds[k]], 5,
               frame_dim(f, color, 0.35f + 0.65f * near));
    }
    // The cot's rail along the bottom.
    for (int col = 0; col < g->cols; col++) {
        put_cell(g, col, g->rows - 4, '=', frame_dim(f, string, 0.8f));
        if (col % 6 == 3)
            for (int row = g->rows - 3; row < g->rows; row++) put_cell(g, col, row, '|', frame_dim(f, string, 0.6f));
    }
    f->fx->glow = 0.55f;
}

/* ---------- Gears ---------- */

typedef struct {
    int count, teeth[5];
    float x[5], y[5], radius[5], speed;
} Gears;

void gears_setup(void *state, Rng *r, const Frame *f) {
    Gears *gs = state;
    gs->count = rng_range(r, 3, 6);
    gs->speed = rng_between(r, 0.25f, 0.45f);
    float x = -f->grid->aspect * 0.55f, y = rng_between(r, -0.2f, 0.2f), heading = rng_between(r, -0.6f, 0.6f);
    for (int i = 0; i < gs->count; i++) {
        gs->teeth[i] = rng_range(r, 8, 15);
        gs->radius[i] = (float)gs->teeth[i] * 0.034f;
        if (i) {
            // Each gear sits on the last one's rim, so their teeth mesh.
            float d = gs->radius[i - 1] + gs->radius[i] + 0.03f;
            heading += rng_between(r, -1.0f, 1.0f);
            x += mcos(heading) * d;
            y = clampf(y + msin(heading) * d, -0.6f, 0.6f);
        }
        gs->x[i] = x, gs->y[i] = y;
    }
}

// Clockwork: brass gears meshed in a chain, each turning the next the other way, smaller ones faster.
void gears_draw(void *state, const Frame *f) {
    Gears *gs = state;
    float turn = f->t * gs->speed;
    for (int i = 0; i < gs->count; i++) {
        float spin = turn * gs->radius[0] / gs->radius[i] * (i & 1 ? -1 : 1) + (i & 1 ? PI_F / (float)gs->teeth[i] : 0);
        Rgbf metal = rgbf_mix(palette_accent(f->palette, 1), palette_ramp(f->palette, 0.9f), (float)(i % 2) * 0.4f);
        StrokePoint pts[64];
        int n = gs->teeth[i] * 4 > 64 ? 64 : gs->teeth[i] * 4;
        for (int k = 0; k < n; k++) {
            // A trapezoid tooth: up the flank, across the top, down the other flank, along the root.
            static const float at[4] = {0.0f, 0.2f, 0.45f, 0.65f};
            float a = spin + ((float)(k / 4) + at[k % 4]) / (float)gs->teeth[i] * TAU_F;
            float r = gs->radius[i] * (k % 4 == 1 || k % 4 == 2 ? 1.0f : 0.84f);
            pts[k] = (StrokePoint){gs->x[i] + mcos(a) * r, gs->y[i] + msin(a) * r, metal};
        }
        stroke_draw(f->stroker, pts, n, 1);
        circle(f, gs->x[i], gs->y[i], gs->radius[i] * 0.55f, frame_dim(f, metal, 0.6f), 24);
        circle(f, gs->x[i], gs->y[i], gs->radius[i] * 0.14f, metal, 10);
        for (int s = 0; s < 4; s++) {
            float a = spin + (float)s * PI_F * 0.5f;
            line(f, gs->x[i] + mcos(a) * gs->radius[i] * 0.14f, gs->y[i] + msin(a) * gs->radius[i] * 0.14f,
                 gs->x[i] + mcos(a) * gs->radius[i] * 0.55f, gs->y[i] + msin(a) * gs->radius[i] * 0.55f,
                 frame_dim(f, metal, 0.7f));
        }
    }
    f->fx->glow = 0.45f;
}

/* ---------- Jungle canopy ---------- */

#define LEAVES 12
#define CANOPY_BEAM_SAMPLES 128

typedef struct {
    float x[LEAVES], y[LEAVES], angle[LEAVES], length[LEAVES], width[LEAVES], phase[LEAVES];
    uint32_t salt;
} Canopy;

void canopy_setup(void *state, Rng *r, const Frame *f) {
    Canopy *c = state;
    float aspect = f->grid->aspect;
    for (int i = 0; i < LEAVES; i++) {
        // Leaves hang in from the top and the sides, pointing down and inward.
        int side = i % 3;
        c->x[i] =
            side == 0 ? rng_between(r, -1, 1) * aspect : (side == 1 ? -aspect : aspect) * rng_between(r, 0.85f, 1.05f);
        c->y[i] = side == 0 ? rng_between(r, 0.85f, 1.1f) : rng_between(r, -0.2f, 0.9f);
        float inward = side == 1 ? -0.6f : side == 2 ? -2.5f : -1.57f;
        c->angle[i] = inward + rng_between(r, -0.5f, 0.5f);
        c->length[i] = rng_between(r, 0.45f, 0.8f);
        c->width[i] = rng_between(r, 0.12f, 0.2f);
        c->phase[i] = rng_float(r) * TAU_F;
    }
    c->salt = rng_u32(r);
}

// Under a rainforest canopy: sunbeams slanting down through the gaps and slowly shifting, broad leaves hanging in from
// above and the sides, veined and stirring in the breeze, drips falling from their tips, mist lying low, and a pair of
// butterflies wandering through.
void canopy_draw(void *state, const Frame *f) {
    Canopy *c = state;
    Grid *g = f->grid;
    Rgbf dark = palette_ramp(f->palette, 0.3f), light = palette_ramp(f->palette, 0.8f),
         drip = palette_ramp(f->palette, 0.95f);
    // Beams lean about twenty degrees off upright, each a soft band that brightens and fades on its own, shown as the
    // dust and damp lit up in the air, drifting slowly down.
    const float lean_c = 0.94f, lean_s = 0.34f, reach = g->aspect * lean_c + lean_s;
    Rgbf sun = rgbf_mix(drip, palette_accent(f->palette, 1), 0.35f);
    // The beams vary only across their own slant, so they're sampled once per frame along it.
    float beams[CANOPY_BEAM_SAMPLES];
    for (int k = 0; k < CANOPY_BEAM_SAMPLES; k++) {
        float across = ((float)k / (CANOPY_BEAM_SAMPLES - 1) * 2 - 1) * reach;
        beams[k] = smoothstep(0.6f, 0.82f, noise_value(across * 4 + (float)(c->salt % 47), f->t * 0.06f));
    }
    for (int row = 0; row < g->rows; row++)
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), y = cell_y(g, row), across = x * lean_c - y * lean_s;
            float beam =
                beams[(int)((across / reach * 0.5f + 0.5f) * (CANOPY_BEAM_SAMPLES - 1) + 0.5f)] * (0.5f + 0.3f * y);
            if (beam <= 0.05f) continue;
            uint32_t mote = (uint32_t)(row + (int)mfloor(f->t * 0.8f + hash01((uint32_t)col * 7919u) * 9) * 61) * 977u +
                            (uint32_t)col;
            if (hash01(mote ^ c->salt) < beam * 1.1f)
                put_cell(g, col, row, hash01(mote * 3u) < beam * 0.5f ? ':' : '.',
                         frame_dim(f, sun, 0.25f + 0.45f * beam));
        }
    // Mist along the floor.
    for (int row = g->rows * 3 / 4; row < g->rows; row++)
        for (int col = 0; col < g->cols; col++) {
            float m = noise_fbm(cell_x(g, col) * 1.2f + f->t * 0.03f, cell_y(g, row) * 3.0f + 7);
            if (m > 0.55f) put_cell(g, col, row, m > 0.65f ? '~' : '-', frame_dim(f, light, 0.25f + (m - 0.55f)));
        }
    for (int i = 0; i < LEAVES; i++) {
        float sway = 0.08f * msin(f->t * 0.5f + c->phase[i]) + 0.025f * msin(f->t * 1.9f + c->phase[i] * 2),
              a = c->angle[i] + sway;
        float dx = mcos(a), dy = msin(a), tipx = c->x[i] + dx * c->length[i], tipy = c->y[i] + dy * c->length[i];
        // Bounding box, then each cell tests itself against the leaf's lens shape.
        float minx = mmin(c->x[i], tipx) - c->width[i], maxx = mmax(c->x[i], tipx) + c->width[i];
        float miny = mmin(c->y[i], tipy) - c->width[i], maxy = mmax(c->y[i], tipy) + c->width[i];
        for (int row = cell_row(g, maxy); row <= cell_row(g, miny); row++)
            for (int col = cell_col(g, minx); col <= cell_col(g, maxx); col++) {
                float px = cell_x(g, col) - c->x[i], py = cell_y(g, row) - c->y[i];
                float along = (px * dx + py * dy) / c->length[i], across = -px * dy + py * dx;
                if (along < 0 || along > 1) continue;
                float half = c->width[i] * msin(along * PI_F) * (1 - 0.3f * along);
                if (mabs(across) > half) continue;
                // The midrib, then veins slanting back from it on either side.
                int glyph = mabs(across) < 0.025f           ? (mabs(dx) > mabs(dy) ? '-' : '|')
                            : (across > 0) == (dx * dy > 0) ? '/'
                                                            : '\\';
                float shade = 0.5f + 0.5f * (1 - mabs(across) / (half + 0.001f));
                put_cell(g, col, row, glyph, rgbf_mix(dark, light, shade * (0.6f + 0.4f * along)));
            }
        // A drop gathers at the tip and falls now and then.
        float period = 1.8f + 0.8f * (float)(i % 4), age = mfract((f->t + c->phase[i]) / period) * period;
        if (age < 1.2f)
            put_cell(g, cell_col(g, tipx), cell_row(g, tipy - age * age * 0.8f), age < 0.2f ? 'o' : '.', drip);
    }
    for (int k = 0; k < 2; k++) {
        float p = c->phase[k] + (float)k * 2.1f, t = f->t;
        float x = (0.6f * msin(t * 0.21f + p) + 0.25f * msin(t * 0.67f + p * 1.7f)) * g->aspect;
        float y = 0.35f * msin(t * 0.29f + p * 0.6f) + 0.12f * msin(t * 1.1f + p) - 0.05f;
        int col = cell_col(g, x), row = cell_row(g, y), open = (int)mfloor(t * 5 + (float)k * 0.5f) & 1;
        Rgbf wing = palette_accent(f->palette, k ? 2 : 0);
        put_cell(g, col, row, open ? '}' : ']', wing);
        put_cell(g, col + 1, row, open ? '{' : '[', wing);
    }
    f->fx->glow = 0.4f;
}

/* ---------- Light trails ---------- */

typedef struct {
    float speed, horizon;
    uint32_t salt;
} Trails;

void trails_setup(void *state, Rng *r, const Frame *f) {
    Trails *t = state;
    t->speed = rng_between(r, 8, 13);
    t->horizon = rng_between(r, 0.05f, 0.22f);
    t->salt = rng_u32(r);
    (void)f;
}

// A highway at night in a long exposure: headlights streaming toward us on one side, tail lights away on the other.
// The kick floors it, and the snare sends a fresh wave of cars down the lanes.
void trails_draw(void *state, const Frame *f) {
    Trails *t = state;
    Grid *g = f->grid;
    dust_stars(f, t->salt, 0.0f, 0.4f);
    float travel = frame_travel(f, t->speed * 0.6f, t->speed) + frame_surge(f, ROLE_KICK, 0.3f, 2.5f);
    float hz = t->horizon;
    // A skyline far away on the horizon.
    for (int col = 0; col < g->cols; col++) {
        float h = noise_fbm(cell_x(g, col) * 2.5f + (float)(t->salt % 77), 3) * 0.18f;
        for (int row = cell_row(g, hz + h); row <= cell_row(g, hz); row++)
            put_cell(g, col, row, hash01((uint32_t)(row * 131 + col) ^ t->salt) < 0.08f ? '.' : ':',
                     frame_dim(f, palette_ramp(f->palette, 0.3f), 0.6f));
    }
    Rgbf lane = palette_ramp(f->palette, 0.35f), head = palette_ramp(f->palette, 1.0f),
         tail = palette_accent(f->palette, 0);
    // Road edges and dashed lane lines, rushing toward us.
    for (int l = -3; l <= 3; l += 2) {
        float x = (float)l * 0.55f;
        StrokePoint edge[2] = {{x * 0.02f, hz, lane}, {x * 1.6f, -1.1f, lane}};
        if (l == -3 || l == 3) stroke_draw(f->stroker, edge, 2, 0);
        else
            for (int d = 0; d < 10; d++) {
                float z0 = 1 + mfract(-travel * 0.05f + (float)d / 10) * 20, z1 = z0 + 0.8f;
                line(f, x / z0, hz - 1.0f / z0, x / z1, hz - 1.0f / z1, frame_dim(f, lane, 0.5f + 0.5f / z0 * 3));
            }
    }
    float surge = frame_pulse(f, ROLE_SNARE, 0.6f, 1.0f);
    for (int k = 0; k < 44; k++) {
        uint32_t id = t->salt + (uint32_t)k * 2654435761u;
        int oncoming = k % 2;
        float x = (oncoming ? -1.0f : 1.0f) * (0.2f + 0.6f * hash01(id));
        float len = 3.0f + 5 * hash01(id + 1) * (1 + surge);
        float z = 0.9f + mfract(hash01(id + 2) + (oncoming ? travel : -travel * 0.7f) / 30) * 22;
        float near = saturate(2.5f / z);
        Rgbf c = oncoming ? head : tail;
        for (int lamp = 0; lamp < 2; lamp++) {
            float lx = x + (lamp ? 0.06f : -0.06f);
            line(f, lx / z, hz - 1.0f / z, lx / (z + len), hz - 1.0f / (z + len), frame_dim(f, c, 0.55f + 0.9f * near));
        }
    }
    f->fx->glow = 0.7f;
}

/* ---------- Sound system ---------- */

typedef struct {
    int columns, rows;
    uint32_t salt;
} Speakers;

void speakers_setup(void *state, Rng *r, const Frame *f) {
    Speakers *s = state;
    s->columns = rng_range(r, 2, 4);
    s->rows = 2;
    s->salt = rng_u32(r);
    (void)f;
}

// A stack of speaker cabinets playing the whole piece: the upper cones push out on the kick and the tune, the lower on
// the bass and the pads, every note sends a ring of air off its cone, and the tweeters glint on the hats.
void speakers_draw(void *state, const Frame *f) {
    static const Role UPPER[4] = {ROLE_KICK, ROLE_LEAD, ROLE_BELL, ROLE_ARP},
                      LOWER[4] = {ROLE_BASS, ROLE_PAD, ROLE_PLUCK, ROLE_CHORD};
    Speakers *s = state;
    Grid *g = f->grid;
    float kick = frame_pulse(f, ROLE_KICK, 0.18f, 1.0f), bass = frame_pulse(f, ROLE_BASS, 0.25f, 0.7f),
          hat = frame_pulse(f, ROLE_HAT, 0.08f, 1.0f);
    float tune = frame_pulse(f, ROLE_LEAD, 0.2f, 0.5f) + frame_pulse(f, ROLE_BELL, 0.25f, 0.5f) +
                 frame_pulse(f, ROLE_ARP, 0.12f, 0.3f);
    float swell = frame_pulse(f, ROLE_PAD, 0.8f, 0.35f) + frame_pulse(f, ROLE_PLUCK, 0.2f, 0.4f) +
                  frame_pulse(f, ROLE_CHORD, 0.3f, 0.5f);
    // Air keeps rolling off the cones between notes too, faster as the music gathers energy.
    float air = frame_travel(f, 0.6f, 1.2f);
    Rgbf box = palette_ramp(f->palette, 0.4f), cone = palette_accent(f->palette, 0),
         cap = palette_ramp(f->palette, 0.95f);
    float cw = g->aspect * 1.7f / (float)s->columns, ch = 1.7f / (float)s->rows;
    for (int i = 0; i < s->columns; i++)
        for (int j = 0; j < s->rows; j++) {
            float x0 = -g->aspect * 0.85f + (float)i * cw, y1 = 0.85f - (float)j * ch, cx = x0 + cw * 0.5f,
                  cy = y1 - ch * 0.56f;
            StrokePoint frame[4] = {{x0 + 0.02f, y1, box},
                                    {x0 + cw - 0.02f, y1, box},
                                    {x0 + cw - 0.02f, y1 - ch + 0.03f, box},
                                    {x0 + 0.02f, y1 - ch + 0.03f, box}};
            stroke_draw(f->stroker, frame, 4, 1);
            float push = mmin(1, j ? bass + swell : kick + tune), r = mmin(cw, ch) * 0.34f;
            circle(f, cx, cy, r * 1.08f, frame_dim(f, box, 0.8f), 32);
            circle(f, cx, cy, r * (0.86f + 0.12f * push), frame_dim(f, cone, 0.6f + 0.6f * push), 32);
            circle(f, cx, cy, r * (0.45f + 0.1f * push), frame_dim(f, cone, 0.5f + 0.5f * push), 24);
            circle(f, cx, cy, r * 0.16f, cap, 12);
            float roll = mfract(air + (float)(i * 2 + j) * 0.37f);
            circle(f, cx, cy, r * (1.12f + 0.35f * roll), frame_dim(f, cone, 0.4f * (1 - roll)), 40);
            for (int k = 0; k < 4; k++) {
                const ScoreEvent *e = score_last(f->score, j ? LOWER[k] : UPPER[k], f->t);
                float age = e ? (f->t - e->time) / 0.5f : 1;
                if (age < 1)
                    circle(f, cx, cy, r * (1.12f + 0.35f * age), frame_dim(f, cone, (1 - age) * 0.8f * frame_bright(f)),
                           40);
            }
            float tx = cx + cw * 0.3f, ty = y1 - ch * 0.14f;
            put_cell(g, cell_col(g, tx), cell_row(g, ty), hat > 0.2f ? '*' : 'o', frame_dim(f, cap, 0.5f + 0.5f * hat));
        }
    f->fx->glow = 0.55f + 0.25f * kick;
}

/* ---------- Lightning ---------- */

typedef struct {
    float cloud_base, ground;
    uint32_t salt;
} Lightning;

void lightning_setup(void *state, Rng *r, const Frame *f) {
    Lightning *l = state;
    l->cloud_base = rng_between(r, 0.35f, 0.55f);
    l->ground = rng_between(r, -0.8f, -0.65f);
    l->salt = rng_u32(r);
    (void)f;
}

static void bolt(const Frame *f, uint32_t id, float x, float top, float bottom, Rgbf color, int branches) {
    StrokePoint pts[24];
    int n = 0;
    float y = top;
    while (y > bottom && n < 24) {
        pts[n++] = (StrokePoint){x, y, color};
        y -= 0.09f + 0.05f * hash01(id + (uint32_t)n);
        x += (hash01(id * 3u + (uint32_t)n) - 0.5f) * 0.22f;
        if (branches && n == 5) bolt(f, id * 7u + 1, x, y, mixf(y, bottom, 0.5f), rgbf_scale(color, 0.6f), 0);
    }
    stroke_draw(f->stroker, pts, n, 0);
}

static void storm_corner(const void *ctx, const Frame *f, float x, float y, float *ink, Rgbf *color) {
    const Lightning *l = ctx;
    float m = noise_fbm(x * 1.4f + f->t * 0.06f + (float)(l->salt % 91), y * 2.4f) + (y - l->cloud_base) * 0.7f;
    *ink = smoothstep(0.42f, 0.75f, m) * 0.9f;
    *color = rgbf_mix(palette_ramp(f->palette, 0.25f), palette_ramp(f->palette, 0.6f), saturate((m - 0.42f) * 2.5f));
}

// A storm over low hills: clouds churning overhead, rain slanting down, and lightning cracking down on the snare.
// Bolts flash as lines; the sky itself never strobes.
void lightning_draw(void *state, const Frame *f) {
    Lightning *l = state;
    Grid *g = f->grid;
    Rgbf cloud = palette_ramp(f->palette, 0.35f), rain = palette_ramp(f->palette, 0.5f),
         white = palette_ramp(f->palette, 1.0f);
    float glow = 0;
    canvas_clear(f->canvas);
    canvas_from_corners(f, storm_corner, l);
    frame_resolve(f);
    const ScoreEvent *hits[8];
    int n = f->react > 0 ? recent_notes(f->score, ROLE_SNARE, f->t, 0.45f, hits, 8) : 0;
    float bolt_x = 0;
    for (int i = 0; i < n; i++) {
        uint32_t id = (uint32_t)(hits[i]->time * 1000.0f) ^ l->salt;
        if (hash01(id) > 0.6f) continue;
        float age = f->t - hits[i]->time, bright = (1 - age / 0.45f) * frame_bright(f);
        if (bright <= 0) continue;
        float x = (hash01(id + 1) * 1.6f - 0.8f) * g->aspect;
        bolt(f, id, x, l->cloud_base, l->ground,
             frame_dim(f, rgbf_mix(white, palette_accent(f->palette, 2), 0.3f), bright * 1.2f), 1);
        if (bright > glow) {
            glow = bright;
            bolt_x = x;
        }
    }
    // The cloud near the latest bolt lights from inside.
    if (glow > 0)
        for (int row = 0; row <= cell_row(g, l->cloud_base); row++)
            for (int col = 0; col < g->cols; col++) {
                Cell *cell = grid_at(g, col, row);
                float lit = glow * mexp(-mabs(cell_x(g, col) - bolt_x) * 2.5f) * 0.6f;
                if (cell->glyph != ' ' && lit > 0.05f) cell->fg = rgb_from_f(rgbf_mix(rgbf(cell->fg), white, lit));
            }
    (void)cloud;
    for (int k = 0; k < g->cols * g->rows / 14; k++) {
        uint32_t id = l->salt + (uint32_t)k * 2246822519u;
        float x = (hash01(id) * 2.2f - 1.1f) * g->aspect, y = 1 - mfract(hash01(id + 1) + f->t * 0.9f) * 2.2f;
        x += (1 - y) * 0.15f;
        if (y < l->ground || y > l->cloud_base) continue;
        put_cell_under(g, cell_col(g, x), cell_row(g, y), '/', frame_dim(f, rain, 0.55f));
    }
    StrokePoint hills[48];
    for (int i = 0; i < 48; i++) {
        float x = -g->aspect + (float)i / 47 * 2 * g->aspect;
        hills[i] = (StrokePoint){x, l->ground + 0.12f * noise_fbm(x * 0.9f + 5, 2), palette_ramp(f->palette, 0.45f)};
    }
    stroke_draw(f->stroker, hills, 48, 0);
    f->fx->glow = 0.5f + glow * 0.3f;
}

/* ---------- Dunes ---------- */

typedef struct {
    float seed, moon_x, wind;
} Dunes;

void dunes_setup(void *state, Rng *r, const Frame *f) {
    Dunes *d = state;
    d->seed = rng_between(r, 0, 100);
    d->moon_x = rng_between(r, -0.6f, 0.6f) * f->grid->aspect;
    d->wind = rng_between(r, 0.04f, 0.08f);
}

static float dune_height(const Dunes *d, int layer, float x) {
    float s = d->seed + (float)layer * 17, base = -0.15f - (float)layer * 0.28f;
    return base + 0.16f * msin(x * (1.3f + 0.4f * (float)layer) + s) + 0.08f * msin(x * 3.1f + s * 2) +
           0.05f * (noise_fbm(x * 0.8f + s, 3) - 0.5f);
}

// A camel walking right, its legs in two strides.
static const char *const CAMEL[2][3] = {{" .^.  o", "(___)/ ", " | |   "}, {" .^.  o", "(___)/ ", " / \\   "}};

// Dunes under a moon: three ridges, the far ones paler, rippled sand creeping down their flanks, the wind lifting
// trails of grains off each crest, and a caravan of camels plodding along the middle ridge.
void dunes_draw(void *state, const Frame *f) {
    Dunes *d = state;
    Grid *g = f->grid;
    dust_stars(f, (uint32_t)(d->seed * 911), 0.0f, 0.7f);
    moon_disc(f, d->moon_x, 0.55f, 0.15f, palette_ramp(f->palette, 1.0f), (uint32_t)(d->seed * 37), 0);
    static const char ripple[] = "~-~-=~-~";
    for (int layer = 0; layer < 3; layer++) {
        Rgbf sand = palette_ramp(f->palette, 0.65f - 0.15f * (float)layer),
             shade = palette_ramp(f->palette, 0.3f - 0.06f * (float)layer),
             moonlit = palette_ramp(f->palette, 0.95f - 0.1f * (float)layer);
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), h = dune_height(d, layer, x), slope = dune_height(d, layer, x + 0.05f) - h;
            // The flanks facing the moon, falling away toward it, catch its light; the far ones fall into shade, and
            // the curve stays soft so the crests still blend.
            float lit = 0.5f + 0.5f * mtanh((d->moon_x > x ? -slope : slope) * 70);
            Rgbf flank = lit < 0.5f ? rgbf_mix(shade, sand, lit * 2) : rgbf_mix(sand, moonlit, (lit - 0.5f) * 1.4f);
            for (int row = cell_row(g, h); row < g->rows; row++) {
                float y = cell_y(g, row), depth = h - y;
                if (row == cell_row(g, h)) {
                    put_cell(g, col, row,
                             slope > 0.006f    ? '/'
                             : slope < -0.006f ? '\\'
                                               : '_',
                             flank);
                    continue;
                }
                int i = (int)(mfract(x * 3 + depth * 9 + d->seed - f->t * 0.08f) * 8);
                int glyph = (row + col / 4) % 4 ? ' ' : ripple[i];
                put_cell(g, col, row, glyph, frame_dim(f, flank, 0.5f + 0.3f * (1 - depth)));
            }
            // Grains streaming off the crest downwind.
            if (hash01((uint32_t)col * 2654435761u + (uint32_t)layer) < 0.4f) {
                float drift = mfract(f->t * d->wind * 2 + hash01((uint32_t)col)) * 0.3f;
                for (int k = 0; k < 2; k++)
                    put_cell_under(g, cell_col(g, x + drift - (float)k * 0.035f), cell_row(g, h + 0.03f + drift * 0.2f),
                                   k ? '.' : '\'', frame_dim(f, sand, 0.6f - 0.25f * (float)k));
            }
        }
        if (layer != 1) continue;
        float wrap = 2 * g->aspect + 0.6f;
        for (int k = 0; k < 4; k++) {
            float x = mfract((d->seed * 0.01f + f->t * 0.05f - (float)k * 0.25f + g->aspect + 0.3f) / wrap) * wrap -
                      g->aspect - 0.3f;
            int feet = cell_row(g, dune_height(d, 1, x)) - 1, stride = (int)mfloor(f->t * 1.6f + (float)k * 0.5f) & 1;
            for (int r = 0; r < 3; r++)
                for (int c = 0; CAMEL[stride][r][c]; c++)
                    if (CAMEL[stride][r][c] != ' ')
                        put_cell(g, cell_col(g, x) + c - 3, feet - 2 + r, CAMEL[stride][r][c],
                                 palette_ramp(f->palette, 0.9f));
        }
    }
    f->fx->glow = 0.45f;
}

/* ---------- Kelp forest ---------- */

#define STALKS 7

typedef struct {
    float x[STALKS], height[STALKS], phase[STALKS];
    uint32_t salt;
} Kelp;

void kelp_setup(void *state, Rng *r, const Frame *f) {
    Kelp *k = state;
    for (int i = 0; i < STALKS; i++) {
        k->x[i] = (((float)i + rng_between(r, 0.2f, 0.8f)) / STALKS * 2 - 1) * f->grid->aspect;
        k->height[i] = rng_between(r, 1.0f, 1.7f);
        k->phase[i] = rng_float(r) * TAU_F;
    }
    k->salt = rng_u32(r);
}

// Under the sea: sunbeams slanting down through the water, kelp swaying up from the sand, bubbles rising, and small
// fish crossing at their own depths and speeds.
void kelp_draw(void *state, const Frame *f) {
    Kelp *k = state;
    Grid *g = f->grid;
    Rgbf ray = palette_ramp(f->palette, 0.5f), weed = palette_accent(f->palette, 1),
         fish = palette_ramp(f->palette, 0.9f);
    for (int row = 0; row < g->rows; row++)
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), y = cell_y(g, row);
            float band = msin((x + y * 0.5f) * 5 + f->t * 0.15f) * msin((x + y * 0.5f) * 1.7f - f->t * 0.07f);
            if (band > 0.55f && y > -0.6f)
                put_cell(g, col, row, '/', frame_dim(f, ray, (band - 0.55f) * 1.4f * (0.4f + 0.6f * y)));
        }
    for (int col = 0; col < g->cols; col++)
        put_cell(g, col, g->rows - 1, hash01((uint32_t)col * 977u ^ k->salt) < 0.3f ? '~' : '_',
                 frame_dim(f, ray, 0.8f));
    for (int i = 0; i < STALKS; i++) {
        StrokePoint pts[24];
        for (int s = 0; s < 24; s++) {
            float t = (float)s / 23, y = -1 + t * k->height[i];
            float x = k->x[i] + 0.12f * t * msin(f->t * 0.6f + k->phase[i] + t * 2.5f);
            pts[s] = (StrokePoint){x, y, frame_dim(f, weed, 0.5f + 0.4f * t)};
            if (s % 3 == 1)
                put_cell(g, cell_col(g, x) + (s % 2 ? 1 : -1), cell_row(g, y), s % 2 ? ')' : '(',
                         frame_dim(f, weed, 0.7f));
        }
        stroke_draw(f->stroker, pts, 24, 0);
    }
    for (int b = 0; b < 18; b++) {
        uint32_t id = k->salt + (uint32_t)b * 2246822519u;
        float x = (hash01(id) * 2 - 1) * g->aspect + 0.03f * msin(f->t * 2 + (float)b);
        float y = -1 + mfract(hash01(id + 1) + f->t * (0.05f + 0.05f * hash01(id + 2))) * 2.1f;
        put_cell_under(g, cell_col(g, x), cell_row(g, y), b % 4 ? 'o' : 'O', frame_dim(f, fish, 0.5f));
    }
    for (int s = 0; s < 6; s++) {
        uint32_t id = k->salt * 7u + (uint32_t)s;
        int right = s % 2;
        float speed = 0.08f + 0.1f * hash01(id),
              y = hash01(id + 1) * 1.2f - 0.4f + 0.03f * msin(f->t * 1.5f + (float)s);
        float span = g->aspect * 2.6f, x = mfract(hash01(id + 2) + f->t * speed / span) * span - span * 0.5f;
        if (!right) x = -x;
        int size = (int)(hash01(id + 3) * 3), col = cell_col(g, x), row = cell_row(g, y);
        // A school of small fish, or one bigger one.
        for (int m = 0; m < (size ? 1 : 4); m++) {
            const char *body = size == 2 ? (right ? "><((('>" : "<')))><") : right ? "><>" : "<><";
            int w = (int)strlen(body);
            for (int c = 0; c < w; c++)
                put_cell(g, col + c - w / 2 + m * (right ? -5 : 5), row + (m % 2), body[c], fish);
        }
    }
    f->fx->glow = 0.5f;
}

/* ---------- Meteor shower ---------- */

typedef struct {
    float radiant_x, radiant_y, ridge, band_tilt, band_at;
    uint32_t salt;
} Meteors;

void meteors_setup(void *state, Rng *r, const Frame *f) {
    Meteors *m = state;
    m->radiant_x = rng_between(r, -0.8f, 0.8f) * f->grid->aspect;
    m->radiant_y = rng_between(r, 0.7f, 1.1f);
    m->ridge = rng_between(r, -0.55f, -0.4f);
    m->band_tilt = rng_between(r, -0.9f, 0.9f);
    m->band_at = rng_between(r, 0.1f, 0.45f);
    m->salt = rng_u32(r);
}

// A meteor shower over a lake: streaks fanning out from one point in the sky, each burning out on its way, now and then
// a slow fireball, and the Milky Way arching behind them as the sky turns, all held upside down in the water.
void meteors_draw(void *state, const Frame *f) {
    Meteors *m = state;
    Grid *g = f->grid;
    Rgbf hot = palette_ramp(f->palette, 1.0f), trail = palette_accent(f->palette, 2),
         land = palette_ramp(f->palette, 0.25f);
    float turn = f->t * 0.004f, nx = -msin(m->band_tilt), ny = mcos(m->band_tilt);
    for (int row = 0; row < cell_row(g, m->ridge); row++)
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col) - turn * 8, y = cell_y(g, row), d = x * nx + y * ny - m->band_at;
            if (d * d > 0.06f) continue;
            float clump = noise_value((x * ny - y * nx) * 4 + (float)(m->salt % 89), d * 6),
                  dense = mexp(-d * d * 60) * clump;
            uint32_t id = (uint32_t)(row * 977 + col) ^ m->salt;
            if (hash01(id) < dense * 1.1f)
                put_cell(g, col, row, hash01(id + 1) < 0.35f ? ':' : '.',
                         frame_dim(f, palette_ramp(f->palette, 0.6f + 0.4f * dense), 0.3f + 0.5f * dense));
        }
    dust_stars(f, m->salt, 0.03f, 0.9f);
    for (int k = 0; k < 8; k++) {
        float slot = mfloor(f->t / 0.45f) - (float)k, age = f->t - slot * 0.45f;
        uint32_t id = (uint32_t)slot * 2654435761u ^ m->salt;
        if (hash01(id) < 0.4f) continue;
        // About one in twelve is a fireball: brighter, slower, with a longer tail.
        int fireball = hash01(id + 5) < 0.08f;
        float life = fireball ? 2.2f : 0.5f + 0.5f * hash01(id + 1);
        if (age > life) continue;
        float a = matan2(-1, 0) + (hash01(id + 2) - 0.5f) * 2.2f, dx = mcos(a), dy = msin(a);
        float start = 0.15f + 0.35f * hash01(id + 3), run = start + age * (fireball ? 0.7f : 1.6f);
        float hx = m->radiant_x + dx * run * g->aspect * 0.8f, hy = m->radiant_y + dy * run;
        float fade = 1 - age / life;
        for (int s = 0; s < (fireball ? 14 : 8); s++) {
            float back = (float)s * 0.035f;
            put_cell(
                g, cell_col(g, hx - dx * back * g->aspect * 0.8f), cell_row(g, hy - dy * back),
                s          ? (s < 3 ? (fireball ? '=' : '-') : '.')
                : fireball ? '@'
                           : '*',
                frame_dim(f, s ? trail : hot, fade * (1 - (float)s / (fireball ? 15 : 9)) * (fireball ? 1.3f : 1.0f)));
        }
    }
    // A treeline along the shore, dark against the sky.
    int shore = cell_row(g, m->ridge);
    for (int col = 0; col < g->cols; col++) {
        if (hash01((uint32_t)col * 7919u ^ m->salt) < 0.12f) {
            int top = shore - 3 - (int)(hash01((uint32_t)col * 31u) * 4);
            for (int row = top; row < shore; row++) {
                int w = (row - top) / 2;
                for (int c = -w; c <= w; c++)
                    put_cell(g, col + c, row, c < 0 ? '/' : c > 0 ? '\\' : '^', frame_dim(f, land, 1.3f));
            }
        }
    }
    for (int row = shore + 1; row < g->rows; row++)
        for (int col = 0; col < g->cols; col++) grid_at(g, col, row)->glyph = ' ';
    // The lake holds everything above it upside down, dimmer, broken by ripples drifting across.
    for (int col = 0; col < g->cols; col++) put_cell(g, col, shore, '-', frame_dim(f, land, 1.5f));
    for (int row = shore + 1; row < g->rows; row++) {
        int src = 2 * shore - row;
        int wobble = (int)mfloor(1.6f * msin((float)row * 1.9f + f->t * 1.1f) + 0.5f);
        for (int col = 0; src >= 0 && col < g->cols; col++) {
            const Cell *c = grid_at(g, clampi(col + wobble, 0, g->cols - 1), src);
            if (c->glyph == ' ') continue;
            int glyph = c->glyph == '/'    ? '\\'
                        : c->glyph == '\\' ? '/'
                        : c->glyph == '^'  ? 'v'
                        : c->glyph == '_'  ? '-'
                                           : c->glyph;
            put_cell(g, col, row, glyph, frame_dim(f, rgbf(c->fg), 0.4f));
        }
    }
    f->fx->glow = 0.55f;
}

/* ---------- Reel-to-reel ---------- */

typedef struct {
    float speed;
    uint32_t salt;
} Reels;

void reels_setup(void *state, Rng *r, const Frame *f) {
    Reels *t = state;
    t->speed = rng_between(r, 0.7f, 1.0f);
    t->salt = rng_u32(r);
    (void)f;
}

// One reel: its flange with three windows cut into it, turning, and the tape wound on its hub.
static void reel(const Frame *f, float x, float y, float radius, float pack, float spin) {
    Rgbf metal = palette_ramp(f->palette, 0.8f), tape = palette_ramp(f->palette, 0.35f);
    Canvas *c = f->canvas;
    canvas_disc(c, x, y, pack, 0.8f, tape);
    for (int k = 0; k < 3; k++) {
        float a = spin + (float)k * TAU_F / 3;
        // The flange between windows shows as spokes; the windows themselves are open.
        for (int s = 0; s < 6; s++) {
            float r = radius * (0.25f + 0.13f * (float)s);
            canvas_disc(c, x + mcos(a) * r, y + msin(a) * r, 0.022f, 0.9f, metal);
        }
    }
    canvas_disc(c, x, y, radius * 0.16f, 0.95f, metal);
}

// A studio tape machine: two big reels turning, the tape running down past the heads, and a pair of VU meters whose
// needles swing with the music.
void reels_draw(void *state, const Frame *f) {
    Reels *t = state;
    Grid *g = f->grid;
    float length = mmax(f->score->length, 1), progress = saturate(f->t / length), radius = 0.36f, ry = 0.46f,
          rx = g->aspect * 0.52f;
    canvas_clear(f->canvas);
    for (int side = 0; side < 2; side++) {
        // The tape runs at a steady speed, so a reel turns faster the less it holds: the angle is the integral of
        // speed / fill. The tape path pulls both reels clockwise.
        float start = side ? 0.3f : 0.9f, gain = side ? 0.6f : -0.6f, fill = start + gain * progress;
        float turned = t->speed * length / gain * mlog2(fill / start) * 0.6931472f;
        reel(f, side ? rx : -rx, ry, radius, radius * fill, -turned);
    }
    frame_resolve(f);
    Rgbf metal = palette_ramp(f->palette, 0.75f), tape = palette_ramp(f->palette, 0.45f),
         panel = palette_ramp(f->palette, 0.3f);
    for (int side = 0; side < 2; side++) {
        float x = side ? rx : -rx;
        circle(f, x, ry, radius, metal, 48);
        // Tape from the reel's pack down round a guide roller to the head block.
        float pack = radius * (0.3f + 0.6f * (side ? progress : 1 - progress)), gx = (side ? 1 : -1) * 0.36f,
              gy = -0.08f;
        line(f, x + (side ? -1 : 1) * pack, ry, gx, gy, tape);
        circle(f, gx, gy, 0.04f, metal, 12);
        line(f, gx, gy - 0.04f, (side ? 1 : -1) * 0.12f, -0.16f, tape);
    }
    // The head block.
    StrokePoint head[4] = {
        {-0.14f, -0.12f, metal}, {0.14f, -0.12f, metal}, {0.14f, -0.24f, metal}, {-0.14f, -0.24f, metal}};
    stroke_draw(f->stroker, head, 4, 1);
    for (int k = -1; k <= 1; k++) put_cell(g, cell_col(g, (float)k * 0.08f), cell_row(g, -0.15f), '#', metal);
    // VU meters: how hard the music is hitting, each side its own blend of parts.
    for (int side = 0; side < 2; side++) {
        float cx = (side ? 1 : -1) * g->aspect * 0.42f, cy = -0.62f, w = 0.5f, h = 0.24f;
        StrokePoint box[4] = {
            {cx - w, cy + h, panel}, {cx + w, cy + h, panel}, {cx + w, cy - h, panel}, {cx - w, cy - h, panel}};
        stroke_draw(f->stroker, box, 4, 1);
        float level = side ? frame_pulse(f, ROLE_SNARE, 0.25f, 0.5f) + frame_pulse(f, ROLE_LEAD, 0.4f, 0.4f) +
                                 frame_pulse(f, ROLE_PAD, 0.8f, 0.2f)
                           : frame_pulse(f, ROLE_KICK, 0.25f, 0.6f) + frame_pulse(f, ROLE_BASS, 0.4f, 0.4f);
        level = saturate(0.08f + level * 0.65f + 0.04f * msin(f->t * 3.1f + (float)side));
        // The scale: an arc of ticks, red at the top end.
        for (int k = 0; k <= 10; k++) {
            float a = PI_F * (0.75f - 0.5f * (float)k / 10), tx = cx + mcos(a) * w * 0.8f,
                  ty = cy - h * 0.7f + msin(a) * h * 1.3f;
            put_cell(g, cell_col(g, tx), cell_row(g, ty), k % 5 == 0 ? '|' : '\'',
                     k > 7 ? palette_accent(f->palette, 0) : metal);
        }
        float a = PI_F * (0.75f - 0.5f * level);
        line(f, cx, cy - h * 0.7f, cx + mcos(a) * w * 0.72f, cy - h * 0.7f + msin(a) * h * 1.15f,
             palette_ramp(f->palette, 1.0f));
        const char *label = side ? "R" : "L";
        put_cell(g, cell_col(g, cx), cell_row(g, cy - h * 0.6f), label[0], panel);
    }
    f->fx->glow = 0.5f;
}

/* ---------- Carousel ---------- */

#define HORSES 6

typedef struct {
    float bars_per_turn, sag;
    uint32_t salt;
} Carousel;

void carousel_setup(void *state, Rng *r, const Frame *f) {
    Carousel *c = state;
    c->bars_per_turn = rng_between(r, 6, 10);
    c->sag = rng_between(r, 0.08f, 0.15f);
    c->salt = rng_u32(r);
    (void)f;
}

// A carousel horse facing right, its pole through the saddle at column 4.
static const char *const HORSE[4] = {"    |  ,^, ", " ~._|_/ o )", "  (_|___ _/", "   /|\\  /\\ "};

static char mirror_glyph(char c) {
    switch (c) {
    case '/': return '\\';
    case '\\': return '/';
    case '(': return ')';
    case ')': return '(';
    default: return c;
    }
}

// One horse with its pole at col and its ears at row, turned to face left on the far side.
static void horse(Grid *g, int col, int row, int left, Rgbf body, Rgbf brass) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 11; c++) {
            int src = left ? 10 - c : c;
            char ch = HORSE[r][src];
            if (ch != ' ')
                put_cell(g, col + c - (left ? 6 : 4), row + r, left ? mirror_glyph(ch) : ch, src == 4 ? brass : body);
        }
}

// The mirrored column at the heart of the carousel, its panels turning with it.
static void carousel_column(const Frame *f, float turn, float top, float deck, Rgbf brass, Rgbf cream) {
    Grid *g = f->grid;
    const float half = 0.11f;
    for (int row = cell_row(g, top); row <= cell_row(g, deck); row++)
        for (int col = cell_col(g, -half); col <= cell_col(g, half); col++) {
            float u = cell_x(g, col) / half;
            if (u < -1 || u > 1) continue;
            float a = matan2(u, msqrt(1 - u * u)) - PI_F / 2 - turn;
            int panel = (int)mfloor(a * 8 / TAU_F) & 1, edge = mabs(u) > 0.75f;
            float shade = 0.45f + 0.4f * msqrt(1 - u * u);
            put_cell(g, col, row, edge ? '|' : panel ? ':' : '#', frame_dim(f, edge || panel ? brass : cream, shade));
        }
}

// A fairground carousel at night, turning once every several bars: painted horses rise and fall on brass poles in
// time with the bar, bulbs chase round the canopy on the beat, and strings of lights run out from its flag.
void carousel_draw(void *state, const Frame *f) {
    Carousel *c = state;
    Grid *g = f->grid;
    const float rim = 0.95f, squash = 0.2f, top = 0.3f, peak = 0.68f, deck = -0.58f, orbit = 0.76f;
    const float rise = peak - top, tilt = rim * squash;
    float beat = f->score->beat, bar = beat * (float)f->score->info->beats_per_bar, gentle = f->still ? 0.5f : 1.0f;
    float turn = TAU_F * f->t * gentle / (bar * c->bars_per_turn);
    int beat_no = (int)mfloor(f->t / beat);
    float chase = 0.4f + 0.6f * mexp(-(f->t - (float)beat_no * beat) / (beat * 0.6f)) * frame_bright(f);
    Rgbf brass = palette_ramp(f->palette, 0.8f), cloth = palette_accent(f->palette, 0),
         cream = palette_ramp(f->palette, 0.92f);
    Rgbf bulb = palette_ramp(f->palette, 1.0f);

    dust_stars(f, c->salt, 0.0f, 0.55f);

    int ground = cell_row(g, deck - tilt - 0.12f);
    for (int row = ground; row < g->rows; row++)
        for (int col = 0; col < g->cols; col++) {
            Cell *cell = grid_at(g, col, row);
            cell->glyph = hash01((uint32_t)(row * 977 + col) ^ c->salt) < 0.12f ? '.' : ' ';
            cell->fg = rgb_from_f(frame_dim(f, brass, 0.25f));
        }

    // Strings of bulbs from the flag out to the edges of the night, every other one lit in turn.
    for (int side = -1; side <= 1; side += 2) {
        StrokePoint wire[21];
        for (int k = 0; k < 21; k++) {
            float u = (float)k / 20;
            wire[k] = (StrokePoint){(float)side * u * (g->aspect + 0.05f),
                                    mixf(peak + 0.1f, 0.62f, u) - c->sag * 4 * u * (1 - u), frame_dim(f, brass, 0.3f)};
        }
        stroke_draw(f->stroker, wire, 21, 0);
        for (int k = 3; k < 21; k += 2) {
            int lit = (k / 2 + beat_no) % 2 == 0;
            Rgbf color = rgbf_mix(palette_accent(f->palette, k / 2), bulb, 0.3f);
            put_cell(g, cell_col(g, wire[k].x), cell_row(g, wire[k].y) + 1, 'o',
                     frame_dim(f, color, lit ? chase : 0.4f));
        }
    }

    // The deck, seen from a little above, the seams of its boards turning with it.
    for (int row = cell_row(g, deck + tilt); row <= cell_row(g, deck - tilt); row++)
        for (int col = 0; col < g->cols; col++) {
            float u = cell_x(g, col) / rim, v = (cell_y(g, row) - deck) / tilt;
            if (u * u + v * v > 1) continue;
            if (mfract((matan2(v, u) - turn) * 16 / TAU_F) < 0.12f)
                put_cell(g, col, row, '.', frame_dim(f, brass, 0.4f));
        }

    // The horses, back to front: the far side's dimmer and going the other way, the column between the two sides.
    int order[HORSES];
    float depth[HORSES];
    for (int k = 0; k < HORSES; k++) {
        order[k] = k;
        depth[k] = msin(turn + (float)k * TAU_F / HORSES);
    }
    for (int i = 1; i < HORSES; i++)
        for (int j = i; j > 0 && depth[order[j]] > depth[order[j - 1]]; j--) {
            int swap = order[j];
            order[j] = order[j - 1];
            order[j - 1] = swap;
        }

    int column_drawn = 0;
    for (int i = 0; i < HORSES; i++) {
        int k = order[i];
        float a = turn + (float)k * TAU_F / HORSES, z = depth[k], lift = orbit * squash * z,
              near = 1 - 0.45f * (z + 1) / 2;
        if (!column_drawn && z <= 0) {
            carousel_column(f, turn, top, deck, brass, cream);
            column_drawn = 1;
        }
        // Neighbors rise and fall in turn, once a bar, the way a waltz sways.
        float bob = 0.065f * gentle * msin(TAU_F * f->t / bar + (float)k * PI_F);
        int col = cell_col(g, orbit * mcos(a)), ears = cell_row(g, -0.08f + lift + bob);
        for (int row = cell_row(g, top + lift); row <= cell_row(g, deck + lift); row++) {
            // A glint climbs the twisted brass.
            int glint = (row + (int)(f->t * 6)) % 3 == 0;
            put_cell(g, col, row, '|', frame_dim(f, brass, near * (glint ? 1.0f : 0.65f)));
        }
        Rgbf body = frame_dim(f, rgbf_mix(palette_accent(f->palette, k), cream, 0.35f), near);
        horse(g, col, ears, z > 0, body, frame_dim(f, brass, near));
    }
    if (!column_drawn) carousel_column(f, turn, top, deck, brass, cream);

    // The skirt round the deck and the canopy above, both turning: painted panels below, a striped roof and a
    // scalloped valance above.
    int last_panel = 0, inside = 0;
    for (int col = 0; col < g->cols; col++) {
        float x = cell_x(g, col), u = x / rim;
        if (u <= -1 || u >= 1) {
            inside = 0;
            continue;
        }
        float front = -msqrt(1 - u * u), around = matan2(front, u) - turn;
        int deck_edge = cell_row(g, deck + tilt * front), panel = (int)mfloor(around * 12 / TAU_F);
        int border = inside && panel != last_panel;
        last_panel = panel, inside = 1;
        for (int r = 1; r <= 3; r++)
            put_cell(g, col, deck_edge + r, border ? '|' : '=',
                     frame_dim(f, panel & 1 ? cloth : brass, border ? 0.8f : 0.6f));
        // Roof: follow the line from the peak through each cell down to the rim to find its stripe.
        int rim_row = cell_row(g, top + tilt * front);
        for (int row = cell_row(g, peak - mabs(u) * rise); row < rim_row; row++) {
            float down = peak - cell_y(g, row), along = x * rise / (down * rim);
            for (int it = 0; it < 2; it++)
                along = clampf(x * (rise + tilt * msqrt(1 - clampf(along * along, 0, 1))) / (down * rim), -1, 1);
            float a = matan2(-msqrt(1 - along * along), along) - turn, facing = msqrt(1 - along * along);
            int stripe = (int)mfloor(a * 12 / TAU_F) & 1;
            put_cell(g, col, row, stripe ? '#' : ':', frame_dim(f, stripe ? cloth : cream, 0.25f + 0.35f * facing));
        }
        put_cell(g, col, rim_row, '=', frame_dim(f, brass, 0.85f));
        float k = around * 18 / TAU_F, dip = msin(PI_F * mfract(k));
        for (int r = 1; r <= 1 + (int)(dip * 2.2f); r++)
            put_cell(g, col, rim_row + r, r == 1 ? '#' : 'V', frame_dim(f, (int)mfloor(k) & 1 ? cloth : cream, 0.75f));
    }

    // Bulbs hang from the low point of every scallop, and more ring the skirt.
    for (int s = 0; s < 18; s++) {
        float a = ((float)s + 0.5f) * TAU_F / 18 + turn;
        if (msin(a) >= 0) continue;
        int lit = (s + beat_no) % 3 == 0;
        Rgbf color = rgbf_mix(palette_accent(f->palette, s), bulb, 0.4f);
        put_cell(g, cell_col(g, rim * mcos(a)), cell_row(g, top + tilt * msin(a)) + 4, 'o',
                 frame_dim(f, color, lit ? chase : 0.4f));
    }

    for (int s = 0; s < 12; s++) {
        float a = ((float)s + 0.5f) * TAU_F / 12 + turn;
        if (msin(a) >= 0) continue;
        put_cell(g, cell_col(g, rim * mcos(a)), cell_row(g, deck + tilt * msin(a)) + 2, 'o',
                 frame_dim(f, bulb, 0.5f + 0.5f * chase));
    }

    // The roof's edges and its flag.
    line(f, 0, peak, -rim, top, brass);
    line(f, 0, peak, rim, top, brass);
    int flag_col = cell_col(g, 0), flag_row = cell_row(g, peak);
    for (int r = 1; r <= 3; r++) put_cell(g, flag_col, flag_row - r, '|', brass);
    put_cell(g, flag_col + 1, flag_row - 3, (int)(f->t * 3) % 2 ? '~' : '=', cloth);
    put_cell(g, flag_col + 2, flag_row - 3, '>', cloth);

    f->fx->glow = 0.55f;
}
