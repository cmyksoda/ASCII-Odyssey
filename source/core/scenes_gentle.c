// SPDX-License-Identifier: GPL-3.0-only
// Gentle scenes for the soft moods: soap bubbles rising with the melody, a snow globe shaken on the bar line, a
// dandelion clock whose seeds let go one note at a time, a music box's dancer, strings of fairy lights, and a mandala
// the tune draws for itself.
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

// A little ASCII picture, its middle at (col, row).
static void sprite(Grid *g, int col, int row, const char *const *rows, int count, Rgbf color) {
    for (int r = 0; r < count; r++) {
        int w = (int)strlen(rows[r]);
        for (int c = 0; c < w; c++)
            if (rows[r][c] != ' ') put_cell(g, col + c - w / 2, row + r - count / 2, rows[r][c], color);
    }
}

// The roles that carry a tune, for scenes that answer the melody.
static const Role TUNE[4] = {ROLE_BELL, ROLE_PLUCK, ROLE_LEAD, ROLE_ARP};

/* ---------- Bubbles ---------- */

#define BUBBLE_NOTES 8
#define BUBBLE_LIFE 9.0f

typedef struct {
    float drift, every;
    uint32_t salt;
} Bubbles;

void bubbles_setup(void *state, Rng *r, const Frame *f) {
    Bubbles *b = state;
    b->drift = rng_between(r, -0.05f, 0.05f);
    b->every = rng_between(r, 0.8f, 1.3f);
    b->salt = rng_u32(r);
    (void)f;
}

// One bubble born at `born`: a thin round skin whose colors slide round it, a window of light on its upper left, and,
// at the end of its rise, a ring of droplets as it pops.
static void bubble(const Frame *f, const Bubbles *b, float born, int note, float flare) {
    Grid *g = f->grid;
    uint32_t id = (uint32_t)(born * 1000.0f) ^ b->salt;
    float age = (f->t - born) * frame_pace(f), life = BUBBLE_LIFE * (0.6f + 0.4f * hash01(id + 1));
    if (age < 0 || age > life + 0.4f) return;
    float pitch = saturate(((float)note - 48) / 36), radius = 0.05f + 0.11f * (1 - pitch) + 0.03f * hash01(id + 2);
    float x =
        (hash01(id) * 1.7f - 0.85f) * g->aspect + 0.07f * msin(age * 0.9f + hash01(id + 3) * TAU_F) + b->drift * age;
    float y = -1.1f - radius + age * (0.2f + 0.08f * hash01(id + 4));
    Rgbf white = palette_ramp(f->palette, 1.0f);
    if (age > life) {
        float spread = radius * (1 + (age - life) * 4), fade = 1 - (age - life) / 0.4f;
        for (int k = 0; k < 10; k++) {
            float a = (float)k / 10 * TAU_F;
            put_cell(g, cell_col(g, x + mcos(a) * spread), cell_row(g, y + msin(a) * spread), '.',
                     frame_dim(f, white, 0.8f * fade));
        }
        return;
    }
    StrokePoint skin[20];
    float swirl = f->t * 0.6f + hash01(id + 5) * TAU_F;
    for (int k = 0; k < 20; k++) {
        float a = (float)k / 20 * TAU_F, hue = 0.5f + 0.5f * msin(a * 2 + swirl);
        Rgbf c = rgbf_mix(palette_accent(f->palette, k % 3), palette_accent(f->palette, k % 3 + 1), hue);
        skin[k] = (StrokePoint){x + mcos(a) * radius, y + msin(a) * radius,
                                frame_dim(f, rgbf_mix(c, white, 0.25f + 0.5f * flare), 0.75f + 0.4f * flare)};
    }
    stroke_draw(f->stroker, skin, 20, 1);
    put_cell(g, cell_col(g, x - radius * 0.45f), cell_row(g, y + radius * 0.45f), radius > 0.09f ? '*' : '\'', white);
}

