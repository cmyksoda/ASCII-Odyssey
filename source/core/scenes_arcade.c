// SPDX-License-Identifier: GPL-3.0-only
// Arcade and terminal scenes: pong played on the beat, a marching alien fleet, a maze typed out on an old computer, a
// spectrum bent into a halo, pulsar lines carrying the music's history into the distance, and a piano roll.
#include "mathx.h"
#include "noise.h"
#include "scene.h"

#include <stdio.h>
#include <string.h>

static float cell_x(const Grid *g, int col) {
    return ((float)col + 0.5f) / (float)g->cols * 2 * g->aspect - g->aspect;
}

static int pitched(Role r) {
    return r == ROLE_BASS || r == ROLE_PAD || r == ROLE_CHORD || r == ROLE_ARP || r == ROLE_LEAD || r == ROLE_BELL ||
           r == ROLE_PLUCK;
}

// Digits three cells wide and five tall.
static const char *const DIGITS[10][5] = {
    {"###", "# #", "# #", "# #", "###"}, {" # ", "## ", " # ", " # ", "###"}, {"###", "  #", "###", "#  ", "###"},
    {"###", "  #", "###", "  #", "###"}, {"# #", "# #", "###", "  #", "  #"}, {"###", "#  ", "###", "  #", "###"},
    {"###", "#  ", "###", "# #", "###"}, {"###", "  #", " # ", " # ", " # "}, {"###", "# #", "###", "# #", "###"},
    {"###", "# #", "###", "  #", "###"}};

static void big_digit(Grid *g, int col, int row, int d, Rgbf color) {
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 3; x++)
            if (DIGITS[d % 10][y][x] == '#') put_cell(g, col + x, row + y, '#', color);
}

/* ---------- Pong ---------- */

typedef struct {
    float slope, rally;
    uint32_t salt;
} Pong;

void pong_setup(void *state, Rng *r, const Frame *f) {
    Pong *p = state;
    p->slope = rng_between(r, 0.35f, 0.8f);
    p->rally = (float)rng_range(r, 12, 20);
    p->salt = rng_u32(r);
    (void)f;
}

// Where the ball is on crossing k at its phase: the court is folded so the ball bounces off the walls.
static float pong_y(const Pong *p, float span, float travel) {
    float u = mfract((travel * p->slope + (float)(p->salt % 97) * 0.13f) / (2 * span)) * 2 * span;
    return u < span ? u : 2 * span - u;
}

