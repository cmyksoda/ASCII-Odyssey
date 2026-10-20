// SPDX-License-Identifier: GPL-3.0-only
// Games and gadgets: a platformer, a race into the horizon, tetris, snake, an adventure's overworld, asteroids,
// breakout, a sound chip's scopes, a lighthouse, and a drum machine.
#include "mathx.h"
#include "noise.h"
#include "scene.h"

#include <stdio.h>
#include <string.h>

static void text_at(Grid *g, int col, int row, const char *s, Rgbf color) {
    for (int k = 0; s[k]; k++) put_cell(g, col + k, row, s[k], color);
}

/* ---------- Platformer ---------- */

typedef struct {
    float speed;
    uint32_t salt;
} Platformer;

void platformer_setup(void *state, Rng *r, const Frame *f) {
    Platformer *p = state;
    p->speed = rng_between(r, 7.0f, 11.0f);
    p->salt = rng_u32(r);
    (void)f;
}

// The world runs in stretches of twelve columns: flat ground, a raised ledge, or a gap to jump. The first two are
// always flat, so every shot starts on firm ground.
#define STRETCH 12

static int stretch_kind(const Platformer *p, int s) {
    if (s < 2) return 0;
    uint32_t h = hash_u32(p->salt ^ (uint32_t)s * 2654435761u) % 6;
    return h < 3 ? 0 : h < 5 ? 1 : 2;
}

// Ground height in rows above the floor at a world column, or -1 over a gap.
static int ground_at(const Platformer *p, int wc) {
    int s = wc / STRETCH, at = wc % STRETCH, kind = stretch_kind(p, s);
    if (kind == 1 && at >= 4 && at < 9) return 3;
    if (kind == 2 && at >= 4 && at < 8) return -1;
    return 0;
}

// The hero runs at a third of the way across, leaping every ledge and gap in one arc, and hops on the kick between.
void platformer_draw(void *state, const Frame *f) {
    Platformer *p = state;
    Grid *g = f->grid;
    float scroll = frame_travel(f, p->speed, p->speed * 0.6f);
    int floor_row = g->rows - 4, hx = g->cols / 3;
    Rgbf sky = palette_ramp(f->palette, 0.3f), ground = palette_ramp(f->palette, 0.55f), grass = palette_accent(f->palette, 2);
    Rgbf coin = palette_accent(f->palette, 1), hero = palette_accent(f->palette, 0), ink = palette_ramp(f->palette, 0.9f);
    // Clouds and far hills scroll slower, for depth, and grow with the screen so a tall one never stands empty.
    float tall = (float)g->rows / 30.0f;
    for (int k = 0; k < 8; k++) {
        uint32_t id = p->salt ^ (uint32_t)k * 40503u;
        float span = (float)g->cols + 16;
        int x = (int)(mfract(hash01(id) - scroll * 0.25f / span) * span) - 8, y = 2 + (int)(hash01(id + 3) * (float)(floor_row / 2));
        text_at(g, x, y, " .--. ", frame_dim(f, sky, 0.9f));
        text_at(g, x, y + 1, "(    )", frame_dim(f, sky, 0.9f));
        text_at(g, x + 1, y + 2, "`--'", frame_dim(f, sky, 0.9f));
    }
    for (int col = 0; col < g->cols; col++) {
        float wx = (float)col + scroll * 0.5f;
        int hill = (int)((3 + 2.5f * msin(wx * 0.07f + (float)(p->salt % 31)) + 1.5f * msin(wx * 0.19f)) * tall);
        for (int y = 0; y < hill; y++) put_cell_under(g, col, floor_row - 2 - y, y == hill - 1 ? '^' : ':', frame_dim(f, ground, 0.55f));
    }
    // The ground itself, with question blocks over some flat stretches and coins over every leap. The hero takes
    // each coin as it reaches it, leaving a sparkle for a moment.
    int coins = 0;
    for (int col = 0; col < g->cols; col++) {
        int wc = (int)(scroll + (float)col), h = ground_at(p, wc), s = wc / STRETCH, at = wc % STRETCH;
        if (h >= 0) {
            for (int y = floor_row - h; y < g->rows; y++)
                put_cell(g, col, y, y == floor_row - h ? '=' : ((y + wc) % 2 ? '#' : 'H'), y == floor_row - h ? grass : ground);
        }
        int kind = stretch_kind(p, s);
        if (kind == 0 && s >= 2 && hash_u32(p->salt + (uint32_t)s) % 3 == 0 && at >= 5 && at < 8)
            put_cell(g, col, floor_row - 6, at == 6 ? '?' : (at == 5 ? '[' : ']'), coin);
        if (kind && at >= 3 && at <= 9 && at % 2 == 1) {
            float u = (float)(at - 1) / 10.0f;
            int y = floor_row - 1 - (int)(7 * 4 * u * (1 - u));
            static const char spin[4] = {'o', 'O', '0', 'O'};
            if (col > hx)
                put_cell(g, col, y, spin[(int)(f->t * 8 + (float)wc) & 3], coin);
            else if (col >= hx - 3)
                put_cell(g, col, y - 1, col == hx ? '*' : '+', frame_dim(f, coin, (float)(col - hx + 4) / 4.0f));
        }
    }
    // Where the hero is in the air: one arc over the feature ahead, or a small hop on the kick.
    float hw = scroll + (float)hx;
    int s = (int)hw / STRETCH, kind = stretch_kind(p, s);
    float u = (hw - (float)(s * STRETCH + 1)) / 10.0f, lift = 0;
    if (kind && u > 0 && u < 1)
        lift = 7 * 4 * u * (1 - u);
    else if (f->react > 0 && !f->still) {
        float since = score_since(f->score, ROLE_KICK, f->t);
        if (since < 0.3f) lift = 1.6f * 4 * (since / 0.3f) * (1 - since / 0.3f);
    }
    for (int k = 0; k <= s; k++) coins += stretch_kind(p, k) ? (k < s ? 4 : (int)mmax(0, u * 4)) : 0;
    int y = floor_row - 3 - (int)(lift + 0.5f), stride = (int)(f->t * 9) & 1, airborne = lift > 0.4f;
    static const char *const RUN[2][3] = {{" o ", "/|\\", "/ \\"}, {" o ", "\\|/", " | "}};
    static const char *const JUMP[3] = {"\\o/", " | ", "/ \\"};
    for (int line = 0; line < 3; line++) {
        const char *row = airborne ? JUMP[line] : RUN[stride][line];
        for (int k = 0; k < 3; k++)
            if (row[k] != ' ') put_cell(g, hx - 1 + k, y + line, row[k], hero);
    }
    char text[32];
    snprintf(text, sizeof text, "WORLD 1-%d   COINS %02d", 1 + (int)(scroll / 400) % 4, coins % 100);
    text_at(g, 2, 0, text, ink);
    f->fx->glow = 0.35f + 0.25f * frame_pulse(f, ROLE_KICK, 0.15f, 1.0f);
}