// Soap bubbles rising through soft light: every note of the tune blows one (high notes blow small ones), a slow stream
// of its own keeps them coming between notes, and each pops into droplets at the end of its rise.
void bubbles_draw(void *state, const Frame *f) {
    Bubbles *b = state;
    dust_stars(f, b->salt, 0.002f, 0.4f);
    for (int k = 0; k < 4 && f->react > 0; k++) {
        const ScoreEvent *notes[BUBBLE_NOTES];
        int n = recent_notes(f->score, TUNE[k], f->t, BUBBLE_LIFE + 0.5f, notes, BUBBLE_NOTES);
        for (int i = 0; i < n; i++)
            bubble(f, b, notes[i]->time, notes[i]->note, mexp(-(f->t - notes[i]->time) / 0.3f) * frame_bright(f));
    }
    for (float born = mfloor(f->t / b->every) * b->every; born > f->t - BUBBLE_LIFE - 0.5f; born -= b->every)
        bubble(f, b, born, 60 + (int)(hash01((uint32_t)(born * 7.0f)) * 24), 0);
    f->fx->glow = 0.5f;
}

/* ---------- Snow globe ---------- */

#define FLAKES 70

typedef struct {
    int bars;
    float settle;
    uint32_t salt;
} Globe;

void globe_setup(void *state, Rng *r, const Frame *f) {
    Globe *s = state;
    s->bars = rng_chance(r, 50) ? 2 : 4;
    s->settle = rng_between(r, 0.05f, 0.09f);
    s->salt = rng_u32(r);
    (void)f;
}