// Pong playing itself to the tempo: the ball meets a paddle on the beat, the paddle flashes on the kick, and every
// so often someone misses and the score turns over.
void pong_draw(void *state, const Frame *f) {
    Pong *p = state;
    Grid *g = f->grid;
    float beat = f->score->beat, cross = beat < 0.45f ? beat * 2 : beat;
    float k = mfloor(f->t / cross), phase = f->t / cross - k;
    int crossing = (int)k, rally = (int)p->rally, miss = crossing % rally == rally - 1;
    int top = 3, bottom = g->rows - 2, left = 3, right = g->cols - 4;
    float span = (float)(bottom - top - 2);
    Rgbf court = palette_ramp(f->palette, 0.35f), ink = palette_ramp(f->palette, 1.0f), glow = palette_accent(f->palette, 1);
    for (int col = 0; col < g->cols; col++) {
        put_cell(g, col, top - 1, '=', court);
        put_cell(g, col, bottom + 1, '=', court);
    }
    for (int row = top; row <= bottom; row += 2) put_cell(g, g->cols / 2, row, '|', frame_dim(f, court, 0.8f));
    // Points so far: the rallies that ended in a miss, taken in turn by each side.
    int points = crossing / rally;
    big_digit(g, g->cols / 2 - 8, top + 1, (points + 1) / 2 % 10, frame_dim(f, ink, 0.6f));
    big_digit(g, g->cols / 2 + 6, top + 1, points / 2 % 10, frame_dim(f, ink, 0.6f));
    int going_right = !(crossing & 1);
    float x0 = going_right ? (float)left + 1 : (float)right - 1, x1 = going_right ? (float)right - 1 : (float)left + 1;
    float by = (float)top + 1 + pong_y(p, span, (float)crossing + phase);
    // The receiving paddle heads for where the ball will arrive; the other drifts back to the middle.
    float arrive = (float)top + 1 + pong_y(p, span, (float)crossing + 1), mid = (float)(top + bottom) / 2;
    float chase = miss ? 0.45f : smoothstep(0.0f, 0.8f, phase);
    float lp = going_right ? mixf(by, mid, phase) : mixf(by, arrive, chase), rp = going_right ? mixf(by, arrive, chase) : mixf(by, mid, phase);
    float hit = frame_pulse(f, ROLE_KICK, 0.15f, 1.0f);
    for (int side = 0; side < 2; side++) {
        float y = side ? rp : lp;
        int col = side ? right : left, fresh = (side == going_right) ? phase > 0.9f : phase < 0.1f;
        Rgbf c = fresh ? rgbf_mix(ink, glow, 0.5f + 0.5f * hit) : ink;
        for (int d = -2; d <= 2; d++) put_cell(g, col, (int)(y + 0.5f) + d, '#', c);
    }
    for (int s = 3; s >= 0; s--) {
        float back = (float)s * 0.06f, tx = mixf(x0, x1, phase - back), ty = (float)top + 1 + pong_y(p, span, (float)crossing + phase - back);
        if (miss) tx = mixf(x0, going_right ? (float)g->cols + 4 : -5.0f, phase - back);
        put_cell(g, (int)(tx + 0.5f), (int)(ty + 0.5f), s ? (s == 1 ? 'o' : '.') : '@', s ? frame_dim(f, ink, 0.6f / (float)s) : ink);
    }
    f->fx->glow = 0.5f + 0.3f * hit;
}

/* ---------- Invaders ---------- */

#define FLEET_ROWS 5
#define FLEET_COLS 9

typedef struct {
    float cannon_speed;
    uint32_t salt;
} Invaders;

void invaders_setup(void *state, Rng *r, const Frame *f) {
    Invaders *v = state;
    v->cannon_speed = rng_between(r, 0.5f, 0.9f);
    v->salt = rng_u32(r);
    (void)f;
}

static const char *const ALIENS[3][2][2] = {{{"/^^\\", "\\../"}, {"/^^\\", "/..\\"}},
                                            {{"|@@|", "/  \\"}, {"|@@|", "\\  /"}},
                                            {{"{oo}", "/\\/\\"}, {"{oo}", "\\/\\/"}}};

typedef struct {
    int x, y;
} Fleet;

// Where the fleet stands after some steps: marching side to side, a row lower at each turn.
static Fleet fleet_at(const Grid *g, int steps) {
    int width = FLEET_COLS * 7 - 3, room = g->cols - width - 4;
    if (room < 2) room = 2;
    int lap = steps / room, along = steps % room;
    Fleet out = {2 + (lap & 1 ? room - along : along), 3 + (lap > 6 ? 6 : lap)};
    return out;
}