/* ---------- Race track ---------- */

typedef struct {
    float speed, bend;
    int side;
    uint32_t salt;
} Race;

void race_setup(void *state, Rng *r, const Frame *f) {
    Race *p = state;
    p->speed = rng_between(r, 30.0f, 46.0f);
    p->bend = rng_between(r, 0.25f, 0.45f);
    p->side = rng_chance(r, 50);
    p->salt = rng_u32(r);
    (void)f;
}

// A road racing toward a low horizon, bending one way and the other, its stripes rushing past at the music's speed.
void race_draw(void *state, const Frame *f) {
    Race *p = state;
    Grid *g = f->grid;
    float travel = frame_travel(f, p->speed, p->speed * 0.8f);
    int horizon = (int)((float)g->rows * 0.42f), depth_rows = g->rows - horizon;
    float curve = msin(travel * 0.004f + (float)(p->salt % 17)) * p->bend;
    Rgbf far = palette_ramp(f->palette, 0.25f), road = palette_ramp(f->palette, 0.4f), line = palette_ramp(f->palette, 0.95f);
    Rgbf kerb_a = palette_accent(f->palette, 0), kerb_b = palette_ramp(f->palette, 0.85f), grass = palette_accent(f->palette, 2);
    dust_stars(f, p->salt, curve * 2, 0.5f);
    // Mountains on the horizon slide against the bend.
    for (int col = 0; col < g->cols; col++) {
        float wx = (float)col * 0.09f - curve * 6 + (float)(p->salt % 23);
        int h = (int)(2 + 2.2f * (0.5f + 0.5f * msin(wx)) + 1.6f * (0.5f + 0.5f * msin(wx * 2.7f + 1)));
        for (int y = 0; y < h; y++) put_cell(g, col, horizon - 1 - y, y == h - 1 ? '^' : '/', frame_dim(f, far, 0.6f + 0.1f * (float)y));
    }
    for (int row = horizon; row < g->rows; row++) {
        float near = (float)(row - horizon + 1) / (float)depth_rows, z = 1 / near;
        // Farther rows swing further with the bend.
        float center = (float)g->cols * 0.5f + curve * (float)g->cols * (1 - near) * (1 - near);
        float half = (float)g->cols * 0.46f * near + 1, band = mfract((travel + z * 22) / 18.0f);
        int stripe = band < 0.5f;
        for (int col = 0; col < g->cols; col++) {
            float off = mabs((float)col + 0.5f - center);
            if (off < half) {
                int kerb = off > half * 0.88f, lane = off < half * 0.035f + 0.5f && stripe;
                char c = kerb ? (stripe ? '#' : '=') : lane ? '|' : (row % 2 ? '.' : ' ');
                Rgbf color = kerb ? (stripe ? kerb_a : kerb_b) : lane ? line : road;
                if (c != ' ') put_cell(g, col, row, c, frame_dim(f, color, 0.55f + 0.45f * near));
            } else {
                put_cell(g, col, row, stripe ? ',' : '.', frame_dim(f, grass, (stripe ? 0.55f : 0.35f) * (0.5f + 0.5f * near)));
            }
        }
    }
    // The car, leaning into the bend, jolted by the kick.
    int bump = frame_pulse(f, ROLE_KICK, 0.12f, 1.0f) > 0.55f, cx = g->cols / 2 - 4 + (int)(curve * 6), cy = g->rows - 4 - bump;
    static const char *const CAR[3] = {"  ____  ", " /|__|\\ ", "[o]--[o]"};
    Rgbf body = palette_accent(f->palette, 1);
    for (int line = 0; line < 3; line++) text_at(g, cx, cy + line, CAR[line], body);
    f->fx->glow = 0.4f + 0.2f * frame_pulse(f, ROLE_KICK, 0.15f, 1.0f);
}

/* ---------- Tetris ---------- */

#define WELL_W 10
#define WELL_H 18

typedef struct {
    uint32_t salt;
    int garbage;
} Tetris;

void tetris_setup(void *state, Rng *r, const Frame *f) {
    Tetris *t = state;
    t->salt = rng_u32(r);
    t->garbage = rng_range(r, 4, 8);
    (void)f;
}

// The seven pieces, each as four rotations of a 4x4 mask, bit 15 the top left.
static const uint16_t PIECES[7][4] = {
    {0x0F00, 0x2222, 0x00F0, 0x4444}, {0x8E00, 0x6440, 0x0E20, 0x44C0}, {0x2E00, 0x4460, 0x0E80, 0xC440},
    {0x6600, 0x6600, 0x6600, 0x6600}, {0x6C00, 0x4620, 0x06C0, 0x8C40}, {0x4E00, 0x4640, 0x0E40, 0x4C40},
    {0xC600, 0x2640, 0x0C60, 0x4C80}};