// A snow globe on a wooden base: a cottage with a lit window and a pine tree under the glass, and the snow tossed up
// in a swirl each time the globe is shaken on the bar line, settling slowly back onto the drift.
void globe_draw(void *state, const Frame *f) {
    Globe *s = state;
    Grid *g = f->grid;
    static const char *const cottage[4] = {"  /\\| ", " /  \\ ", "|[] #|", "|__|_|"};
    static const char *const pine[4] = {" ^ ", "/^\\", "/^\\", " | "};
    float bar = f->score->beat * (float)f->score->info->beats_per_bar, every = bar * (float)s->bars;
    float since = f->t - mfloor(f->t / every) * every,
          wobble = f->still ? 0 : 0.03f * msin(since * 24) * mexp(-since * 5);
    float cx = wobble, cy = 0.12f, radius = 0.62f, bank = cy - radius * 0.55f;
    Rgbf glass = palette_ramp(f->palette, 0.7f), wood = palette_accent(f->palette, 1),
         snow = palette_ramp(f->palette, 1.0f);
    Rgbf glow = palette_accent(f->palette, 0);
    dust_stars(f, s->salt, 0.0f, 0.4f);
    // The base, and the glass with a curve of light along its upper left.
    for (int k = 0; k < 3; k++) {
        float y = cy - radius - 0.05f - (float)k * 0.07f, w = radius * (0.75f + 0.12f * (float)k);
        line(f, cx - w, y, cx + w, y, frame_dim(f, wood, 1.0f - 0.15f * (float)k));
    }
    line(f, cx - radius * 1.0f, cy - radius - 0.26f, cx - radius * 0.75f, cy - radius - 0.05f, wood);
    line(f, cx + radius * 1.0f, cy - radius - 0.26f, cx + radius * 0.75f, cy - radius - 0.05f, wood);
    StrokePoint rim[40];
    for (int k = 0; k < 40; k++) {
        float a = (float)k / 40 * TAU_F, sheen = mexp(-(a - 2.2f) * (a - 2.2f) * 3);
        rim[k] = (StrokePoint){cx + mcos(a) * radius, cy + msin(a) * radius,
                               rgbf_mix(frame_dim(f, glass, 0.6f), snow, sheen)};
    }
    stroke_draw(f->stroker, rim, 40, 1);
    // The drift inside, deep snow under it filling the bottom of the glass, the cottage with its lit window, and the
    // pine.
    for (int col = cell_col(g, cx - radius * 0.9f); col <= cell_col(g, cx + radius * 0.9f); col++) {
        float x = cell_x(g, col), top = bank + 0.02f * msin(x * 9),
              depth = msqrt(mmax(0.0f, radius * radius - (x - cx) * (x - cx)));
        if (mabs(x - cx) < radius * 0.82f) put_cell(g, col, cell_row(g, top), '~', frame_dim(f, snow, 0.8f));
        for (int row = cell_row(g, top) + 1; row < cell_row(g, cy - depth); row++)
            put_cell(g, col, row, (row + col) % 3 ? ':' : '.', frame_dim(f, snow, 0.35f));
    }
    int house_col = cell_col(g, cx - 0.14f), house_row = cell_row(g, bank + 0.12f);
    sprite(g, house_col, house_row, cottage, 4, frame_dim(f, wood, 1.1f));
    // The sprite sits centered on (house_col, house_row): its lit window is row 2, column 4, and the chimney row 0,
    // column 4.
    put_cell(g, house_col + 1, house_row, '#',
             frame_dim(f, glow, 0.85f + 0.15f * msin(f->t * 7.3f) * msin(f->t * 3.1f)));
    // Smoke from the chimney, curling up and fading, whatever the snow is doing.
    for (int k = 0; k < 6; k++) {
        float age = mfract(f->t * 0.35f * frame_pace(f) + (float)k / 6), up = age * 0.3f;
        int col =
            house_col + 1 + (int)mfloor(0.04f * msin(age * 7 + f->t * 0.5f) / (2 * g->aspect / (float)g->cols) + 0.5f);
        put_cell(g, col, house_row - 3 - (int)(up / (2.0f / (float)g->rows)), age < 0.5f ? '~' : '.',
                 frame_dim(f, palette_ramp(f->palette, 0.6f), 0.7f * (1 - age)));
    }
    sprite(g, cell_col(g, cx + 0.24f), cell_row(g, bank + 0.11f), pine, 4,
           frame_dim(f, palette_accent(f->palette, 2), 0.9f));
    // The snow: tossed up off the drift by each shake, then circling the middle as the swirl dies away and sinking
    // until it rests again. The swirl carries on from shake to shake, so no flake ever jumps.
    float spin = 2.4f * (mfloor(f->t / every) + 1 - mexp(-since / 1.4f)) * frame_pace(f),
          toss = smoothstep(0.0f, 0.5f, since);
    for (int k = 0; k < FLAKES; k++) {
        uint32_t id = s->salt + (uint32_t)k * 2654435761u;
        float a = hash01(id) * TAU_F + spin * (0.6f + 0.8f * hash01(id + 1)),
              ring = radius * (0.2f + 0.65f * hash01(id + 2));
        // One flake in five is a straggler, still drifting down long after the rest have settled.
        float sink = hash01(id + 7) < 0.2f ? 0.15f : 0.6f + 0.8f * hash01(id + 3);
        float x = cx + mcos(a) * ring, swirl = cy + msin(a) * ring * 0.8f - since * s->settle * sink;
        float floor_y = bank + 0.01f + 0.05f * hash01(id + 4), y = mixf(floor_y, mmax(swirl, floor_y), toss);
        if (mhypot(x - cx, y - cy) > radius - 0.05f) continue;
        put_cell(g, cell_col(g, x), cell_row(g, y), hash01(id + 5) < 0.3f ? '*' : '.',
                 frame_dim(f, snow, 0.6f + 0.4f * hash01(id + 6)));
    }
    f->fx->glow = 0.5f;
}

/* ---------- Dandelion ---------- */

#define SEEDS 40
// After the last seed goes, the stem stands bare for a few releases before a new clock opens.
#define SEED_CYCLE (SEEDS + 6)
#define SEED_FLIGHT 9.0f
#define FLYING 48

typedef struct {
    float hx, hy, tick, wind;
    uint32_t salt;
} Dandelion;

void dandelion_setup(void *state, Rng *r, const Frame *f) {
    Dandelion *d = state;
    d->hx = rng_between(r, -0.55f, -0.25f) * f->grid->aspect;
    d->hy = rng_between(r, 0.0f, 0.2f);
    d->tick = rng_between(r, 1.2f, 1.8f);
    d->wind = rng_between(r, 0.18f, 0.28f);
    d->salt = rng_u32(r);
}