// The alien fleet marching to the beat, waving its arms each step, with a cannon below firing on the snare; aliens it
// hits burst and are gone. A saucer slides across the top now and then.
void invaders_draw(void *state, const Frame *f) {
    Invaders *v = state;
    Grid *g = f->grid;
    dust_stars(f, v->salt, 0.0f, 0.4f);
    float beat = f->score->beat, start = f->t - f->local;
    int steps = (int)(f->local / beat);
    Fleet fleet = fleet_at(g, steps);
    int bottom = g->rows - 3;
    float cannon_x = (float)g->cols * 0.5f + (float)g->cols * 0.38f * msin(f->t * v->cannon_speed);
    int dead[FLEET_ROWS][FLEET_COLS] = {{0}}, score = 0;
    float boom[FLEET_ROWS][FLEET_COLS] = {{0}};
    // Replay the shot's bullets: each flies up from where the cannon stood and takes the lowest alien in its column.
    const ScoreEvent *hits[32];
    int n = f->react > 0 ? recent_notes(f->score, ROLE_SNARE, f->t, f->local, hits, 32) : 0;
    Rgbf shot = palette_ramp(f->palette, 1.0f);
    for (int i = n - 1; i >= 0; i--) {
        float fired = hits[i]->time, x = (float)g->cols * 0.5f + (float)g->cols * 0.38f * msin(fired * v->cannon_speed);
        float rise = (float)(bottom - 2 - (fleet.y + FLEET_ROWS * 3)) / 40.0f, reached = fired + rise;
        if (fired < start) continue;
        Fleet then = fleet_at(g, (int)((reached - start) / beat));
        int column = ((int)x - then.x) / 7;
        int lowest = -1;
        if (column >= 0 && column < FLEET_COLS && ((int)x - then.x) % 7 < 4)
            for (int r = FLEET_ROWS - 1; r >= 0; r--)
                if (!dead[r][column] || boom[r][column] > reached) {
                    lowest = r;
                    break;
                }
        if (f->t < reached) {
            float y = (float)(bottom - 2) - (f->t - fired) * 40.0f;
            put_cell(g, (int)x, (int)y, '|', shot);
            put_cell(g, (int)x, (int)y + 1, '\'', frame_dim(f, shot, 0.5f));
        } else if (lowest >= 0) {
            dead[lowest][column] = 1;
            boom[lowest][column] = reached;
            score += (3 - lowest / 2) * 10;
        }
    }
    for (int r = 0; r < FLEET_ROWS; r++)
        for (int c = 0; c < FLEET_COLS; c++) {
            int kind = r == 0 ? 0 : r < 3 ? 1 : 2, x = fleet.x + c * 7, y = fleet.y + r * 3;
            Rgbf color = palette_accent(f->palette, kind);
            if (dead[r][c]) {
                float age = f->t - boom[r][c];
                if (age < 0.35f) {
                    put_cell(g, x + 1, y, '*', palette_ramp(f->palette, 1.0f));
                    put_cell(g, x, y + 1, '\\', frame_dim(f, color, 1 - age * 2.5f));
                    put_cell(g, x + 3, y + 1, '/', frame_dim(f, color, 1 - age * 2.5f));
                }
                continue;
            }
            const char *const *sprite = ALIENS[kind][steps & 1];
            for (int line = 0; line < 2; line++)
                for (int k = 0; k < 4; k++) put_cell(g, x + k, y + line, sprite[line][k], color);
        }
    // Shields, the cannon, the ground.
    Rgbf shield = palette_accent(f->palette, 1);
    for (int s = 0; s < 4; s++) {
        int x = (int)((float)g->cols * ((float)s + 0.5f) / 4) - 3;
        for (int k = 0; k < 7; k++) {
            put_cell(g, x + k, bottom - 5, k == 0 || k == 6 ? '/' : '#', frame_dim(f, shield, 0.7f));
            if (k != 3) put_cell(g, x + k, bottom - 4, '#', frame_dim(f, shield, 0.7f));
        }
    }
    Rgbf gun = palette_ramp(f->palette, 0.9f);
    put_cell(g, (int)cannon_x, bottom - 1, '^', gun);
    for (int k = -2; k <= 2; k++) put_cell(g, (int)cannon_x + k, bottom, k ? '=' : 'A', gun);
    for (int col = 0; col < g->cols; col++) put_cell(g, col, bottom + 1, '_', palette_ramp(f->palette, 0.4f));
    float saucer = mfract((f->t + (float)(v->salt % 13)) / 9.0f);
    if (saucer < 0.5f) {
        int x = (int)(saucer * 2 * (float)(g->cols + 10)) - 5;
        const char *ufo = "<=O=>";
        for (int k = 0; k < 5; k++) put_cell(g, x + k, 1, ufo[k], palette_accent(f->palette, 0));
    }
    char text[24];
    snprintf(text, sizeof text, "SCORE %04d", score);
    for (int k = 0; text[k]; k++) put_cell(g, 2 + k, 0, text[k], palette_ramp(f->palette, 0.7f));
    f->fx->glow = 0.55f;
}

/* ---------- Maze ---------- */