typedef struct {
    uint8_t cell[WELL_H][WELL_W];
} Well;

static int fits(const Well *w, int piece, int rot, int x, int y) {
    uint16_t m = PIECES[piece][rot];
    for (int i = 0; i < 16; i++) {
        if (!(m & (0x8000 >> i))) continue;
        int cx = x + i % 4, cy = y + i / 4;
        if (cx < 0 || cx >= WELL_W || cy >= WELL_H || (cy >= 0 && w->cell[cy][cx])) return 0;
    }
    return 1;
}

static void garbage(Well *w, const Tetris *t, int game) {
    memset(w, 0, sizeof *w);
    for (int y = WELL_H - t->garbage; y < WELL_H; y++) {
        int hole = (int)(hash_u32(t->salt ^ (uint32_t)(y * 131 + game * 7919)) % WELL_W);
        for (int x = 0; x < WELL_W; x++) w->cell[y][x] = x == hole ? 0 : (uint8_t)(1 + (x + y) % 7);
    }
}

// Where a little player would put this piece: the lowest landing, then the fewest holes left under it.
static void choose_drop(const Well *w, int piece, uint32_t salt, int *best_rot, int *best_x, int *best_y) {
    int best = -1000000;
    *best_rot = 0, *best_x = 3, *best_y = 0;
    for (int rot = 0; rot < 4; rot++)
        for (int x = -2; x < WELL_W; x++) {
            if (!fits(w, piece, rot, x, 0)) continue;
            int y = 0;
            while (fits(w, piece, rot, x, y + 1)) y++;
            int holes = 0;
            uint16_t m = PIECES[piece][rot];
            for (int i = 0; i < 16; i++)
                if ((m & (0x8000 >> i)) && y + i / 4 + 1 < WELL_H && !w->cell[y + i / 4 + 1][x + i % 4] &&
                    !(i / 4 < 3 && (m & (0x8000 >> (i + 4)))))
                    holes++;
            int score = y * 10 - holes * 25 + (int)(hash_u32(salt ^ (uint32_t)(rot * 31 + x + 7)) % 7);
            if (score > best) best = score, *best_rot = rot, *best_x = x, *best_y = y;
        }
}

// A game of tetris played to the beat: each piece falls a row a sixteenth (twice as fast in a drop), lands where a
// tidy player would put it, and full lines clear. When the stack reaches the top the game starts over.
void tetris_draw(void *state, const Frame *f) {
    Tetris *t = state;
    Grid *g = f->grid;
    Well w;
    int game = 0, lines = 0, clearing_rows = 0;
    garbage(&w, t, game);
    float clock = f->t - f->local, beat = f->score->beat, clear_at = -9;
    int piece = 0, rot = 0, x = 3, y = 0, falling = 0;
    for (int n = 0; n < 200; n++) {
        piece = (int)(hash_u32(t->salt + (uint32_t)n * 2654435761u) % 7);
        int target_y;
        choose_drop(&w, piece, t->salt + (uint32_t)n, &rot, &x, &target_y);
        if (!fits(&w, piece, rot, x, 0)) {
            garbage(&w, t, ++game);
            continue;
        }
        float step = beat / (score_energy(f->score, clock) > 0.8f ? 8.0f : 4.0f), lands = clock + step * (float)(target_y + 1);
        if (f->t < lands) {
            y = (int)((f->t - clock) / step);
            falling = 1;
            break;
        }
        uint16_t m = PIECES[piece][rot];
        for (int i = 0; i < 16; i++)
            if ((m & (0x8000 >> i)) && target_y + i / 4 >= 0) w.cell[target_y + i / 4][x + i % 4] = (uint8_t)(1 + piece);
        int cleared = 0;
        for (int row = WELL_H - 1; row >= 0; row--) {
            int full = 1;
            for (int c = 0; c < WELL_W; c++) full &= w.cell[row][c] != 0;
            if (!full) continue;
            memmove(&w.cell[1], &w.cell[0], sizeof w.cell[0] * (size_t)row);
            memset(&w.cell[0], 0, sizeof w.cell[0]);
            row++, cleared++;
        }
        if (cleared) lines += cleared, clear_at = lands, clearing_rows = cleared;
        clock = lands + step;
    }
    int left = g->cols / 2 - WELL_W - 2, top = (g->rows - WELL_H) / 2 - 1;
    Rgbf wall = palette_ramp(f->palette, 0.5f), ink = palette_ramp(f->palette, 0.95f);
    for (int row = 0; row < WELL_H; row++) {
        text_at(g, left, top + row, "<!", wall);
        text_at(g, left + 2 + WELL_W * 2, top + row, "!>", wall);
        for (int c = 0; c < WELL_W; c++) {
            int v = w.cell[row][c];
            if (v) {
                Rgbf color = palette_accent(f->palette, v % 3);
                if (v % 2) color = rgbf_mix(color, ink, 0.3f);
                text_at(g, left + 2 + c * 2, top + row, "[]", color);
            } else {
                put_cell(g, left + 3 + c * 2, top + row, '.', frame_dim(f, wall, 0.5f));
            }
        }
    }
    if (falling) {
        uint16_t m = PIECES[piece][rot];
        for (int i = 0; i < 16; i++)
            if ((m & (0x8000 >> i)) && y + i / 4 >= 0) text_at(g, left + 2 + (x + i % 4) * 2, top + y + i / 4, "[]", ink);
    }
    char floor_line[2 * WELL_W + 5];
    memset(floor_line, '=', sizeof floor_line - 1);
    floor_line[0] = '<', floor_line[1] = '!', floor_line[sizeof floor_line - 3] = '!', floor_line[sizeof floor_line - 2] = '>';
    floor_line[sizeof floor_line - 1] = 0;
    text_at(g, left, top + WELL_H, floor_line, wall);
    for (int c = 0; c < WELL_W; c++) text_at(g, left + 2 + c * 2, top + WELL_H + 1, "\\/", frame_dim(f, wall, 0.7f));
    char text[24];
    snprintf(text, sizeof text, "LINES %3d", lines);
    text_at(g, left + 2 * WELL_W + 6, top + 2, text, ink);
    snprintf(text, sizeof text, "LEVEL %3d", 1 + lines / 10 + game);
    text_at(g, left + 2 * WELL_W + 6, top + 4, text, ink);
    // A clear lights the well for a moment, gently when the viewer asked for calm.
    float flash = clear_at > 0 ? mmax(0, 1 - (f->t - clear_at) / 0.4f) * frame_bright(f) : 0;
    f->fx->glow = 0.4f + 0.3f * flash * (float)(clearing_rows > 1 ? 1 : 0.6f);
}