// Where seed i's tuft sits on the clock: spread evenly over a ball, with how far toward the viewer it faces.
static void seed_tip(const Dandelion *d, int i, float *x, float *y, float *front) {
    float up = 1 - 2 * ((float)i + 0.5f) / SEEDS, around = (float)i * 2.3999632f, ring = msqrt(1 - up * up);
    *x = d->hx + mcos(around) * ring * 0.27f, *y = d->hy + up * 0.27f, *front = msin(around) * ring;
}

// Seeds let go since the shot began, before time t: one for every note of the tune, and one on a slow clock of its own.
static int released(const Dandelion *d, const Frame *f, float t) {
    float start = f->t - f->local;
    int n = (int)mfloor((t - start) / d->tick);
    for (int k = 0; k < 4 && f->react > 0; k++) n += score_count(f->score, TUNE[k], start, t);
    return n < 0 ? 0 : n;
}

// A dandelion clock in a meadow at dusk: its seeds let go one at a time, with the tune and on a slow clock of their
// own, and drift away on the breeze, until the head is bare and a new one opens.
void dandelion_draw(void *state, const Frame *f) {
    Dandelion *d = state;
    Grid *g = f->grid;
    Rgbf grass = palette_ramp(f->palette, 0.35f), stem = palette_accent(f->palette, 2),
         down = palette_ramp(f->palette, 1.0f);
    float pace = frame_pace(f);
    dust_stars(f, d->salt, 0.003f, 0.6f);
    moon_disc(f, g->aspect * 0.55f, 0.62f, 0.13f, palette_ramp(f->palette, 0.95f), d->salt, 1);
    // The meadow: blades of uneven heights, swaying.
    for (int col = 0; col < g->cols; col += 2) {
        float x = cell_x(g, col), h = 0.08f + 0.12f * hash01((uint32_t)col * 7919u ^ d->salt),
              sway = 0.03f * msin(f->t * 0.8f + x * 3);
        line(f, x, -1.0f, x + sway, -1.0f + h, frame_dim(f, grass, 0.6f + 0.4f * h / 0.2f));
    }
    StrokePoint stalk[8];
    for (int k = 0; k < 8; k++) {
        float u = (float)k / 7;
        stalk[k] = (StrokePoint){d->hx + 0.05f * msin(u * PI_F) + 0.01f * msin(f->t * 0.7f) * u,
                                 mixf(-1.0f, d->hy - 0.02f, u), stem};
    }
    stroke_draw(f->stroker, stalk, 8, 0);
    int gone = released(d, f, f->t) % SEED_CYCLE;
    for (int i = gone; i < SEEDS; i++) {
        float x, y, front;
        seed_tip(d, i, &x, &y, &front);
        Rgbf c = frame_dim(f, down, 0.45f + 0.45f * (front + 1) / 2);
        line(f, d->hx + (x - d->hx) * 0.3f, d->hy + (y - d->hy) * 0.3f, x, y, frame_dim(f, c, 0.6f));
        put_cell(g, cell_col(g, x), cell_row(g, y), '*', c);
    }
    put_cell(g, cell_col(g, d->hx), cell_row(g, d->hy), '@', frame_dim(f, stem, 1.1f));
    // Seeds in flight: the newest few releases, each from the very place on the head it left.
    float when[FLYING];
    int count = 0;
    float start = f->t - f->local;
    for (float t = start + d->tick; t <= f->t && count < FLYING; t += d->tick)
        if (t > f->t - SEED_FLIGHT) when[count++] = t;
    for (int k = 0; k < 4 && f->react > 0; k++) {
        const ScoreEvent *notes[12];
        int n = recent_notes(f->score, TUNE[k], f->t, SEED_FLIGHT, notes, 12);
        for (int i = 0; i < n && count < FLYING; i++)
            if (notes[i]->time >= start) when[count++] = notes[i]->time;
    }
    for (int j = 0; j < count; j++) {
        // Its place on the head is the count of seeds that left before it.
        int index = released(d, f, when[j] - 0.001f) % SEED_CYCLE;
        if (index >= SEEDS) continue;
        float x, y, front, age = (f->t - when[j]) * pace;
        seed_tip(d, index, &x, &y, &front);
        uint32_t id = (uint32_t)(when[j] * 997.0f) ^ d->salt;
        x += age * d->wind + 0.05f * msin(age * 1.3f + hash01(id) * TAU_F);
        y += age * (0.05f + 0.04f * hash01(id + 1)) + 0.03f * msin(age * 2.1f);
        put_cell(g, cell_col(g, x), cell_row(g, y), '*', frame_dim(f, down, 0.9f - age / SEED_FLIGHT * 0.5f));
        put_cell(g, cell_col(g, x), cell_row(g, y) + 1, '|', frame_dim(f, down, 0.5f - age / SEED_FLIGHT * 0.3f));
    }
    f->fx->glow = 0.5f;
}