typedef struct {
    float rate;
    uint32_t salt;
} Maze;

void maze_setup(void *state, Rng *r, const Frame *f) {
    Maze *m = state;
    m->rate = rng_between(r, 180, 320);
    m->salt = rng_u32(r);
    (void)f;
}

// An old home computer running the most famous one-line program there is: it types the line, runs it, and a maze of
// slashes pours down the screen, faster with every note.
void maze_draw(void *state, const Frame *f) {
    Maze *m = state;
    Grid *g = f->grid;
    static const char *const program[3] = {"READY.", "10 PRINT CHR$(205.5+RND(1)); : GOTO 10", "RUN"};
    Rgbf text = palette_ramp(f->palette, 0.9f), dim = palette_ramp(f->palette, 0.55f);
    int notes = 0;
    static const Role sung[5] = {ROLE_LEAD, ROLE_BELL, ROLE_ARP, ROLE_PLUCK, ROLE_SNARE};
    for (int v = 0; v < 5; v++) notes += score_count(f->score, sung[v], f->t - f->local, f->t);
    // Typing the program takes a couple of seconds; after that, characters pour at a steady rate plus a burst per note.
    const int pause = 12;
    float typed = f->local * 32.0f;
    int header = 0;
    for (int line = 0; line < 3; line++) header += (int)strlen(program[line]) + pause;
    float poured = typed > (float)header ? (typed - (float)header) / 32.0f * m->rate + (float)notes * 8.0f * f->react : 0;
    int rows_used = 3 + (int)(poured / (float)g->cols) + 1, scroll = rows_used > g->rows ? rows_used - g->rows : 0;
    float left = typed;
    for (int line = 0; line < 3 && left > 0; line++) {
        int len = (int)strlen(program[line]), shown = left < (float)len ? (int)left : len, row = line - scroll;
        for (int k = 0; k < shown; k++) put_cell(g, k + 1, row, program[line][k], text);
        left -= (float)(len + pause);
    }
    int total = (int)poured;
    for (int i = 0; i < total; i++) {
        int row = 3 + i / g->cols - scroll, col = i % g->cols;
        if (row < 0) continue;
        if (row >= g->rows) break;
        uint32_t h = hash_u32(m->salt + (uint32_t)i * 2654435761u);
        float age = (poured - (float)i) / (float)g->cols;
        put_cell(g, col, row, h & 1 ? '/' : '\\', rgbf_mix(text, dim, saturate(age * 0.08f)));
    }
    // The cursor blinks where the next character will land.
    int cursor_row = 3 + total / g->cols - scroll, cursor_col = total % g->cols;
    if (total == 0) {
        int line = 0;
        float l2 = typed;
        while (line < 2 && l2 > (float)((int)strlen(program[line]) + pause)) l2 -= (float)((int)strlen(program[line]) + pause), line++;
        cursor_row = line - scroll, cursor_col = 1 + (int)mmin(l2, (float)strlen(program[line]));
    }
    if (mfract(f->t * 2.0f) < 0.6f) put_cell(g, cursor_col, cursor_row, '#', text);
    f->fx->glow = 0.5f;
}

/* ---------- Halo ---------- */

#define HALO_BARS 72

typedef struct {
    float spin, inner;
    uint32_t salt;
} Halo;

void halo_setup(void *state, Rng *r, const Frame *f) {
    Halo *h = state;
    h->spin = rng_between(r, 0.08f, 0.2f) * (rng_chance(r, 50) ? 1 : -1);
    h->inner = rng_between(r, 0.26f, 0.34f);
    h->salt = rng_u32(r);
    (void)f;
}