/* ---------- Snake ---------- */

typedef struct {
    uint32_t salt;
} Snake;

void snake_setup(void *state, Rng *r, const Frame *f) {
    Snake *s = state;
    s->salt = rng_u32(r);
    (void)f;
}

// A loop through every cell of a w by h field (h even): along the top, back and forth down the rows, up the left.
static void snake_cell(int i, int w, int h, int *x, int *y) {
    int n = w * h;
    i = ((i % n) + n) % n;
    if (i < w) {
        *x = i, *y = 0;
    } else if (i < n - (h - 1)) {
        int k = i - w, row = 1 + k / (w - 1), along = k % (w - 1);
        *y = row, *x = row % 2 ? w - 1 - along : 1 + along;
    } else {
        *x = 0, *y = h - 1 - (i - (n - (h - 1)));
    }
}

// The snake game, played a cell a sixteenth: it loops its field forever, growing at every apple it passes.
void snake_draw(void *state, const Frame *f) {
    Snake *s = state;
    Grid *g = f->grid;
    int w = g->cols - 10, h = (g->rows - 6) & ~1, left = 5, top = 3, n = w * h;
    float step = f->score->beat / 4;
    int steps = (int)(f->local / step), start = (int)(hash_u32(s->salt) % (uint32_t)n), head = start + steps;
    // Apples sit at fixed points of the loop, one every few dozen cells, so it eats every few seconds.
    int gap = n / 28 > 12 ? n / 28 : 12, eaten = 0;
    int first = start / gap;
    for (int k = first; k < first + 400; k++) {
        int at = k * gap + (int)(hash_u32(s->salt ^ (uint32_t)k * 2246822519u) % (uint32_t)(gap / 2 + 1));
        if (at > start + 4 && at <= head) eaten++;
    }
    int length = 6 + eaten * 2;
    if (length > n / 4) length = n / 4;
    Rgbf border = palette_ramp(f->palette, 0.5f), body = palette_accent(f->palette, 2), apple = palette_accent(f->palette, 0);
    Rgbf ink = palette_ramp(f->palette, 0.95f);
    for (int x = -1; x <= w; x++) {
        put_cell(g, left + x, top - 1, '#', border);
        put_cell(g, left + x, top + h, '#', border);
    }
    for (int y = 0; y < h; y++) {
        put_cell(g, left - 1, top + y, '#', border);
        put_cell(g, left + w, top + y, '#', border);
    }
    float blink = f->react > 0 ? score_pulse(f->score, ROLE_HAT, f->t, 0.1f) * frame_bright(f) : 0;
    for (int k = first; k < first + 400; k++) {
        int at = k * gap + (int)(hash_u32(s->salt ^ (uint32_t)k * 2246822519u) % (uint32_t)(gap / 2 + 1));
        if (at <= head || at > head + n - length) continue;
        int x, y;
        snake_cell(at, w, h, &x, &y);
        put_cell(g, left + x, top + y, '@', rgbf_mix(apple, ink, blink * 0.6f));
    }
    for (int k = length - 1; k >= 0; k--) {
        int x, y;
        snake_cell(head - k, w, h, &x, &y);
        put_cell(g, left + x, top + y, k == 0 ? 'O' : k % 4 == 0 ? '=' : 'o', k == 0 ? ink : frame_dim(f, body, 1 - 0.4f * (float)k / (float)length));
    }
    char text[48];
    snprintf(text, sizeof text, "SNAKE   SCORE %04d   LENGTH %d", eaten * 10, length);
    text_at(g, left, top - 2, text, ink);
    f->fx->glow = 0.4f + 0.2f * frame_pulse(f, ROLE_KICK, 0.15f, 1.0f);
}

/* ---------- Overworld ---------- */

typedef struct {
    float speed, ox, oy;
    uint32_t salt;
} Overworld;

void overworld_setup(void *state, Rng *r, const Frame *f) {
    Overworld *o = state;
    o->speed = rng_between(r, 1.2f, 2.0f);
    o->ox = rng_between(r, 0, 64);
    o->oy = rng_between(r, 0, 64);
    o->salt = rng_u32(r);
    (void)f;
}

static float overworld_road(const Overworld *o, float x) {
    return 4.0f * msin(x * 0.07f + (float)(o->salt % 29)) + 2.0f * msin(x * 0.19f);
}