/* ---------- Ballerina ---------- */

typedef struct {
    float tilt, spin;
    uint32_t salt;
} Ballerina;

void ballerina_setup(void *state, Rng *r, const Frame *f) {
    Ballerina *b = state;
    b->tilt = rng_between(r, 0.25f, 0.35f);
    b->spin = rng_chance(r, 50) ? 1.0f : -1.0f;
    b->salt = rng_u32(r);
    (void)f;
}

// A point of the dancer, given in her own frame (x right, y up, z toward us), turned about her standing leg.
static void dancer_point(float c, float s, float x, float y, float z, float base_x, float base_y, float *ox, float *oy,
                         float *depth) {
    *ox = base_x + x * c + z * s;
    *oy = base_y + y;
    *depth = z * c - x * s;
}

// A limb with some thickness, as a row of overlapping discs on the canvas.
static void limb(Canvas *cv, float x0, float y0, float x1, float y1, float r, Rgbf color) {
    int steps = 1 + (int)(mhypot(x1 - x0, y1 - y0) / (r * 0.9f));
    for (int k = 0; k <= steps; k++) {
        float u = (float)k / (float)steps;
        canvas_disc(cv, mixf(x0, x1, u), mixf(y0, y1, u), r, 0.95f, color);
    }
}

// The dancer of an opened music box, turning a pirouette every bar on a little mirror: arms rounded above her head, a
// tutu standing out at her waist, one foot drawn up to her knee. Glints wink in the mirror at her feet.
void ballerina_draw(void *state, const Frame *f) {
    Ballerina *b = state;
    Grid *g = f->grid;
    float bar = f->score->beat * (float)f->score->info->beats_per_bar,
          turn = b->spin * TAU_F * f->t / bar * frame_pace(f);
    float c = mcos(turn), s = msin(turn), bx = 0, by = -0.6f, k = 1.9f;
    Rgbf wood = palette_accent(f->palette, 1), velvet = palette_accent(f->palette, 0),
         body = palette_ramp(f->palette, 1.0f);
    Rgbf silver = palette_ramp(f->palette, 0.75f), skirt = palette_accent(f->palette, 2);
    // Joints in her own frame (x right, y up, z toward us), turned about her standing leg as she spins.
    static const float J[][3] = {
        {0, 0, 0},       {0, 0.22f, 0},     {0.12f, 0.17f, 0.05f}, {0.01f, 0.13f, 0.01f}, {0, 0.26f, 0},
        {0, 0.4f, 0},    {-0.07f, 0.38f, 0}, {0.07f, 0.38f, 0},    {-0.12f, 0.47f, 0.03f}, {0.12f, 0.47f, 0.03f},
        {-0.02f, 0.55f, 0.04f}, {0.02f, 0.55f, 0.04f}};
    static const int LIMBS[][2] = {{0, 1}, {1, 2}, {2, 3},  {1, 4}, {4, 5}, {5, 6},
                                   {5, 7}, {6, 8}, {8, 10}, {7, 9}, {9, 11}};
    float px[12], py[12], depth[12];
    for (int i = 0; i < 12; i++)
        dancer_point(c, s, J[i][0] * k, J[i][1] * k, J[i][2] * k, bx, by, &px[i], &py[i], &depth[i]);
    Canvas *cv = f->canvas;
    canvas_clear(cv);
    // The tutu first, a disc of tulle seen from a little above, so her body stands in front of it.
    for (int ring = 0; ring < 3; ring++)
        for (int i = 0; i < 14; i++) {
            float a = (float)i / 14 * TAU_F + turn, reach = (0.04f + 0.055f * (float)ring) * k;
            canvas_disc(cv, bx + mcos(a) * reach, by + 0.27f * k + msin(a) * reach * b->tilt, 0.036f * k,
                        0.8f - 0.1f * (float)ring, skirt);
        }
    for (int i = 0; i < (int)(sizeof LIMBS / sizeof LIMBS[0]); i++) {
        int p = LIMBS[i][0], q = LIMBS[i][1], torso = (p == 4 || q == 5);
        float shade = 0.75f + 0.25f * saturate(0.5f + (depth[p] + depth[q]) * 2);
        limb(cv, px[p], py[p], px[q], py[q], (torso ? 0.028f : 0.014f) * k, frame_dim(f, body, shade));
    }
    canvas_disc(cv, bx + 0, by + 0.49f * k, 0.033f * k, 0.95f, body);
    frame_resolve(f);
    dust_stars(f, b->salt, 0.0f, 0.3f);
    // The box's front with its keyhole, and the mirror she turns on.
    for (int row = cell_row(g, -0.7f); row <= cell_row(g, -0.98f); row++)
        for (int col = cell_col(g, -0.95f); col <= cell_col(g, 0.95f); col++)
            put_cell(g, col, row,
                     row == cell_row(g, -0.7f) ? '='
                     : (col + row) % 4         ? ':'
                                               : '.',
                     frame_dim(f, row == cell_row(g, -0.7f) ? wood : velvet, 0.55f));
    put_cell(g, cell_col(g, 0), cell_row(g, -0.86f), 'o', wood);
    StrokePoint mirror[24];
    for (int i = 0; i < 24; i++) {
        float a = (float)i / 24 * TAU_F;
        mirror[i] = (StrokePoint){bx + mcos(a) * 0.34f, by + msin(a) * 0.34f * b->tilt, silver};
    }
    stroke_draw(f->stroker, mirror, 24, 1);
    for (int i = 0; i < 3; i++) {
        float a = turn * 0.5f + (float)i * 2.1f, wink = 0.5f + 0.5f * msin(f->t * 2.3f + (float)i * 1.7f);
        if (wink > 0.6f)
            put_cell(g, cell_col(g, bx + mcos(a) * 0.24f), cell_row(g, by + msin(a) * 0.07f), '*',
                     frame_dim(f, body, wink));
    }
    f->fx->glow = 0.5f;
}