// The spectrum bent into a ring: every note pushes out the bars at its pitch, mirrored left and right; the kick swells
// the glowing core, and the ring slowly turns.
void halo_draw(void *state, const Frame *f) {
    Halo *h = state;
    Grid *g = f->grid;
    float level[HALO_BARS] = {0};
    for (int role = ROLE_KICK; role < ROLE_COUNT; role++) {
        const ScoreEvent *notes[32];
        int n = recent_notes(f->score, (Role)role, f->t, 1.2f, notes, 32);
        for (int i = 0; i < n; i++) {
            float age = f->t - notes[i]->time, now = notes[i]->velocity * mexp(-age / (f->calm ? 0.9f : 0.35f));
            int at = pitched((Role)role) ? clampi((notes[i]->note - 25) * (HALO_BARS / 2) / 72, 0, HALO_BARS / 2 - 1)
                                          : role == ROLE_KICK ? 0 : HALO_BARS / 4;
            for (int d = -2; d <= 2; d++) {
                int b = at + d;
                if (b < 0 || b >= HALO_BARS / 2) continue;
                float w = d ? (d == 1 || d == -1 ? 0.6f : 0.3f) : 1.0f;
                level[b] = mmax(level[b], now * w);
                level[HALO_BARS - 1 - b] = mmax(level[HALO_BARS - 1 - b], now * w);
            }
        }
    }
    float kick = frame_pulse(f, ROLE_KICK, 0.2f, 1.0f), turn = f->t * h->spin, scale = g->aspect * 0.75f;
    Canvas *c = f->canvas;
    canvas_clear(c);
    canvas_disc(c, 0, 0, h->inner * (0.55f + 0.12f * kick), 0.7f + 0.3f * kick, palette_accent(f->palette, 1));
    frame_resolve(f);
    dust_stars(f, h->salt, 0.0f, 0.5f);
    for (int b = 0; b < HALO_BARS; b++) {
        float a = turn + (float)b / HALO_BARS * TAU_F;
        float swell = 0.04f + 0.05f * (0.5f + 0.5f * msin(f->t * 1.3f + (float)b * 0.4f));
        float len = swell + 0.32f * saturate(level[b] * frame_bright(f) * 1.6f);
        float r0 = h->inner * 1.12f, r1 = r0 + len, ca = mcos(a), sa = msin(a);
        Rgbf color = rgbf_mix(palette_accent(f->palette, b * 3 / HALO_BARS), palette_ramp(f->palette, 1.0f), saturate(len * 1.8f));
        StrokePoint bar[2] = {{ca * r0, sa * r0, frame_dim(f, color, 0.6f)}, {ca * r1, sa * r1, color}};
        stroke_draw(f->stroker, bar, 2, 0);
        put_cell(g, cell_col(g, ca * (r1 + 0.03f)), cell_row(g, sa * (r1 + 0.03f)), '.', frame_dim(f, color, 0.7f));
    }
    // Two rings: one on which the bars stand, and a dotted one turning the other way inside it.
    StrokePoint ring[64];
    for (int k = 0; k < 64; k++) {
        float a = (float)k / 64 * TAU_F;
        ring[k] = (StrokePoint){mcos(a) * h->inner * 1.08f, msin(a) * h->inner * 1.08f, palette_ramp(f->palette, 0.85f)};
    }
    stroke_draw(f->stroker, ring, 64, 1);
    for (int k = 0; k < 40; k++) {
        float a = -turn * 2 + (float)k / 40 * TAU_F;
        put_cell(g, cell_col(g, mcos(a) * h->inner * 0.82f), cell_row(g, msin(a) * h->inner * 0.82f), k % 2 ? '.' : ':',
                 palette_ramp(f->palette, 0.7f));
    }
    (void)scale;
    f->fx->glow = 0.6f + 0.3f * kick;
}

/* ---------- Pulsar ---------- */

#define PULSAR_LINES 24

typedef struct {
    float rate, width;
    uint32_t salt;
} Pulsar;

void pulsar_setup(void *state, Rng *r, const Frame *f) {
    Pulsar *p = state;
    p->rate = rng_between(r, 0.8f, 1.4f);
    p->width = rng_between(r, 0.22f, 0.32f);
    p->salt = rng_u32(r);
    (void)f;
}