// An adventure's map seen from above, two cells to a tile, scrolling along the road the hero walks: water, sand,
// meadows, forest and mountains, with a castle along the way now and then.
void overworld_draw(void *state, const Frame *f) {
    Overworld *o = state;
    Grid *g = f->grid;
    float cx = frame_travel(f, o->speed, 0) + 20, cy = overworld_road(o, cx);
    int tiles_w = g->cols / 2, tiles_h = g->rows;
    Rgbf water = palette_accent(f->palette, 2), sand = palette_ramp(f->palette, 0.75f), grass = palette_ramp(f->palette, 0.5f);
    Rgbf wood = palette_accent(f->palette, 1), rock = palette_ramp(f->palette, 0.85f), road = palette_ramp(f->palette, 0.95f);
    for (int ty = 0; ty < tiles_h; ty++)
        for (int tx = 0; tx < tiles_w; tx++) {
            float wx = cx + (float)(tx - tiles_w / 2), wy = cy + (float)(ty - tiles_h / 2) * 0.5f;
            float hgt = noise_fbm(wx * 0.06f + o->ox, wy * 0.12f + o->oy);
            int on_road = mabs(wy - overworld_road(o, wx)) < 0.5f;
            uint32_t spot = (uint32_t)((int)mfloor(wx) * 7919 + (int)mfloor(wy * 2) * 131);
            const char *tile;
            Rgbf color;
            if (on_road) {
                tile = hgt < 0.38f ? "==" : "##";
                color = road;
            } else if (hgt < 0.38f) {
                int lap = (int)(f->t * 1.5f + wx * 0.7f + wy * 2) & 3;
                tile = lap == 0 ? "~ " : lap == 2 ? " ~" : "  ";
                color = water;
            } else if (hgt < 0.43f) {
                tile = hash01(spot) < 0.5f ? ". " : "  ";
                color = sand;
            } else if (hgt < 0.6f) {
                float h = hash01(spot);
                tile = h < 0.12f ? "\" " : h < 0.24f ? " ," : "  ";
                color = grass;
            } else if (hgt < 0.72f) {
                tile = hash01(spot) < 0.6f ? "^ " : " ^";
                color = wood;
            } else {
                tile = "/\\";
                color = rock;
            }
            text_at(g, tx * 2, ty, tile, frame_dim(f, color, 0.65f + 0.35f * hgt));
        }
    // A castle by the road every so often.
    for (int k = (int)(cx / 40) - 1; k <= (int)(cx / 40) + 1; k++) {
        float kx = (float)k * 40 + 10 + 8 * hash01(o->salt + (uint32_t)k), ky = overworld_road(o, kx) - 2.5f;
        int col = (int)((kx - cx) + (float)(tiles_w / 2)) * 2, row = (int)((ky - cy) * 2 + (float)(tiles_h / 2));
        static const char *const CASTLE[4] = {"|^| |^|", "|  _  |", "| |#| |", "=======",};
        for (int line = 0; line < 4; line++) text_at(g, col - 3, row - 3 + line, CASTLE[line], rock);
    }
    int bob = (int)(f->t * 4) & 1;
    Rgbf hero = palette_ramp(f->palette, 1.0f);
    put_cell(g, (tiles_w / 2) * 2, tiles_h / 2 - 1 - bob, 'o', hero);
    put_cell(g, (tiles_w / 2) * 2, tiles_h / 2 - bob, '@', hero);
    f->fx->glow = 0.3f;
}

/* ---------- Asteroids ---------- */

#define ROCKS 7

typedef struct {
    float spin;
    uint32_t salt;
} Asteroids;

void asteroids_setup(void *state, Rng *r, const Frame *f) {
    Asteroids *a = state;
    a->spin = rng_between(r, 0.4f, 1.0f);
    a->salt = rng_u32(r);
    (void)f;
}

static void wrap_to(float *v, float half) {
    *v = mfract((*v + half) / (2 * half)) * 2 * half - half;
}

// Where a rock is: each of its lives starts somewhere new and drifts in a straight line, wrapping at the edges.
static void rock_at(const Asteroids *a, const Grid *g, int k, int life, float since, float *x, float *y) {
    uint32_t id = a->salt ^ (uint32_t)(k * 131 + life * 7919) * 2654435761u;
    float ang = hash01(id) * TAU_F, speed = 0.08f + 0.1f * hash01(id + 1);
    *x = (hash01(id + 2) * 2 - 1) * g->aspect + mcos(ang) * speed * since;
    *y = (hash01(id + 3) * 2 - 1) + msin(ang) * speed * since;
    wrap_to(x, g->aspect + 0.15f);
    wrap_to(y, 1.15f);
}

