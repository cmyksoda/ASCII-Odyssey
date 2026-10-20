// SPDX-License-Identifier: GPL-3.0-only
// Gentle scenes for the soft moods: soap bubbles rising with the melody, a snow globe shaken on the bar line, and a
// dandelion clock whose seeds let go one note at a time.
#include "mathx.h"
#include "noise.h"
#include "scene.h"

#include <string.h>

static float cell_x(const Grid *g, int col) {
    return ((float)col + 0.5f) / (float)g->cols * 2 * g->aspect - g->aspect;
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
    float x = (hash01(id) * 1.7f - 0.85f) * g->aspect + 0.07f * msin(age * 0.9f + hash01(id + 3) * TAU_F) + b->drift * age;
    float y = -1.1f - radius + age * (0.2f + 0.08f * hash01(id + 4));
    Rgbf white = palette_ramp(f->palette, 1.0f);
    if (age > life) {
        float spread = radius * (1 + (age - life) * 4), fade = 1 - (age - life) / 0.4f;
        for (int k = 0; k < 10; k++) {
            float a = (float)k / 10 * TAU_F;
            put_cell(g, cell_col(g, x + mcos(a) * spread), cell_row(g, y + msin(a) * spread), '.', frame_dim(f, white, 0.8f * fade));
        }
        return;
    }
    StrokePoint skin[20];
    float swirl = f->t * 0.6f + hash01(id + 5) * TAU_F;
    for (int k = 0; k < 20; k++) {
        float a = (float)k / 20 * TAU_F, hue = 0.5f + 0.5f * msin(a * 2 + swirl);
        Rgbf c = rgbf_mix(palette_accent(f->palette, k % 3), palette_accent(f->palette, k % 3 + 1), hue);
        skin[k] = (StrokePoint){x + mcos(a) * radius, y + msin(a) * radius, frame_dim(f, rgbf_mix(c, white, 0.25f + 0.5f * flare), 0.75f + 0.4f * flare)};
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
    float since = f->t - mfloor(f->t / every) * every, wobble = f->still ? 0 : 0.03f * msin(since * 24) * mexp(-since * 5);
    float cx = wobble, cy = 0.12f, radius = 0.62f, bank = cy - radius * 0.55f;
    Rgbf glass = palette_ramp(f->palette, 0.7f), wood = palette_accent(f->palette, 1), snow = palette_ramp(f->palette, 1.0f);
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
        rim[k] = (StrokePoint){cx + mcos(a) * radius, cy + msin(a) * radius, rgbf_mix(frame_dim(f, glass, 0.6f), snow, sheen)};
    }
    stroke_draw(f->stroker, rim, 40, 1);
    // The drift inside, deep snow under it filling the bottom of the glass, the cottage with its lit window, and the pine.
    for (int col = cell_col(g, cx - radius * 0.9f); col <= cell_col(g, cx + radius * 0.9f); col++) {
        float x = cell_x(g, col), top = bank + 0.02f * msin(x * 9), depth = msqrt(mmax(0.0f, radius * radius - (x - cx) * (x - cx)));
        if (mabs(x - cx) < radius * 0.82f) put_cell(g, col, cell_row(g, top), '~', frame_dim(f, snow, 0.8f));
        for (int row = cell_row(g, top) + 1; row < cell_row(g, cy - depth); row++)
            put_cell(g, col, row, (row + col) % 3 ? ':' : '.', frame_dim(f, snow, 0.35f));
    }
    int house_col = cell_col(g, cx - 0.14f), house_row = cell_row(g, bank + 0.12f);
    sprite(g, house_col, house_row, cottage, 4, frame_dim(f, wood, 1.1f));
    // The sprite sits centered on (house_col, house_row): its lit window is row 2, column 4, and the chimney row 0, column 4.
    put_cell(g, house_col + 1, house_row, '#', frame_dim(f, glow, 0.85f + 0.15f * msin(f->t * 7.3f) * msin(f->t * 3.1f)));
    // Smoke from the chimney, curling up and fading, whatever the snow is doing.
    for (int k = 0; k < 6; k++) {
        float age = mfract(f->t * 0.35f * frame_pace(f) + (float)k / 6), up = age * 0.3f;
        int col = house_col + 1 + (int)mfloor(0.04f * msin(age * 7 + f->t * 0.5f) / (2 * g->aspect / (float)g->cols) + 0.5f);
        put_cell(g, col, house_row - 3 - (int)(up / (2.0f / (float)g->rows)), age < 0.5f ? '~' : '.', frame_dim(f, palette_ramp(f->palette, 0.6f), 0.7f * (1 - age)));
    }
    sprite(g, cell_col(g, cx + 0.24f), cell_row(g, bank + 0.11f), pine, 4, frame_dim(f, palette_accent(f->palette, 2), 0.9f));
    // The snow: tossed up off the drift by each shake, then circling the middle as the swirl dies away and sinking
    // until it rests again. The swirl carries on from shake to shake, so no flake ever jumps.
    float spin = 2.4f * (mfloor(f->t / every) + 1 - mexp(-since / 1.4f)) * frame_pace(f), toss = smoothstep(0.0f, 0.5f, since);
    for (int k = 0; k < FLAKES; k++) {
        uint32_t id = s->salt + (uint32_t)k * 2654435761u;
        float a = hash01(id) * TAU_F + spin * (0.6f + 0.8f * hash01(id + 1)), ring = radius * (0.2f + 0.65f * hash01(id + 2));
        // One flake in five is a straggler, still drifting down long after the rest have settled.
        float sink = hash01(id + 7) < 0.2f ? 0.15f : 0.6f + 0.8f * hash01(id + 3);
        float x = cx + mcos(a) * ring, swirl = cy + msin(a) * ring * 0.8f - since * s->settle * sink;
        float floor_y = bank + 0.01f + 0.05f * hash01(id + 4), y = mixf(floor_y, mmax(swirl, floor_y), toss);
        if (mhypot(x - cx, y - cy) > radius - 0.05f) continue;
        put_cell(g, cell_col(g, x), cell_row(g, y), hash01(id + 5) < 0.3f ? '*' : '.', frame_dim(f, snow, 0.6f + 0.4f * hash01(id + 6)));
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
    Rgbf grass = palette_ramp(f->palette, 0.35f), stem = palette_accent(f->palette, 2), down = palette_ramp(f->palette, 1.0f);
    float pace = frame_pace(f);
    dust_stars(f, d->salt, 0.003f, 0.6f);
    moon_disc(f, g->aspect * 0.55f, 0.62f, 0.13f, palette_ramp(f->palette, 0.95f), d->salt, 1);
    // The meadow: blades swaying, taller toward the front.
    for (int col = 0; col < g->cols; col += 2) {
        float x = cell_x(g, col), h = 0.08f + 0.12f * hash01((uint32_t)col * 7919u ^ d->salt), sway = 0.03f * msin(f->t * 0.8f + x * 3);
        line(f, x, -1.0f, x + sway, -1.0f + h, frame_dim(f, grass, 0.6f + 0.4f * h / 0.2f));
    }
    StrokePoint stalk[8];
    for (int k = 0; k < 8; k++) {
        float u = (float)k / 7;
        stalk[k] = (StrokePoint){d->hx + 0.05f * msin(u * PI_F) + 0.01f * msin(f->t * 0.7f) * u, mixf(-1.0f, d->hy - 0.02f, u), stem};
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