/* ---------- Fairy lights ---------- */

#define STRINGS 4
#define BULBS 22

typedef struct {
    float sag[STRINGS], height[STRINGS];
    uint32_t salt;
} FairyLights;

void lights_setup(void *state, Rng *r, const Frame *f) {
    FairyLights *l = state;
    for (int k = 0; k < STRINGS; k++) {
        l->height[k] = 0.75f - 0.42f * (float)k + rng_between(r, -0.06f, 0.06f);
        l->sag[k] = rng_between(r, 0.12f, 0.22f);
    }
    l->salt = rng_u32(r);
    (void)f;
}

// Strings of fairy lights sagging across a dark room, more strung farther back. The bulbs twinkle on their own, and
// every note of the tune sends a glow running along one of the strings.
void lights_draw(void *state, const Frame *f) {
    FairyLights *l = state;
    Grid *g = f->grid;
    dust_stars(f, l->salt, 0.002f, 0.35f);
    // A dimmer, farther layer of strings behind the near ones, for depth.
    for (int s = 0; s < 3; s++)
        for (int i = 0; i < 30; i++) {
            float u = ((float)i + 0.5f) / 30, x = (u * 2 - 1) * (g->aspect + 0.05f),
                  y = 0.55f - 0.5f * (float)s - 0.1f * 4 * u * (1 - u) + 0.15f;
            float tw = 0.5f + 0.5f * msin(f->t * 0.9f + (float)(i * 7 + s * 3));
            put_cell_under(g, cell_col(g, x), cell_row(g, y), '.',
                           frame_dim(f, palette_accent(f->palette, i + s), 0.25f + 0.2f * tw));
        }
    const ScoreEvent *notes[16];
    int n = 0;
    for (int k = 0; k < 4 && f->react > 0 && n < 12; k++)
        n += recent_notes(f->score, TUNE[k], f->t, 1.6f, notes + n, 16 - n < 4 ? 16 - n : 4);
    Rgbf wire = palette_ramp(f->palette, 0.3f);
    for (int s = 0; s < STRINGS; s++) {
        StrokePoint pts[24];
        for (int i = 0; i < 24; i++) {
            float u = (float)i / 23, x = (u * 2 - 1) * (g->aspect + 0.05f);
            pts[i] = (StrokePoint){x, l->height[s] - l->sag[s] * 4 * u * (1 - u), frame_dim(f, wire, 0.7f)};
        }
        stroke_draw(f->stroker, pts, 24, 0);
        for (int i = 0; i < BULBS; i++) {
            float u = ((float)i + 0.5f) / BULBS, x = (u * 2 - 1) * (g->aspect + 0.05f),
                  y = l->height[s] - l->sag[s] * 4 * u * (1 - u);
            uint32_t id = l->salt ^ (uint32_t)(s * 131 + i) * 2654435761u;
            float level =
                0.45f + 0.35f * (0.5f + 0.5f * msin(f->t * (0.7f + hash01(id) * 1.6f) + hash01(id + 1) * TAU_F));
            for (int k = 0; k < n; k++) {
                // A note's glow sets off from a place its pitch picks on the string its time picks, and runs both ways.
                uint32_t nid = (uint32_t)(notes[k]->time * 1000.0f);
                if ((int)(nid % STRINGS) != s) continue;
                float from = (float)(notes[k]->note % 24) / 24, run = (f->t - notes[k]->time) * 0.9f,
                      d = mabs(u - from) - run;
                level += mexp(-d * d * 400) * frame_bright(f) * 0.8f;
            }
            Rgbf color = rgbf_mix(palette_accent(f->palette, i % 3), palette_ramp(f->palette, 1.0f), 0.2f);
            put_cell(g, cell_col(g, x), cell_row(g, y) + 1, level > 0.9f ? '@' : 'o', frame_dim(f, color, level));
        }
    }
    f->fx->glow = 0.6f;
}