// Asteroids in vector lines: the ship turns to the nearest rock and fires on the snare, and every rock it hits bursts
// and drifts back in from somewhere new.
void asteroids_draw(void *state, const Frame *f) {
    Asteroids *a = state;
    Grid *g = f->grid;
    dust_stars(f, a->salt, 0.0f, 0.35f);
    float start = f->t - f->local;
    int life[ROCKS] = {0}, score = 0, target = 0;
    float born[ROCKS], burst[ROCKS], bx[ROCKS], by[ROCKS], hit_x[ROCKS] = {0}, hit_y[ROCKS] = {0}, aim = 0;
    for (int k = 0; k < ROCKS; k++) born[k] = start, burst[k] = -9;
    const ScoreEvent *shots[48];
    int n = f->react > 0 ? recent_notes(f->score, ROLE_SNARE, f->t, f->local, shots, 48) : 0;
    Rgbf bullet = palette_ramp(f->palette, 1.0f);
    for (int i = n - 1; i >= 0; i--) {
        float fired = shots[i]->time, best = 1e9f;
        if (fired < start) continue;
        for (int k = 0; k < ROCKS; k++) {
            float x, y;
            rock_at(a, g, k, life[k], fired - born[k], &x, &y);
            float d = mhypot(x, y);
            if (d < best) best = d, target = k, bx[k] = x, by[k] = y;
        }
        float arrive = fired + best / 1.8f;
        aim = matan2(by[target], bx[target]);
        if (f->t < arrive) {
            float u = (f->t - fired) / (arrive - fired);
            StrokePoint p[2] = {{bx[target] * u, by[target] * u, bullet}, {bx[target] * (u + 0.04f), by[target] * (u + 0.04f), bullet}};
            stroke_draw(f->stroker, p, 2, 0);
        } else {
            burst[target] = arrive;
            hit_x[target] = bx[target], hit_y[target] = by[target];
            life[target]++;
            born[target] = arrive;
            score += 20 + 30 * (life[target] % 3);
        }
    }
    Rgbf rock = palette_ramp(f->palette, 0.85f), spark = palette_accent(f->palette, 0);
    for (int k = 0; k < ROCKS; k++) {
        float age = f->t - burst[k];
        if (age >= 0 && age < 0.6f) {
            for (int s = 0; s < 8; s++) {
                float ang = (float)s / 8 * TAU_F + (float)k, r = 0.05f + age * 0.6f;
                put_cell(g, cell_col(g, hit_x[k] + mcos(ang) * r), cell_row(g, hit_y[k] + msin(ang) * r), s & 1 ? '.' : '*',
                         frame_dim(f, spark, 1 - age / 0.6f));
            }
            if (age < 0.35f) continue;
        }
        float x, y, size = 0.09f + 0.05f * (float)(k % 3);
        rock_at(a, g, k, life[k], f->t - born[k], &x, &y);
        StrokePoint outline[9];
        float turn = f->t * a->spin * (k & 1 ? 1 : -1);
        for (int v = 0; v < 9; v++) {
            float ang = (float)v / 9 * TAU_F + turn, r = size * (0.7f + 0.3f * hash01(a->salt + (uint32_t)(k * 17 + v)));
            outline[v] = (StrokePoint){x + mcos(ang) * r, y + msin(ang) * r, rock};
        }
        stroke_draw(f->stroker, outline, 9, 1);
    }
    // The ship, a narrow wedge pointing where it last fired.
    Rgbf ship = palette_accent(f->palette, 1);
    StrokePoint hull[4] = {{mcos(aim) * 0.16f, msin(aim) * 0.16f, ship},
                           {mcos(aim + 2.5f) * 0.11f, msin(aim + 2.5f) * 0.11f, ship},
                           {mcos(aim + PI_F) * 0.05f, msin(aim + PI_F) * 0.05f, ship},
                           {mcos(aim - 2.5f) * 0.11f, msin(aim - 2.5f) * 0.11f, ship}};
    stroke_draw(f->stroker, hull, 4, 1);
    char text[24];
    snprintf(text, sizeof text, "%05d", score);
    text_at(g, 2, 0, text, palette_ramp(f->palette, 0.9f));
    text_at(g, g->cols - 8, 0, "A A A", ship);
    f->fx->glow = 0.5f + 0.2f * frame_pulse(f, ROLE_SNARE, 0.12f, 1.0f);
}

/* ---------- Breakout ---------- */

#define BRICK_ROWS 5
#define BRICK_COLS 12

typedef struct {
    float slope;
    uint32_t salt;
} Breakout;

void breakout_setup(void *state, Rng *r, const Frame *f) {
    Breakout *b = state;
    b->slope = rng_between(r, 0.35f, 0.75f);
    b->salt = rng_u32(r);
    (void)f;
}

// The ball's column on a court folded at the walls.
static float breakout_x(const Breakout *b, float cols, float t) {
    float u = mfract((t * b->slope * 9 + (float)(b->salt % 61)) / (2 * cols)) * 2 * cols;
    return u < cols ? u : 2 * cols - u;
}

// Breakout to the beat: the ball rises and falls once every two beats, taking out the lowest brick in its column at
// the top of each rise. A cleared wall comes back for the next level.
void breakout_draw(void *state, const Frame *f) {
    Breakout *b = state;
    Grid *g = f->grid;
    int wall_top = 3, paddle_row = g->rows - 2, brick_w = (g->cols - 4) / BRICK_COLS, left = (g->cols - brick_w * BRICK_COLS) / 2;
    float period = f->score->beat * 2, start = f->t - f->local, court = (float)(brick_w * BRICK_COLS - 1);
    uint8_t gone[BRICK_ROWS][BRICK_COLS] = {{0}};
    int level = 1, score = 0, apex_row = wall_top + BRICK_ROWS, left_bricks = BRICK_ROWS * BRICK_COLS;
    int cycles = (int)(f->local / period);
    for (int c = 0; c <= cycles; c++) {
        float top_time = start + ((float)c + 0.5f) * period;
        int col = (int)(breakout_x(b, court, top_time)) / brick_w, row = -1;
        if (col >= BRICK_COLS) col = BRICK_COLS - 1;
        for (int r = BRICK_ROWS - 1; r >= 0; r--)
            if (!gone[r][col]) {
                row = r;
                break;
            }
        int apex = row >= 0 ? wall_top + row + 1 : wall_top;
        if (c == cycles) {
            apex_row = apex;
            if (f->t < top_time) break;
        }
        if (row >= 0) {
            gone[row][col] = 1, score += (BRICK_ROWS - row) * 10;
            if (--left_bricks == 0) memset(gone, 0, sizeof gone), left_bricks = BRICK_ROWS * BRICK_COLS, level++;
        }
    }
    Rgbf ink = palette_ramp(f->palette, 0.95f), frame_ink = palette_ramp(f->palette, 0.45f);
    for (int r = 0; r < BRICK_ROWS; r++)
        for (int c = 0; c < BRICK_COLS; c++) {
            if (gone[r][c]) continue;
            Rgbf color = palette_accent(f->palette, r % 3);
            if (r >= 3) color = rgbf_mix(color, ink, 0.25f);
            for (int k = 0; k < brick_w; k++) put_cell(g, left + c * brick_w + k, wall_top + r, k == 0 ? '[' : k == brick_w - 1 ? ']' : '=', color);
        }
    for (int col = 0; col < g->cols; col++) put_cell(g, col, wall_top - 1, '-', frame_ink);
    float phase = mfract(f->local / period), rise = 1 - mabs(phase * 2 - 1);
    float bx = (float)left + breakout_x(b, court, f->t), by = (float)(paddle_row - 1) - rise * (float)(paddle_row - 1 - apex_row);
    put_cell(g, (int)(bx + 0.5f), (int)(by + 0.5f), 'O', ink);
    // The paddle meets the ball at the bottom of every fall, flashing on the kick.
    float hit = frame_pulse(f, ROLE_KICK, 0.15f, 1.0f);
    int px = (int)mixf((float)g->cols * 0.5f, bx, 0.85f);
    Rgbf paddle = rgbf_mix(palette_accent(f->palette, 1), ink, 0.5f * hit);
    for (int k = -3; k <= 3; k++) put_cell(g, px + k, paddle_row, k == -3 ? '<' : k == 3 ? '>' : '=', paddle);
    char text[48];
    snprintf(text, sizeof text, "SCORE %04d   LEVEL %d", score, level);
    text_at(g, 2, 0, text, ink);
    f->fx->glow = 0.45f + 0.25f * hit;
}