// Stacked waveforms in the manner of a famous pulsar plot: a new line is drawn at the front every beat or two, shaped
// by how hard the music hit just then, and the lines march back into the distance carrying that history; nearer
// lines hide the ones behind them.
void pulsar_draw(void *state, const Frame *f) {
    Pulsar *p = state;
    Grid *g = f->grid;
    float every = f->score->beat * (f->score->beat < 0.45f ? 2 : 1) / p->rate, now = f->t / every;
    int newest = (int)mfloor(now);
    float slide = now - (float)newest, front = -0.82f, spacing = 1.55f / PULSAR_LINES;
    float skyline[256];
    int cols = g->cols < 256 ? g->cols : 256;
    for (int i = 0; i < cols; i++) skyline[i] = -2;
    Rgbf ink = palette_ramp(f->palette, 1.0f), far = palette_ramp(f->palette, 0.35f);
    for (int k = 0; k < PULSAR_LINES; k++) {
        int line = newest - k;
        float depth = (float)k + slide, base = front + depth * spacing;
        if (base > 0.85f) break;
        // How hard the music hit when this line was drawn sets its height.
        float born = (float)line * every;
        float hit = born > 0 ? score_pulse(f->score, ROLE_KICK, born, 0.4f) + 0.6f * score_pulse(f->score, ROLE_BASS, born, 0.5f) : 0;
        float amp = 0.06f + 0.22f * saturate(hit * f->react + (1 - f->react) * 0.5f) + 0.05f * score_energy(f->score, born);
        Rgbf color = rgbf_mix(ink, far, saturate(depth / PULSAR_LINES));
        float prev = 0;
        for (int col = 0; col < cols; col++) {
            float x = cell_x(g, col) / g->aspect;
            if (mabs(x) > 0.62f) continue;
            float env = mexp(-x * x / (2 * p->width * p->width));
            float wave = noise_fbm(x * 7 + (float)(p->salt % 89), (float)line * 0.37f) + 0.5f * noise_fbm(x * 19 + (float)line, 3.7f);
            float y = base + amp * env * saturate(wave - 0.35f) * 3.0f + 0.01f * noise_fbm(x * 40, (float)line);
            if (y > skyline[col]) {
                int glyph = col > 0 && y - prev > 0.035f ? '/' : col > 0 && prev - y > 0.035f ? '\\' : '_';
                put_cell(g, col, cell_row(g, y), glyph, color);
                skyline[col] = y;
            }
            prev = y;
        }
    }
    f->fx->glow = 0.5f;
}

/* ---------- Piano roll ---------- */

typedef struct {
    float window;
    int low;
    uint32_t salt;
} PianoRoll;

void piano_setup(void *state, Rng *r, const Frame *f) {
    PianoRoll *p = state;
    p->window = rng_between(r, 2.0f, 3.0f);
    p->low = 37 + 12 * rng_range(r, 0, 2);
    p->salt = rng_u32(r);
    (void)f;
}

// Semitone in the octave of each white key, and whether a black key follows it.
static const int WHITE_STEP[7] = {0, 2, 4, 5, 7, 9, 11};
static const int HAS_BLACK[7] = {1, 1, 0, 1, 1, 1, 0};

// The column a note's key sits at, and whether it is a black key; -1 off the keyboard.
static int piano_key(const PianoRoll *p, int note, int left, int whites, int *black) {
    int rel = note - p->low, octave = rel / 12, step = ((rel % 12) + 12) % 12;
    if (rel < 0) return -1;
    for (int w = 0; w < 7; w++) {
        if (WHITE_STEP[w] == step) {
            *black = 0;
            int index = octave * 7 + w;
            return index < whites ? left + index * 3 + 1 : -1;
        }
        if (HAS_BLACK[w] && WHITE_STEP[w] + 1 == step) {
            *black = 1;
            int index = octave * 7 + w;
            return index + 1 < whites ? left + index * 3 + 3 : -1;
        }
    }
    return -1;
}