/* ---------- Mandala ---------- */

#define PETAL_NOTES 6
#define PETAL_LIFE 2.4f

typedef struct {
    int folds;
    float spin, every;
    uint32_t salt;
} Mandala;

void mandala_setup(void *state, Rng *r, const Frame *f) {
    Mandala *m = state;
    // Few enough mirrors that a busy tune's petals still read as a figure rather than a solid disc.
    static const int folds[4] = {5, 6, 7, 8};
    m->folds = folds[rng_range(r, 0, 4)];
    m->spin = rng_between(r, 0.01f, 0.025f) * (rng_chance(r, 50) ? 1 : -1);
    m->every = rng_between(r, 0.35f, 0.6f);
    m->salt = rng_u32(r);
    (void)f;
}

#define PETALS 9

typedef struct {
    float at, angle, size, level;
    Rgbf color;
} Petal;

// A petal for the mandala at `age` seconds old: set out by its pitch `high` (0 low, 1 high) at its own angle in the
// wedge, opening quickly and then drifting outward as it fades.
static Petal petal(const Frame *f, float age, float high, float angle, float level, float attack, Rgbf color) {
    float grow = smoothstep(0.0f, attack, age), fade = saturate(1 - (age / PETAL_LIFE) * (age / PETAL_LIFE));
    return (Petal){0.2f + 0.6f * high + age * 0.05f * frame_pace(f), angle, (0.035f + 0.035f * (1 - high)) * grow,
                   level * fade, color};
}