/* ---------- Channel scope ---------- */

typedef struct {
    float drift;
} Scope;

void scope_setup(void *state, Rng *r, const Frame *f) {
    Scope *s = state;
    s->drift = rng_between(r, 0.6f, 1.4f);
    (void)f;
}

// A sound chip's four voices on four scopes, drawn from the score: each lane's wave takes its pitch and swell from its
// part's last note, a pulse for the lead and the chords, a triangle for the bass and noise for the drums.
void scope_draw(void *state, const Frame *f) {
    Scope *s = state;
    Grid *g = f->grid;
    int chip = f->score->info->mood == MOOD_8BIT;
    static const char *const CHIP_NAMES[4] = {"PULSE 1", "PULSE 2", "TRIANGLE", "NOISE"};
    static const char *const NAMES[4] = {"LEAD", "CHORDS", "BASS", "DRUMS"};
    static const Role ROLES[4][3] = {{ROLE_LEAD, ROLE_BELL, ROLE_PLUCK}, {ROLE_ARP, ROLE_CHORD, ROLE_PAD},
                                     {ROLE_BASS, ROLE_BASS, ROLE_BASS}, {ROLE_KICK, ROLE_SNARE, ROLE_HAT}};
    int lane_h = (g->rows - 2) / 4, label_w = 10, width = g->cols - label_w - 2;
    Rgbf frame_ink = palette_ramp(f->palette, 0.35f), label = palette_ramp(f->palette, 0.75f);
    for (int lane = 0; lane < 4; lane++) {
        int top = 1 + lane * lane_h, mid = top + lane_h / 2;
        const ScoreEvent *last = NULL;
        float echo_lag = 0;
        for (int k = 0; k < 3; k++) {
            const ScoreEvent *e = score_last(f->score, ROLES[lane][k], f->t);
            if (e && (!last || e->time > last->time)) last = e;
        }
        // On a chip, pulse two answers the lead three rows behind it whenever it isn't playing chords.
        float row_time = (float)f->score->info->bar_seconds / (float)f->score->info->rows_per_bar;
        const ScoreEvent *lead = chip && lane == 1 ? score_last(f->score, ROLE_LEAD, f->t - 3 * row_time) : NULL;
        if (lead && (!last || lead->time + 3 * row_time > last->time)) last = lead, echo_lag = 3 * row_time;
        float swell = 0, cycles = 3;
        if (last && f->react > 0) {
            float age = f->t - last->time - echo_lag;
            swell = last->velocity * mexp(-age / (lane == 2 ? 0.6f : lane == 3 ? 0.12f : 0.35f));
            cycles = 2 + (float)(last->note > 24 ? last->note - 24 : 0) / 9.0f;
        }
        swell = 0.15f + 0.85f * mmin(1.0f, swell);
        text_at(g, 1, mid, (chip ? CHIP_NAMES : NAMES)[lane], label);
        for (int col = 0; col < g->cols; col++) put_cell(g, col, top + lane_h - 1, '-', frame_dim(f, frame_ink, 0.6f));
        Rgbf wave_ink = palette_accent(f->palette, lane % 3);
        int prev = mid;
        for (int x = 0; x < width; x++) {
            float u = (float)x / (float)width * cycles + f->t * s->drift, v;
            if (lane == 3)
                v = hash01((uint32_t)(x * 2654435761u) ^ (uint32_t)(f->t * 30)) * 2 - 1;
            else if (lane == 2)
                v = 1 - 4 * mabs(mfract(u) - 0.5f);
            else
                v = mfract(u) < (lane == 0 ? 0.5f : 0.25f) ? 1.0f : -1.0f;
            int row = mid - (int)(v * swell * (float)(lane_h / 2 - 1) + (v > 0 ? 0.5f : -0.5f));
            int col = label_w + x;
            if (row == prev)
                put_cell(g, col, row, v > 0 ? '"' : '_', wave_ink);
            else if (mabs((float)(row - prev)) == 1)
                put_cell(g, col, row, row < prev ? '/' : '\\', wave_ink);
            else
                for (int y = prev < row ? prev : row; y <= (prev < row ? row : prev); y++) put_cell(g, col, y, '|', wave_ink);
            prev = row;
        }
    }
    f->fx->glow = 0.5f;
}

/* ---------- Lighthouse ---------- */

typedef struct {
    float period;
    int right;
    uint32_t salt;
} Lighthouse;

void lighthouse_setup(void *state, Rng *r, const Frame *f) {
    Lighthouse *l = state;
    l->period = rng_between(r, 8.0f, 12.0f);
    l->right = rng_chance(r, 50);
    l->salt = rng_u32(r);
    (void)f;
}