// A piano roll: the melody's coming notes fall toward a keyboard, each landing on its own key the moment it sounds,
// and the key lights up as it plays.
void piano_draw(void *state, const Frame *f) {
    PianoRoll *p = state;
    Grid *g = f->grid;
    int whites = (g->cols - 2) / 3, left = (g->cols - whites * 3) / 2, keys_top = g->rows - 6, roll_top = 1;
    if (whites > 35) whites = 35;
    left = (g->cols - whites * 3) / 2;
    float height = (float)(keys_top - roll_top);
    // Beat lines slide down with the music, solid at each bar.
    float beat = f->score->beat;
    int per_bar = f->score->info->beats_per_bar;
    for (int b = 0; b < 12; b++) {
        float t_line = mfloor(f->t / beat) * beat + (float)b * beat, y = (float)keys_top - (t_line - f->t) / p->window * height;
        if (y < (float)roll_top || y >= (float)keys_top) continue;
        int bar = ((int)mfloor(t_line / beat + 0.5f)) % per_bar == 0;
        for (int col = left; col < left + whites * 3; col += bar ? 1 : 2)
            put_cell(g, col, (int)y, bar ? '-' : '.', palette_ramp(f->palette, bar ? 0.35f : 0.25f));
    }
    static const Role sung[5] = {ROLE_LEAD, ROLE_BELL, ROLE_ARP, ROLE_PLUCK, ROLE_BASS};
    float lit[128] = {0};
    for (int v = 0; v < 5; v++) {
        Rgbf color = v == 4 ? palette_ramp(f->palette, 0.6f) : palette_accent(f->palette, v);
        const ScoreEvent *notes[48];
        int n = recent_notes(f->score, sung[v], f->t + p->window, p->window + 0.6f, notes, 48);
        for (int i = 0; i < n; i++) {
            int black = 0, note = notes[i]->note + (v == 4 ? 12 : 0), col = piano_key(p, note, left, whites, &black);
            if (col < 0) continue;
            float ahead = notes[i]->time - f->t;
            if (ahead >= 0) {
                // Falling: a block as long as a sixteenth note, lit brighter as it nears its key.
                float y = (float)keys_top - ahead / p->window * height, len = 0.25f * beat / p->window * height + 1;
                for (int k = 0; k < (int)len; k++) {
                    int row = (int)y - 1 - k;
                    if (row < roll_top || row >= keys_top) continue;
                    put_cell(g, col, row, black ? '!' : '#', frame_dim(f, color, 0.5f + 0.5f * (1 - ahead / p->window)));
                    if (!black) put_cell(g, col + 1, row, '#', frame_dim(f, color, 0.45f + 0.45f * (1 - ahead / p->window)));
                }
            } else {
                lit[note & 127] = mmax(lit[note & 127], mexp(ahead / 0.25f) * frame_bright(f));
            }
        }
    }
    // The keyboard: white keys three cells wide, black keys between, lit where notes are sounding.
    Rgbf ivory = palette_ramp(f->palette, 0.85f), ebony = palette_ramp(f->palette, 0.3f), glow = palette_accent(f->palette, 1);
    for (int w = 0; w < whites; w++) {
        int step = WHITE_STEP[w % 7], note = p->low + (w / 7) * 12 + step, col = left + w * 3;
        float on = lit[note & 127];
        for (int row = keys_top; row < g->rows - 1; row++) {
            put_cell(g, col, row, '|', frame_dim(f, ivory, 0.7f));
            for (int k = 1; k < 3; k++) put_cell(g, col + k, row, on > 0.05f ? '#' : ' ', rgbf_mix(ivory, glow, on));
        }
        put_cell(g, col, g->rows - 1, '|', frame_dim(f, ivory, 0.7f));
        for (int k = 1; k < 3; k++) put_cell(g, col + k, g->rows - 1, '_', frame_dim(f, ivory, 0.7f));
        if (HAS_BLACK[w % 7] && w + 1 < whites) {
            float b = lit[(note + 1) & 127];
            for (int row = keys_top; row < keys_top + 3; row++)
                for (int k = 2; k < 4; k++) put_cell(g, col + k, row, '#', rgbf_mix(ebony, glow, b));
        }
    }
    put_cell(g, left + whites * 3, keys_top, '|', frame_dim(f, ivory, 0.7f));
    for (int col = left; col <= left + whites * 3; col++) put_cell(g, col, keys_top - 1, '=', palette_ramp(f->palette, 0.6f));
    f->fx->glow = 0.5f;
}