// A mandala the tune draws for itself: every note of the melody opens a petal, set farther out the higher it is and
// repeated in each of the mirrors' wedges, then drifts outward and fades, so a phrase blooms into a figure and melts
// away. Kicks swell its heart and chords turn its colors; between tunes, petals open on a slow clock of their own.
void mandala_draw(void *state, const Frame *f) {
    Mandala *m = state;
    Grid *g = f->grid;
    static const char ramp[] = " .:-=+*#@";
    float turn = f->local * m->spin * TAU_F * frame_pace(f), wedge = TAU_F / (float)m->folds,
          contrast = frame_contrast(f);
    float kick = frame_pulse(f, ROLE_KICK, 0.25f, 1.0f), attack = f->calm || f->still ? 0.3f : 0.08f;
    const ChordMark *chord = score_chord(f->score, f->t);
    int shift = chord ? chord->chord.root_pc : 0;

    const ScoreEvent *notes[4 * PETAL_NOTES];
    int n = 0, count = 0;
    for (int k = 0; k < 4 && f->react > 0; k++)
        n += recent_notes(f->score, TUNE[k], f->t, PETAL_LIFE, notes + n, PETAL_NOTES);

    // The newest notes win when a busy tune has more than the mandala can hold.
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && notes[j]->time > notes[j - 1]->time; j--) {
            const ScoreEvent *swap = notes[j];
            notes[j] = notes[j - 1], notes[j - 1] = swap;
        }

    Petal petals[PETALS];
    for (int i = 0; i < n && count < PETALS; i++) {
        float high = saturate(((float)notes[i]->note - 48) / 40.0f);
        float angle = wedge * 0.5f * hash01((uint32_t)notes[i]->note * 2654435761u ^ m->salt);
        float level = (0.6f + 0.4f * frame_bright(f)) * (0.55f + 0.45f * notes[i]->velocity);
        petals[count++] = petal(f, f->t - notes[i]->time, high, angle, level, attack,
                                palette_accent(f->palette, notes[i]->note + shift));
    }

    // Between tunes, or in a shot that doesn't follow the music, petals keep opening on a clock of their own.
    if (n < 3) {
        int last = mfloor_i(f->t / m->every);
        for (int k = 0; k <= (int)(PETAL_LIFE / m->every) && count < PETALS; k++) {
            uint32_t id = m->salt + (uint32_t)(last - k) * 2654435761u;
            petals[count++] = petal(f, f->t - (float)(last - k) * m->every, hash01(id), wedge * 0.5f * hash01(id + 1),
                                    0.6f, attack, palette_accent(f->palette, (int)(hash01(id + 2) * 3) + shift));
        }
    }

    Rgbf bone = palette_ramp(f->palette, 0.5f), heart = palette_ramp(f->palette, 1.0f);
    // No petal grows wider than a third of its half-wedge, or near the middle the mirrors would fuse petals into rings.
    float core = 0.05f + 0.05f * kick, widest = 0.35f * wedge * 0.5f;
    for (int row = 0; row < g->rows; row++) {
        float y = cell_y(g, row);
        for (int col = 0; col < g->cols; col++) {
            float x = cell_x(g, col), r = mhypot(x, y);
            if (r < core) {
                put_cell(g, col, row, '@', frame_dim(f, heart, (0.8f + 0.2f * kick) * contrast));
                continue;
            }

            // Folded into half a wedge by the mirrors, as a kaleidoscope folds its view.
            float a = matan2(y, x) - turn;
            a -= mfloor(a / wedge) * wedge;
            if (a > wedge * 0.5f) a = wedge - a;
            float best = 0;
            Rgbf ink = bone;
            for (int k = 0; k < count; k++) {
                const Petal *p = &petals[k];
                if (p->size <= 0.001f || mabs(r - p->at) > p->size * 1.8f) continue;
                float dr = (r - p->at) / (p->size * 1.8f), dt = (a - p->angle) / mmin(p->size / r, widest),
                      d = dr * dr + dt * dt;
                // Full inside and soft only at the rim, so each petal keeps a clear shape.
                float lit = d < 1 ? saturate((1 - d) * 2.5f) * p->level : 0;
                if (lit > best) best = lit, ink = p->color;
            }
            int i = (int)(saturate(best * 1.2f) * 8.99f);
            if (i > 0)
                put_cell(g, col, row, ramp[i], frame_dim(f, ink, (0.45f + 0.7f * best) * contrast));
            else if (r > 0.12f && r < 0.95f && r * mmin(a, wedge * 0.5f - a) < 0.012f)
                // The mandala's bones: faint dots along its mirrors, between the petals.
                put_cell(g, col, row, '.', frame_dim(f, bone, 0.45f * contrast));
        }
    }

    f->fx->glow = 0.6f + 0.2f * kick;
}