// A lighthouse on a dark point, its beam sweeping out over a rolling sea and back round behind the tower.
void lighthouse_draw(void *state, const Frame *f) {
    Lighthouse *l = state;
    Grid *g = f->grid;
    dust_stars(f, l->salt, 0.03f, 0.6f);
    int horizon = (int)((float)g->rows * 0.55f), tower_h = (int)((float)g->rows * 0.42f);
    int tower_x = l->right ? g->cols - 12 : 6, lamp_row = horizon - tower_h + 1;
    Rgbf sea = palette_accent(f->palette, 2), stripe_a = palette_ramp(f->palette, 0.9f), stripe_b = palette_accent(f->palette, 0);
    Rgbf rock = palette_ramp(f->palette, 0.3f), beam = palette_ramp(f->palette, 1.0f);
    for (int row = horizon; row < g->rows; row++) {
        float near = (float)(row - horizon + 1) / (float)(g->rows - horizon);
        for (int col = 0; col < g->cols; col++) {
            float w = msin((float)col * (0.16f + 0.1f * (1 - near)) + f->t * 2.4f * frame_pace(f) + (float)row * 1.7f);
            if (w > 0.35f) put_cell(g, col, row, w > 0.8f ? '~' : '-', frame_dim(f, sea, 0.55f + 0.45f * near));
        }
    }
    // The beam: long and low when it points along the coast, short as it turns toward or away from us.
    float a = f->t * TAU_F / l->period * frame_pace(f), reach = mcos(a);
    int dir = reach > 0 ? 1 : -1;
    float length = mabs(reach) * (float)g->cols * 0.9f, level = (msin(a) > 0 ? 0.55f : 0.3f) * (f->calm ? 0.6f : 1.0f);
    for (int k = 1; k < (int)length; k++) {
        int col = tower_x + 2 + dir * k, spread = 1 + k / 10;
        for (int d = -spread; d <= spread; d++) {
            float fade = level * (1 - (float)k / (length + 1)) * (1 - mabs((float)d) / (float)(spread + 1));
            put_cell_under(g, col, lamp_row + d, d == 0 ? '-' : '.', frame_dim(f, beam, fade));
        }
    }
    for (int y = lamp_row + 1; y < horizon; y++) {
        int half = 1 + (y - lamp_row) / 6;
        for (int x = -half; x <= half + 2; x++) put_cell(g, tower_x + 2 + x - 1, y, x == -half ? '/' : x == half + 2 ? '\\' : '#', ((y / 2) & 1) ? stripe_a : stripe_b);
    }
    text_at(g, tower_x, lamp_row - 1, "_/^\\_", stripe_a);
    text_at(g, tower_x, lamp_row, "|[*]|", beam);
    for (int x = -6; x <= 10; x++) {
        int h = (int)(1.5f + 1.5f * hash01(l->salt + (uint32_t)(x + 7)));
        for (int y = 0; y < h; y++) put_cell(g, tower_x + x, horizon + y, y == 0 ? '^' : '#', rock);
    }
    f->fx->glow = 0.35f + 0.15f * level;
}

/* ---------- Drum machine ---------- */

typedef struct {
    float knob;
} DrumMachine;

void drums_setup(void *state, Rng *r, const Frame *f) {
    DrumMachine *d = state;
    d->knob = rng_between(r, 0, 1);
    (void)f;
}

// A step sequencer playing the piece's own drums: each row lights the steps its part hits in this bar, under a
// playhead walking the steps in time.
void drums_draw(void *state, const Frame *f) {
    DrumMachine *d = state;
    Grid *g = f->grid;
    static const char *const LABELS[4] = {"KICK ", "SNARE", "HATS ", "PERC "};
    static const Role ROWS[4][2] = {{ROLE_KICK, ROLE_KICK}, {ROLE_SNARE, ROLE_SNARE}, {ROLE_HAT, ROLE_HAT}, {ROLE_PERC, ROLE_CRASH}};
    const SongInfo *info = f->score->info;
    int steps = info->rows_per_bar, cell_w = (g->cols - 10) / steps > 4 ? 4 : 3;
    float bar = (float)info->bar_seconds, step = bar / (float)steps, bar_start = mfloor(f->t / bar) * bar;
    int now = (int)((f->t - bar_start) / step), left = (g->cols - 7 - steps * cell_w) / 2 + 7, top = g->rows / 2 - 5;
    Rgbf ink = palette_ramp(f->palette, 0.95f), dim = palette_ramp(f->palette, 0.35f), led = palette_accent(f->palette, 0);
    char text[40];
    snprintf(text, sizeof text, "DRUM MACHINE   BPM %d", info->bpm);
    text_at(g, left - 7, top - 3, text, ink);
    for (int r = 0; r < 4; r++) {
        int row = top + r * 2;
        text_at(g, left - 7, row, LABELS[r], palette_ramp(f->palette, 0.7f));
        for (int s = 0; s < steps; s++) {
            float from = bar_start + (float)s * step, to = from + step;
            int hit = f->react > 0 && (score_count(f->score, ROWS[r][0], from, to) || score_count(f->score, ROWS[r][1], from, to));
            int here = s == now, col = left + s * cell_w;
            Rgbf color = hit ? (here ? ink : palette_accent(f->palette, r % 3)) : (here ? palette_ramp(f->palette, 0.6f) : dim);
            text_at(g, col, row, hit ? "[#]" : "[ ]", color);
        }
    }
    for (int s = 0; s < steps; s++) put_cell(g, left + s * cell_w + 1, top + 8, s == now ? '^' : (s % 4 ? '.' : ':'), s == now ? led : dim);
    // Three knobs that turn with the music's energy.
    static const char *const KNOBS[3] = {"TEMPO", "SWING", "TUNE"};
    static const char TURN[4] = {'|', '/', '-', '\\'};
    for (int k = 0; k < 3; k++) {
        int col = left + k * 12, turn = (int)((d->knob + score_energy(f->score, f->t) * 0.8f + (float)k * 0.3f) * 4) & 3;
        text_at(g, col, top + 11, "(", dim);
        put_cell(g, col + 1, top + 11, TURN[turn], led);
        text_at(g, col + 2, top + 11, ")", dim);
        text_at(g, col + 4, top + 11, KNOBS[k], palette_ramp(f->palette, 0.6f));
    }
    f->fx->glow = 0.4f + 0.3f * frame_pulse(f, ROLE_KICK, 0.12f, 1.0f);
}
